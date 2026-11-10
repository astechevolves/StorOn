#include "opentag3d.h"

static const char MIME_TYPE[] = "application/opentag3d";

enum FieldType
{
  F_STR,
  F_INT,
  F_RGBA,
  F_DATE,
  F_TIME
};

struct Field
{
  const char *id;
  uint8_t offset;
  uint8_t len;
  FieldType type;
  float scale; // stored value = real value / scale (F_INT only)
  bool required;
};

// Payload layout from the OpenTag3D 2.004 spec (offsets are payload-relative)
static const Field FIELDS[] = {
    {"tag_version", 0x00, 2, F_INT, 0.001f, true},
    {"material", 0x02, 5, F_STR, 1, true},
    {"material_mod", 0x07, 5, F_STR, 1, false},
    {"manufacturer", 0x0C, 16, F_STR, 1, true},
    {"color_name", 0x1C, 32, F_STR, 1, false},
    {"color_1", 0x3C, 4, F_RGBA, 1, true},
    {"color_2", 0x40, 4, F_RGBA, 1, false},
    {"color_3", 0x44, 4, F_RGBA, 1, false},
    {"color_4", 0x48, 4, F_RGBA, 1, false},
    {"serial", 0x4C, 32, F_STR, 1, false},
    {"sku", 0x6C, 16, F_STR, 1, false},
    {"barcode", 0x7C, 6, F_INT, 1, false},
    {"mfg_date", 0x84, 4, F_DATE, 1, false},
    {"mfg_time", 0x88, 3, F_TIME, 1, false},
    {"diameter", 0x8C, 2, F_INT, 0.001f, true},
    {"tolerance", 0x8E, 1, F_INT, 0.01f, false},
    {"nozzle_diameter", 0x8F, 1, F_INT, 0.1f, false},
    {"print_temp", 0x90, 1, F_INT, 5, true},
    {"min_print_temp", 0x91, 1, F_INT, 5, false},
    {"max_print_temp", 0x92, 1, F_INT, 5, false},
    {"chamber_temp", 0x93, 1, F_INT, 5, true},
    {"bed_temp", 0x94, 1, F_INT, 5, true},
    {"min_bed_temp", 0x95, 1, F_INT, 5, false},
    {"max_bed_temp", 0x96, 1, F_INT, 5, false},
    {"target_vso", 0x97, 1, F_INT, 1, false},
    {"min_vso", 0x98, 1, F_INT, 1, false},
    {"max_vso", 0x99, 1, F_INT, 1, false},
    {"max_dry_temp", 0x9A, 1, F_INT, 5, false},
    {"dry_time", 0x9B, 1, F_INT, 1, false},
    {"density", 0x9C, 2, F_INT, 0.001f, true},
    {"weight", 0x9E, 2, F_INT, 1, true},
    {"empty_spool_weight", 0xA0, 2, F_INT, 1, false},
    {"measured_length", 0xA2, 2, F_INT, 1, false},
    {"measured_weight", 0xA4, 2, F_INT, 1, false},
    {"spool_core_diameter", 0xA6, 1, F_INT, 1, false},
    {"td", 0xA7, 1, F_INT, 0.1f, false},
    {"mfi_temp", 0xA8, 1, F_INT, 5, false},
    {"mfi_value", 0xAA, 1, F_INT, 1, false},
    {"mfi_load", 0xAB, 2, F_INT, 0.001f, false},
    {"data_url", 0xB8, 32, F_STR, 1, false},
};
static const int PAYLOAD_MAX = 0xD8;  // end of data_url
static const int PAYLOAD_MIN = 0xA0;  // always write through "weight"

// ── NTAG access ───────────────────────────────────────────
static bool reselect(MFRC522 &r, const MFRC522::Uid &uid)
{
  byte atqa[2];
  byte size = sizeof(atqa);
  r.PICC_WakeupA(atqa, &size);
  MFRC522::Uid again = uid;
  return r.PICC_Select(&again) == MFRC522::STATUS_OK;
}

// Read 4 pages (16 bytes) starting at page, retrying a weakly coupled tag
static bool read16(MFRC522 &r, int page, byte *out)
{
  MFRC522::Uid uid = r.uid;
  for (int attempt = 0; attempt < 3; attempt++)
  {
    byte buf[18];
    byte size = sizeof(buf);
    if (r.MIFARE_Read(page, buf, &size) == MFRC522::STATUS_OK)
    {
      memcpy(out, buf, 16);
      return true;
    }
    reselect(r, uid);
  }
  return false;
}

