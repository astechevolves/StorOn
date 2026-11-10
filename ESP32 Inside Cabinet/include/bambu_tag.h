#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <MFRC522.h>

// Bambu Lab spool tags: MIFARE Classic 1K, each sector locked with a key
// derived from the tag's 4-byte UID. Key derivation and data layout follow
// the Bambu Research Group's published findings
// (github.com/Bambu-Research-Group/RFID-Tag-Guide). Read-only: the data is
// RSA-signed by Bambu, so tags can't be meaningfully rewritten.

struct BambuTag
{
  bool valid = false;
  char trayUuid[33] = "";   // 16 bytes as hex; the AMS reports this as "tray_uuid"
  char materialId[9] = "";  // e.g. "GFA00"
  char variantId[9] = "";   // e.g. "A00-K0"
  char type[17] = "";       // e.g. "PLA"
  char detailedType[17] = ""; // e.g. "PLA Basic"
  char color[10] = "";      // "#RRGGBBAA"
  uint16_t spoolWeightG = 0;
  float diameterMm = 0;
  uint16_t dryingTempC = 0, dryingHours = 0, bedTempC = 0;
  uint16_t hotendMinC = 0, hotendMaxC = 0;
  uint16_t lengthM = 0;
  char productionDate[17] = ""; // "YYYY_MM_DD_HH_MM"
};

// The 16 per-sector Key A values for a 4-byte UID
void bambuDeriveKeys(const byte uid[4], byte keys[16][6]);

// Reads and decodes the data sectors of a selected tag (r.uid must be the
// tag just selected). Returns false if it isn't a Bambu tag or a read failed.
bool bambuReadTag(MFRC522 &r, BambuTag &out);

void bambuToJson(const BambuTag &t, JsonObject out);
