#include "bambu_tag.h"
#include <mbedtls/md.h>

// HKDF-SHA256 (RFC 5869) with the tag UID as input key material, this
// constant as salt and "RFID-A\0" as info, expanded to 16 keys x 6 bytes.
static const byte BAMBU_SALT[16] = {0x9a, 0x75, 0x9c, 0xf2, 0xc4, 0xf7, 0xca, 0xff,
                                    0x22, 0x2c, 0xb9, 0x76, 0x9b, 0x41, 0xbc, 0x96};
static const byte BAMBU_INFO[7] = {'R', 'F', 'I', 'D', '-', 'A', 0};

static void hmacSha256(const byte *key, size_t keyLen, const byte *msg, size_t msgLen,
                       byte out[32])
{
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), key, keyLen, msg, msgLen,
                  out);
}

void bambuDeriveKeys(const byte uid[4], byte keys[16][6])
{
  byte prk[32];
  hmacSha256(BAMBU_SALT, sizeof(BAMBU_SALT), uid, 4, prk); // HKDF-Extract

  // HKDF-Expand: T(i) = HMAC(PRK, T(i-1) | info | i), 96 bytes = 3 blocks
  byte okm[96];
  byte t[32];
  byte msg[32 + sizeof(BAMBU_INFO) + 1];
  size_t tLen = 0;
  for (int i = 1; i <= 3; i++)
  {
    memcpy(msg, t, tLen);
    memcpy(msg + tLen, BAMBU_INFO, sizeof(BAMBU_INFO));
    msg[tLen + sizeof(BAMBU_INFO)] = i;
    hmacSha256(prk, sizeof(prk), msg, tLen + sizeof(BAMBU_INFO) + 1, t);
    memcpy(okm + (i - 1) * 32, t, 32);
    tLen = 32;
  }
  memcpy(keys, okm, 96);
}

static uint16_t u16le(const byte *p) { return p[0] | (p[1] << 8); }

static void copyStr(char *out, size_t outLen, const byte *src, size_t srcLen)
{
  size_t n = 0;
  for (; n < srcLen && n < outLen - 1 && src[n]; n++)
    out[n] = (src[n] >= 0x20 && src[n] < 0x7F) ? src[n] : '?';
  out[n] = '\0';
}

bool bambuReadTag(MFRC522 &r, BambuTag &out)
{
  out = BambuTag();
  if (r.uid.size != 4 || MFRC522::PICC_GetType(r.uid.sak) != MFRC522::PICC_TYPE_MIFARE_1K)
    return false;

  byte keys[16][6];
  bambuDeriveKeys(r.uid.uidByte, keys);

  // Data lives in sectors 0-4 (blocks 0-18); sectors 10-15 hold the signature
  byte blocks[20][16] = {};
  MFRC522::Uid uid = r.uid;
  for (int s = 0; s < 5; s++)
  {
    MFRC522::MIFARE_Key key;
    memcpy(key.keyByte, keys[s], 6);
    if (r.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, s * 4 + 3, &key, &uid) !=
        MFRC522::STATUS_OK)
    {
      r.PCD_StopCrypto1();
      return false;
    }
    for (int b = 0; b < 3; b++) // skip the trailer (keys and access bits)
    {
      byte buf[18];
      byte size = sizeof(buf);
      if (r.MIFARE_Read(s * 4 + b, buf, &size) != MFRC522::STATUS_OK)
      {
        r.PCD_StopCrypto1();
        return false;
      }
      memcpy(blocks[s * 4 + b], buf, 16);
    }
  }
  r.PCD_StopCrypto1();

  // All multi-byte numbers are little-endian
  copyStr(out.variantId, sizeof(out.variantId), blocks[1], 8);
  copyStr(out.materialId, sizeof(out.materialId), blocks[1] + 8, 8);
  copyStr(out.type, sizeof(out.type), blocks[2], 16);
  copyStr(out.detailedType, sizeof(out.detailedType), blocks[4], 16);
  snprintf(out.color, sizeof(out.color), "#%02X%02X%02X%02X", blocks[5][0], blocks[5][1],
           blocks[5][2], blocks[5][3]);
  out.spoolWeightG = u16le(blocks[5] + 4);
  memcpy(&out.diameterMm, blocks[5] + 8, 4);
  out.dryingTempC = u16le(blocks[6]);
  out.dryingHours = u16le(blocks[6] + 2);
  out.bedTempC = u16le(blocks[6] + 6);
  out.hotendMaxC = u16le(blocks[6] + 8);
  out.hotendMinC = u16le(blocks[6] + 10);
  for (int i = 0; i < 16; i++)
    snprintf(out.trayUuid + i * 2, 3, "%02X", blocks[9][i]);
  copyStr(out.productionDate, sizeof(out.productionDate), blocks[12], 16);
  out.lengthM = u16le(blocks[14] + 4);
  out.valid = true;
  return true;
}

void bambuToJson(const BambuTag &t, JsonObject o)
{
  // String() makes ArduinoJson copy the text; a plain const char* is stored
  // by reference and would dangle once a temporary BambuTag goes away
  o["tray_uuid"] = String(t.trayUuid);
  o["material_id"] = String(t.materialId);
  o["variant_id"] = String(t.variantId);
  o["type"] = String(t.type);
  o["detailed_type"] = String(t.detailedType);
  o["color"] = String(t.color);
  o["spool_weight_g"] = t.spoolWeightG;
  o["diameter_mm"] = serialized(String(t.diameterMm, 2));
  o["drying_temp_c"] = t.dryingTempC;
  o["drying_hours"] = t.dryingHours;
  o["bed_temp_c"] = t.bedTempC;
  o["hotend_min_c"] = t.hotendMinC;
  o["hotend_max_c"] = t.hotendMaxC;
  o["length_m"] = t.lengthM;
  o["production_date"] = String(t.productionDate);
}