static bool writePage(MFRC522 &r, int page, const byte *data)
{
  MFRC522::Uid uid = r.uid;
  for (int attempt = 0; attempt < 3; attempt++)
  {
    byte buf[4];
    memcpy(buf, data, 4);
    if (r.MIFARE_Ultralight_Write(page, buf, 4) == MFRC522::STATUS_OK)
      return true;
    reselect(r, uid);
  }
  return false;
}

// Header pages 0-3: UID, static lock bytes (page 2 bytes 2-3), capability
// container (page 3). Returns user-memory size in bytes, 0 if not usable.
static int checkHeader(MFRC522 &r, String &err, bool forWrite)
{
  byte h[16];
  if (!read16(r, 0, h))
  {
    err = "Couldn't read the tag header";
    return -1;
  }
  const byte *cc = h + 12;
  if (cc[0] != 0xE1)
  {
    err = "Tag isn't NDEF formatted (capability container missing)";
    return 0;
  }
  if (forWrite && cc[3] != 0x00)
  {
    err = "Tag is write-protected";
    return 0;
  }
  if (forWrite && (h[10] || h[11]))
  {
    err = "Tag has locked pages; refusing to write";
    return 0;
  }
  return cc[2] * 8;
}

// ── Decoding ──────────────────────────────────────────────
static uint32_t beInt(const byte *p, int len)
{
  uint32_t v = 0;
  for (int i = 0; i < len && i < 4; i++)
    v = (v << 8) | p[i];
  return v;
}

static void decodePayload(const byte *p, int len, JsonObject out)
{
  byte full[PAYLOAD_MAX] = {}; // missing trailing bytes read as 0x00
  memcpy(full, p, min(len, PAYLOAD_MAX));
  for (const Field &f : FIELDS)
  {
    const byte *v = full + f.offset;
    switch (f.type)
    {
    case F_STR:
    {
      char s[33];
      int n = 0;
      for (; n < f.len && v[n]; n++)
        s[n] = v[n];
      s[n] = '\0';
      if (n)
        out[f.id] = s;
      break;
    }
    case F_INT:
    {
      uint64_t raw = 0;
      for (int i = 0; i < f.len; i++)
        raw = (raw << 8) | v[i];
      if (!raw)
        break; // not set
      if (f.scale == 1)
        out[f.id] = raw;
      else if (f.scale >= 1)
        out[f.id] = (uint32_t)(raw * f.scale + 0.5f);
      else
        out[f.id] = serialized(String(raw * f.scale, f.scale < 0.01f ? 3 : f.scale < 0.1f ? 2 : 1));
      break;
    }
    case F_RGBA:
    {
      if (!v[0] && !v[1] && !v[2] && !v[3])
        break;
      char s[10];
      snprintf(s, sizeof(s), "#%02X%02X%02X%02X", v[0], v[1], v[2], v[3]);
      out[f.id] = s;
      break;
    }
    case F_DATE:
    {
      uint16_t y = beInt(v, 2);
      if (!y)
        break;
      char s[11];
      snprintf(s, sizeof(s), "%04u-%02u-%02u", y, v[2], v[3]);
      out[f.id] = s;
      break;
    }
    case F_TIME:
    {
      if (!v[0] && !v[1] && !v[2])
        break;
      char s[9];
      snprintf(s, sizeof(s), "%02u:%02u:%02u", v[0], v[1], v[2]);
      out[f.id] = s;
      break;
    }
    }
  }
}

// Walk the TLVs in user memory and return the OpenTag3D record's payload
static bool findPayload(const byte *mem, int memLen, const byte **payload, int *payloadLen)
{
  int i = 0;
  while (i < memLen)
  {
    byte t = mem[i++];
    if (t == 0x00)
      continue; // NULL TLV
    if (t == 0xFE || i >= memLen)
      return false; // terminator
    int len = mem[i++];
    if (len == 0xFF && i + 2 <= memLen)
    {
      len = (mem[i] << 8) | mem[i + 1];
      i += 2;
    }
    if (t != 0x03)
    {
      i += len; // lock/memory control or proprietary TLV
      continue;
    }
    // NDEF message: walk its records
    int end = min(i + len, memLen);
    while (i + 3 <= end)
    {
      byte hdr = mem[i++];
      byte tnf = hdr & 0x07;
      bool sr = hdr & 0x10, il = hdr & 0x08, me = hdr & 0x40;
      int typeLen = mem[i++];
      uint32_t plen;
      if (sr)
        plen = mem[i++];
      else
      {
        if (i + 4 > end)
          return false;
        plen = beInt(mem + i, 4);
        i += 4;
      }
      int idLen = il ? mem[i++] : 0;
      const byte *type = mem + i;
      i += typeLen + idLen;
      if (i + (int)plen > end)
        return false;
      if (tnf == 0x02 && typeLen == (int)strlen(MIME_TYPE) &&
          memcmp(type, MIME_TYPE, typeLen) == 0)
      {
        *payload = mem + i;
        *payloadLen = plen;
        return true;
      }
      i += plen;
      if (me)
        break;
    }
    return false;
  }
  return false;
}

Ot3dResult ot3dRead(MFRC522 &r, JsonObject out, String &err)
{
  int cap = checkHeader(r, err, false);
  if (cap < 0)
    return OT3D_IO_ERROR;
  if (cap == 0)
    return OT3D_NOT_OT3D;
  cap = min(cap, 888); // NTAG216 user memory

  // Read the first chunk, work out how long the NDEF TLV is, then read the rest
  static byte mem[896];
  if (!read16(r, 4, mem))
  {
    err = "Read failed";
    return OT3D_IO_ERROR;
  }
  int need = 16;
  for (int i = 0; i < 12;)
  {
    if (mem[i] == 0x00)
    {
      i++;
      continue;
    }
    int len = mem[i + 1], hdr = 2;
    if (len == 0xFF)
    {
      len = (mem[i + 2] << 8) | mem[i + 3];
      hdr = 4;
    }
    if (mem[i] == 0x03)
    {
      need = i + hdr + len;
      break;
    }
    if (mem[i] == 0xFE)
      break;
    i += hdr + len;
  }
  need = min(need, cap);
  for (int off = 16; off < need; off += 16)
    if (!read16(r, 4 + off / 4, mem + off))
    {
      err = "Read failed partway through";
      return OT3D_IO_ERROR;
    }

  const byte *payload;
  int plen;
  if (!findPayload(mem, need, &payload, &plen))
  {
    err = "No OpenTag3D record on this tag";
    return OT3D_NOT_OT3D;
  }
  uint16_t version = beInt(payload, 2);
  if (version / 1000 > OT3D_VERSION / 1000)
  {
    err = String("Tag uses OpenTag3D ") + (version / 1000) + ".x, newer than this firmware supports";
    return OT3D_NOT_OT3D;
  }
  decodePayload(payload, plen, out);
  return OT3D_OK;
}

// ── Encoding ──────────────────────────────────────────────
static bool parseHexColor(const char *s, byte out[4])
{
  if (*s == '#')
    s++;
  size_t n = strlen(s);
  if (n != 6 && n != 8)
    return false;
  for (size_t i = 0; i < n; i++)
    if (!isxdigit(s[i]))
      return false;
  for (int i = 0; i < 3; i++)
  {
    char b[3] = {s[i * 2], s[i * 2 + 1], 0};
    out[i] = strtoul(b, nullptr, 16);
  }
  out[3] = 0xFF;
  if (n == 8)
  {
    char b[3] = {s[6], s[7], 0};
    out[3] = strtoul(b, nullptr, 16);
  }
  return true;
}

static bool encodePayload(JsonObjectConst in, byte *p, int &len, String &err)
{
  memset(p, 0, PAYLOAD_MAX);
  len = PAYLOAD_MIN;
  int written = 0;
  for (const Field &f : FIELDS)
  {
    byte *v = p + f.offset;
    JsonVariantConst val = in[f.id];
    if (strcmp(f.id, "tag_version") == 0)
    {
      v[0] = OT3D_VERSION >> 8;
      v[1] = OT3D_VERSION & 0xFF;
      continue;
    }
    // Write what's known: a blank field stays all zero bytes, which readers
    // treat as "not set". OpenTag3D marks some fields required, but at first
    // load (before calibration) not everything is known; rewrite the tag later.
    bool empty = val.isNull() || (val.is<const char *>() && !*val.as<const char *>());
    if (empty)
      continue;
    written++;
    switch (f.type)
    {
    case F_STR:
    {
      const char *s = val.as<const char *>();
      if (!s)
      {
        err = String(f.id) + " must be text";
        return false;
      }
      size_t n = strlen(s);
      if (n > f.len)
      {
        err = String(f.id) + " is too long (max " + f.len + " bytes)";
        return false;
      }
      memcpy(v, s, n);
      break;
    }
    case F_INT:
    {
      double real = val.as<double>();
      double raw = real / f.scale;
      double maxRaw = f.len >= 6 ? 281474976710655.0 : (double)((1ULL << (8 * f.len)) - 1);
      if (raw < 0 || raw + 0.5 > maxRaw + 1)
      {
        err = String(f.id) + " is out of range";
        return false;
      }
      uint64_t iv = (uint64_t)(raw + 0.5);
      for (int i = f.len - 1; i >= 0; i--, iv >>= 8)
        v[i] = iv & 0xFF;
      break;
    }
    case F_RGBA:
      if (!parseHexColor(val.as<const char *>() ? val.as<const char *>() : "", v))
      {
        err = String(f.id) + " must be a color like #FF8800 or #FF8800FF";
        return false;
      }
      break;
    case F_DATE:
    {
      unsigned y, m, d;
      if (sscanf(val.as<const char *>() ? val.as<const char *>() : "", "%u-%u-%u", &y, &m, &d) != 3 ||
          m < 1 || m > 12 || d < 1 || d > 31)
      {
        err = String(f.id) + " must be a date like 2026-11-08";
        return false;
      }
      v[0] = y >> 8;
      v[1] = y & 0xFF;
      v[2] = m;
      v[3] = d;
      break;
    }
    case F_TIME:
    {
      unsigned h, mi, s = 0;
      if (sscanf(val.as<const char *>() ? val.as<const char *>() : "", "%u:%u:%u", &h, &mi, &s) < 2 ||
          h > 23 || mi > 59 || s > 59)
      {
        err = String(f.id) + " must be a time like 14:30";
        return false;
      }
      v[0] = h;
      v[1] = mi;
      v[2] = s;
      break;
    }
    }
    len = max(len, (int)(f.offset + f.len));
  }
  // Trim trailing zero bytes (readers treat missing bytes as 0x00)
  if (!written)
  {
    err = "Fill in at least one field";
    return false;
  }
  while (len > PAYLOAD_MIN && p[len - 1] == 0)
    len--;
  return true;
}

bool ot3dWrite(MFRC522 &r, JsonObjectConst fields, String &err)
{
  byte payload[PAYLOAD_MAX];
  int plen;
  if (!encodePayload(fields, payload, plen, err))
    return false;

  // TLV: 03 <len> [record] FE, record = D2 (MB|ME|SR, TNF=MIME) <typeLen> <payloadLen> type payload
  byte buf[8 + sizeof(MIME_TYPE) + PAYLOAD_MAX + 4];
  int typeLen = strlen(MIME_TYPE);
  int recLen = 3 + typeLen + plen;
  int n = 0;
  buf[n++] = 0x03;
  if (recLen < 0xFF)
    buf[n++] = recLen;
  else
  {
    buf[n++] = 0xFF;
    buf[n++] = recLen >> 8;
    buf[n++] = recLen & 0xFF;
  }
  buf[n++] = 0xD2;
  buf[n++] = typeLen;
  buf[n++] = plen; // short record: payload < 256
  memcpy(buf + n, MIME_TYPE, typeLen);
  n += typeLen;
  memcpy(buf + n, payload, plen);
  n += plen;
  buf[n++] = 0xFE;
  while (n % 4)
    buf[n++] = 0x00;

  int cap = checkHeader(r, err, true);
  if (cap <= 0)
    return false;
  if (n > cap)
  {
    err = String("Data needs ") + n + " bytes but the tag only has " + cap +
          " (use NTAG215 or NTAG216)";
    return false;
  }

  for (int off = 0; off < n; off += 4)
    if (!writePage(r, 4 + off / 4, buf + off))
    {
      err = String("Write failed at page ") + (4 + off / 4) + "; hold the tag closer and retry";
      return false;
    }

  // Verify by reading back
  byte check[16];
  for (int off = 0; off < n; off += 16)
  {
    if (!read16(r, 4 + off / 4, check))
    {
      err = "Written, but the read-back failed; try Read tag to check it";
      return false;
    }
    if (memcmp(check, buf + off, min(16, n - off)) != 0)
    {
      err = String("Verify failed near page ") + (4 + off / 4);
      return false;
    }
  }
  return true;
}

void ot3dFieldsJson(JsonArray out)
{
  static const char *TYPES[] = {"str", "int", "rgba", "date", "time"};
  for (const Field &f : FIELDS)
  {
    if (strcmp(f.id, "tag_version") == 0)
      continue; // set by the firmware
    JsonObject o = out.add<JsonObject>();
    o["id"] = f.id;
    o["type"] = TYPES[f.type];
    o["len"] = f.len;
    o["required"] = f.required;
  }
}
