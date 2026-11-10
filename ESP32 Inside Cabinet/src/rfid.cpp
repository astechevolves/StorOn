#include "rfid.h"
#include <SPI.h>
#include <MFRC522.h>
#include "settings.h"
#include "diag.h"
#include "bambu_tag.h"
#include "opentag3d.h"
#include "spoolman.h"
#include <Preferences.h>
#include "moonraker.h"
#include <driver/gpio.h>

#define FIELD_SETTLE_MS 10   // time for a tag to power up after the antenna turns on
#define MISS_LIMIT 3         // misses in a row before a never-seen lane is reported empty
#define VISIBLE_MS 3000      // tag counts as "in view" this long after a read
#define UID_LEN 21           // 10-byte UID as hex + terminator
#define PROBE_RETRY_MS 10000 // retry a missing reader this often (e.g. its supply came up late)
#define DECODE_TRIES 10      // decode attempts per new tag (a passing tag can leave mid-read)
#define INFO_LEN 768         // decoded spool data, serialized JSON

struct Lane
{
  MFRC522 reader;
  bool present = false; // reader answered
  byte version = 0;     // VersionReg: 0x91/0x92 genuine, other values on clones
  unsigned long lastProbe = 0;
  bool known = false;
  char reported[UID_LEN] = "";
  int missCount = 0;
  // Diagnostics
  char lastUid[UID_LEN] = "";
  byte lastSak = 0;
  unsigned long lastSeen = 0; // millis() of the last successful read, 0 = never
  uint32_t reads = 0, hits = 0;
  // Decoded spool data (Bambu or OpenTag3D) for the tag in infoFor, decoded
  // once per new tag
  char info[INFO_LEN] = "";
  char infoFor[UID_LEN] = "";
  int infoTries = 0;
  bool infoDone = false; // decoded, or known not to hold spool data
};

static Lane lanes[MAX_LANES];
static int nextLane = 0;
static unsigned long lastStep = 0;

bool laneActive(int i) { return i >= 0 && i < MAX_LANES && cfg.laneEnabled[i]; }
bool laneKnown(int i) { return lanes[i].known; }
const char *laneReported(int i) { return lanes[i].reported; }

static int activeCount()
{
  int n = 0;
  for (int i = 0; i < MAX_LANES; i++)
    n += cfg.laneEnabled[i];
  return n;
}

bool laneTagInfo(int i, JsonObject out)
{
  const Lane &lane = lanes[i];
  if (!lane.info[0] || !lane.reported[0] || strcmp(lane.infoFor, lane.reported) != 0)
    return false;
  JsonDocument tmp;
  if (deserializeJson(tmp, lane.info))
    return false;
  for (JsonPairConst kv : tmp.as<JsonObjectConst>())
    out[kv.key()] = kv.value();
  return true;
}

bool laneSpoolKey(int i, char *out, size_t len)
{
  const Lane &lane = lanes[i];
  if (!lane.known || !lane.reported[0])
    return false;
  bool decoded = strcmp(lane.infoFor, lane.reported) == 0 && lane.infoDone;
  if (!decoded)
    return false; // wait for decoding, so a Bambu tag is keyed by tray_uuid
  if (lane.info[0])
  {
    JsonDocument tmp;
    if (!deserializeJson(tmp, lane.info) && tmp["format"] == "bambu" &&
        tmp["tray_uuid"].is<const char *>())
    {
      strlcpy(out, tmp["tray_uuid"], len);
      return true;
    }
  }
  strlcpy(out, lane.reported, len);
  return true;
}

// Held lanes are saved to flash, so a reboot doesn't forget a spool whose tag
// is sitting out of the reader's view
static void saveLane(int i)
{
  const Lane &lane = lanes[i];
  Preferences p;
  p.begin("lanes", false);
  char key[8];
  snprintf(key, sizeof(key), "l%d", i);
  if (!lane.reported[0])
    p.remove(key);
  else
  {
    JsonDocument d;
    d["uid"] = lane.reported;
    if (lane.info[0] && strcmp(lane.infoFor, lane.reported) == 0)
      d["info"] = serialized(lane.info);
    String s;
    serializeJson(d, s);
    p.putString(key, s);
  }
  p.end();
}

static void restoreLanes()
{
  Preferences p;
  p.begin("lanes", false);
  for (int i = 0; i < MAX_LANES; i++)
  {
    char key[8];
    snprintf(key, sizeof(key), "l%d", i);
    if (!cfg.laneEnabled[i] || !p.isKey(key))
      continue;
    JsonDocument d;
    if (deserializeJson(d, p.getString(key)) || !d["uid"].is<const char *>())
      continue;
    Lane &lane = lanes[i];
    strlcpy(lane.reported, d["uid"], sizeof(lane.reported));
    strlcpy(lane.infoFor, lane.reported, sizeof(lane.infoFor));
    if (d["info"].is<JsonObjectConst>())
      serializeJson(d["info"], lane.info, INFO_LEN);
    lane.infoDone = true;
    lane.known = true;
    logEvent("Lane %d: holding saved spool %s", i + 1, lane.reported);
    spoolSyncLaneChanged(i); // look it up in Spoolman again
  }
  p.end();
}

// The lane's published state or data changed: tell MQTT and Spoolman, save it
static void laneChanged(int i)
{
  saveLane(i);
  publishTagState(i);
  spoolSyncLaneChanged(i);
}

void rfidClearLane(int i)
{
  if (!laneActive(i))
    return;
  Lane &lane = lanes[i];
  if (lane.reported[0])
    logEvent("Lane %d: released spool %s", i + 1, lane.reported);
  lane.reported[0] = '\0';
  lane.known = true;
  lane.missCount = 0;
  laneChanged(i);
}

static void uidToHex(const MFRC522::Uid &uid, char *out, size_t len)
{
  size_t pos = 0;
  out[0] = '\0';
  for (byte i = 0; i < uid.size && pos + 2 < len; i++)
    pos += snprintf(out + pos, len - pos, "%02X", uid.uidByte[i]);
}

static const char *typeName(byte sak)
{
  return (const char *)MFRC522::PICC_GetTypeName(MFRC522::PICC_GetType(sak));
}

// Init one reader and check it answers. Logs the result unless quiet (used
// for background retries, so a missing reader doesn't flood the log).
static void probeReader(int i, bool quiet)
{
  Lane &lane = lanes[i];
  lane.lastProbe = millis();
  lane.reader.PCD_Init(cfg.ssPins[i], MFRC522::UNUSED_PIN); // RST tied high: soft reset
  lane.version = lane.reader.PCD_ReadRegister(MFRC522::VersionReg);
  // 0x00 / 0xFF means nothing answered: check wiring and power
  lane.present = lane.version != 0x00 && lane.version != 0xFF;
  // Max receive gain (48 dB, default 33 dB): helps small spool tags, and the
  // readers run at 3.0 V, which weakens the field a little
  lane.reader.PCD_SetAntennaGain(MFRC522::RxGain_max);
  lane.reader.PCD_AntennaOff(); // PCD_Init leaves the antenna on
  if (!quiet || lane.present)
    logEvent("Lane %d reader (SS GPIO %d): %s (version 0x%02X)", i + 1, cfg.ssPins[i],
             lane.present ? "found" : "NOT FOUND, will keep retrying", lane.version);
}

void setupRfid()
{
  if (activeCount() == 0)
    return;
  restoreLanes();
  for (int i = 0; i < MAX_LANES; i++)
    lanes[i].lastSeen = 0;
  SPI.begin(cfg.spiSck, cfg.spiMiso, cfg.spiMosi);
  for (int i = 0; i < MAX_LANES; i++)
    if (cfg.laneEnabled[i])
    {
      pinMode(cfg.ssPins[i], OUTPUT);
      digitalWrite(cfg.ssPins[i], HIGH); // deselect every reader before talking to any
    }
  for (int i = 0; i < MAX_LANES; i++)
    if (cfg.laneEnabled[i])
      probeReader(i, false);
}

// Power the field, look for a tag. Leaves the antenna ON when a tag was found
// so the caller can keep talking to it; call finishTag() when done.
static bool selectTag(MFRC522 &r)
{
  r.PCD_AntennaOn();
  delay(FIELD_SETTLE_MS);
  // The tag loses power whenever the antenna is off, so it always wakes as
  // "new" and IsNewCardPresent works as a plain presence check. A weakly
  // coupled tag often misses the first request, so try twice.
  for (int attempt = 0; attempt < 2; attempt++)
    if (r.PICC_IsNewCardPresent() && r.PICC_ReadCardSerial())
      return true;
  r.PCD_AntennaOff();
  return false;
}

static void finishTag(MFRC522 &r)
{
  r.PICC_HaltA();
  r.PCD_AntennaOff();
}

// Decode spool data from the selected tag into the lane's info. Bambu tags
// (MIFARE Classic 1K) and OpenTag3D stickers (NTAG) are recognised.
static void decodeTag(int i, const char *uid)
{
  Lane &lane = lanes[i];
  MFRC522 &r = lane.reader;
  MFRC522::PICC_Type type = MFRC522::PICC_GetType(r.uid.sak);
  JsonDocument doc;
  lane.infoTries++;

  if (type == MFRC522::PICC_TYPE_MIFARE_1K && r.uid.size == 4)
  {
    BambuTag b;
    if (bambuReadTag(r, b))
    {
      doc["format"] = "bambu";
      bambuToJson(b, doc.as<JsonObject>());
      logEvent("Lane %d: Bambu %s %s, tray %s", i + 1, b.detailedType, b.color, b.trayUuid);
    }
    else if (lane.infoTries >= DECODE_TRIES)
    {
      logEvent("Lane %d: tag %s isn't a Bambu tag (keys rejected)", i + 1, uid);
      lane.infoDone = true;
    }
  }
  else if (type == MFRC522::PICC_TYPE_MIFARE_UL)
  {
    String err;
    JsonObject o = doc.to<JsonObject>();
    Ot3dResult res = ot3dRead(r, o, err);
    if (res == OT3D_OK)
    {
      o["format"] = "opentag3d";
      logEvent("Lane %d: OpenTag3D %s %s %s", i + 1, o["manufacturer"] | "",
               o["material"] | "", o["color_1"] | "");
    }
    else
    {
      doc.clear();
      if (res == OT3D_NOT_OT3D || lane.infoTries >= DECODE_TRIES)
      {
        logEvent("Lane %d: tag %s has no spool data (%s)", i + 1, uid, err.c_str());
        lane.infoDone = true; // a blank sticker isn't an error; stop retrying
      }
    }
  }
  else
    lane.infoDone = true; // nothing to decode on other tag types

  if (doc.size())
  {
    if (measureJson(doc) < INFO_LEN)
      serializeJson(doc, lane.info, INFO_LEN);
    lane.infoDone = true;
  }
  // Already reported before decoding finished: send the decoded data (or the
  // fact that there is none) now, so MQTT and the Spoolman sync catch up
  if (lane.infoDone && lane.known && strcmp(lane.reported, uid) == 0)
    laneChanged(i);
}

// Lane rules, from tests with spools feeding through the buffer: a tag is
// often only in view for a moment as the spool turns, and can then sit in a
// blind spot indefinitely. So one good read assigns the lane (the UID read is
// checksummed), and missed reads never release it. A lane changes only when a
// different tag is read, it's cleared (web UI / unload), or optionally after
// cfg.laneReleaseMin minutes unseen.
static void updateLane(int i, bool found, const char *uid, unsigned long now)
{
  Lane &lane = lanes[i];
  if (found)
  {
    lane.missCount = 0;
    if (lane.known && strcmp(uid, lane.reported) == 0)
      return;
    if (lane.reported[0])
      logEvent("Lane %d: tag %s replaced %s", i + 1, uid, lane.reported);
    else
      logEvent("Lane %d: tag %s (%s)", i + 1, uid, typeName(lane.lastSak));
    strlcpy(lane.reported, uid, sizeof(lane.reported));
    lane.known = true;
    laneChanged(i);
    return;
  }
  if (!lane.known)
  {
    // Nothing held and nothing seen since boot: report empty after a few misses
    if (++lane.missCount >= MISS_LIMIT)
      rfidClearLane(i);
    return;
  }
  if (lane.reported[0] && cfg.laneReleaseMin &&
      now - lane.lastSeen > cfg.laneReleaseMin * 60000UL)
  {
    logEvent("Lane %d: tag unseen for %u min", i + 1, cfg.laneReleaseMin);
    rfidClearLane(i);
  }
}

// Poll one enabled reader per call, spread evenly across the cycle time
void updateRfid(unsigned long now)
{
  int active = activeCount();
  if (active == 0 || now - lastStep < cfg.rfidCycleMs / active)
    return;
  lastStep = now;
  int i = nextLane;
  for (int n = 0; n < MAX_LANES && !cfg.laneEnabled[i]; n++)
    i = (i + 1) % MAX_LANES;
  nextLane = (i + 1) % MAX_LANES;
  Lane &lane = lanes[i];
  if (!lane.present)
  {
    if (now - lane.lastProbe >= PROBE_RETRY_MS)
      probeReader(i, true);
    return;
  }

  char uid[UID_LEN] = "";
  lane.reads++;
  bool found = selectTag(lane.reader);
  if (found)
  {
    uidToHex(lane.reader.uid, uid, sizeof(uid));
    lane.hits++;
    lane.lastSeen = now;
    lane.lastSak = lane.reader.uid.sak;
    strlcpy(lane.lastUid, uid, sizeof(lane.lastUid));

    if (strcmp(uid, lane.infoFor) != 0) // a different tag: forget the old data
    {
      strlcpy(lane.infoFor, uid, sizeof(lane.infoFor));
      lane.info[0] = '\0';
      lane.infoTries = 0;
      lane.infoDone = false;
    }
    if (!lane.infoDone)
      decodeTag(i, uid); // while the tag is selected
    finishTag(lane.reader);
  }
  updateLane(i, found, uid, now);
}

void rfidStatusJson(JsonArray out)
{
  unsigned long now = millis();
  for (int i = 0; i < MAX_LANES; i++)
  {
    const Lane &lane = lanes[i];
    JsonObject o = out.add<JsonObject>();
    o["lane"] = i + 1;
    o["enabled"] = cfg.laneEnabled[i];
    o["ss_pin"] = cfg.ssPins[i];
    o["tool"] = cfg.laneTool[i];
    o["tool_loaded"] = toolLoaded(cfg.laneTool[i]);
    if (toolSensor(cfg.laneTool[i]) >= 0)
      o["tool_sensor"] = toolSensor(cfg.laneTool[i]);
    if (toolMismatch(cfg.laneTool[i]))
      o["tool_mismatch"] = true;
    if (!cfg.laneEnabled[i])
      continue;
    o["present"] = lane.present;
    char ver[5];
    snprintf(ver, sizeof(ver), "0x%02X", lane.version);
    o["version"] = ver;
    o["state"] = !lane.known ? "unknown" : (lane.reported[0] ? lane.reported : "empty");
    o["last_uid"] = lane.lastUid;
    if (lane.lastSeen)
    {
      o["last_seen_s"] = (now - lane.lastSeen) / 1000;
      char sak[5];
      snprintf(sak, sizeof(sak), "0x%02X", lane.lastSak);
      o["last_sak"] = sak;
      o["last_type"] = typeName(lane.lastSak);
    }
    o["reads"] = lane.reads;
    o["hits"] = lane.hits;
    o["tag_visible"] = lane.lastSeen && now - lane.lastSeen < VISIBLE_MS;
    if (!laneTagInfo(i, o["spool"].to<JsonObject>()))
      o.remove("spool");
    spoolSyncJson(i, o);
  }
}

// ── Tag dump for the diagnostics page ─────────────────────
static void addBlock(JsonArray blocks, int n, const byte *data, int len)
{
  char hex[3 * 16 + 1] = "", ascii[17] = "";
  for (int i = 0; i < len; i++)
  {
    snprintf(hex + i * 3, 4, "%02X ", data[i]);
    ascii[i] = (data[i] >= 0x20 && data[i] < 0x7F) ? data[i] : '.';
  }
  ascii[len] = '\0';
  hex[len * 3 - 1] = '\0';
  JsonObject b = blocks.add<JsonObject>();
  b["n"] = n;
  b["hex"] = hex;
  b["ascii"] = ascii;
}

// Wake a tag that dropped out after a failed command and select it again
static bool reselect(MFRC522 &r, const MFRC522::Uid &uid)
{
  byte atqa[2];
  byte size = sizeof(atqa);
  r.PICC_WakeupA(atqa, &size);
  MFRC522::Uid again = uid;
  return r.PICC_Select(&again) == MFRC522::STATUS_OK;
}

// NTAG / MIFARE Ultralight: no keys, 4-byte pages, read 4 pages at a time
// until the tag stops answering (past the end of its memory). A failed read
// is retried, since a weakly coupled tag can drop a read mid-dump; only
// repeated failures mean the end of memory.
static void dumpUltralight(MFRC522 &r, JsonArray blocks)
{
  MFRC522::Uid uid = r.uid;
  for (int page = 0; page < 256; page += 4)
  {
    byte buf[18];
    bool ok = false;
    for (int attempt = 0; attempt < 3 && !ok; attempt++)
    {
      byte size = sizeof(buf);
      ok = r.MIFARE_Read(page, buf, &size) == MFRC522::STATUS_OK;
      if (!ok)
        reselect(r, uid);
    }
    if (!ok)
      break;
    for (int p = 0; p < 4; p++)
      addBlock(blocks, page + p, buf + p * 4, 4);
  }
}

// MIFARE Classic: every sector needs a key. Try the Bambu key derived from the
// UID (when given), then the factory default key. A failed attempt drops the
// tag, so wake and re-select it before the next try.
static bool authSector(MFRC522 &r, int s, const byte *key6, MFRC522::Uid &uid)
{
  MFRC522::MIFARE_Key key;
  memcpy(key.keyByte, key6, 6);
  if (r.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, s * 4 + 3, &key, &uid) ==
      MFRC522::STATUS_OK)
    return true;
  r.PCD_StopCrypto1();
  reselect(r, uid);
  return false;
}

static void dumpClassic(MFRC522 &r, int sectors, JsonArray blocks, int &locked,
                        byte (*bambuKeys)[6])
{
  static const byte DEFAULT_KEY[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  MFRC522::Uid uid = r.uid;
  for (int s = 0; s < sectors; s++)
  {
    int first = s * 4;
    bool ok = (bambuKeys && authSector(r, s, bambuKeys[s], uid)) ||
              authSector(r, s, DEFAULT_KEY, uid);
    if (!ok)
    {
      locked++;
      JsonObject b = blocks.add<JsonObject>();
      b["n"] = first;
      b["locked"] = true;
      b["sector"] = s;
      continue;
    }
    for (int b = 0; b < 4; b++)
    {
      byte buf[18];
      byte size = sizeof(buf);
      if (r.MIFARE_Read(first + b, buf, &size) == MFRC522::STATUS_OK)
        addBlock(blocks, first + b, buf, 16);
    }
  }
  r.PCD_StopCrypto1();
}

// Common checks before talking to a lane's tag from the web UI
static bool laneTagReady(int i, JsonDocument &out)
{
  out["lane"] = i + 1;
  if (!laneActive(i))
  {
    out["error"] = "Lane is disabled";
    return false;
  }
  if (!lanes[i].present)
  {
    out["error"] = "Reader not found; check wiring and power";
    return false;
  }
  if (!selectTag(lanes[i].reader))
  {
    out["error"] = "No tag in range";
    return false;
  }
  return true;
}

// Reader hardware check for one lane, for the Reader check page. In order:
//  1. re-init the chip, then read back the registers init wrote (did the
//     writes stick?)
//  2. 200 reads of the version register (are reads reliable?)
//  3. 64 write/read-back patterns on a scratch register (are writes reliable?)
//  4. 20 raw tag wake-ups with the antenna on, with the chip's error flags
//  5. the chip's built-in self-test
// drive 0-3 sets the ESP32's SCK/MOSI/SS pin strength first (-1 = leave it).
void rfidDiagJson(int i, JsonDocument &out, int drive)
{
  out["lane"] = i + 1;
  if (i < 0 || i >= MAX_LANES || !laneActive(i))
  {
    out["error"] = "Lane is disabled";
    return;
  }
  out["ss_pin"] = cfg.ssPins[i];
  out["spi_clock_hz"] = MFRC522_SPICLOCK;
  if (drive >= 0 && drive <= 3)
  {
    gpio_set_drive_capability((gpio_num_t)cfg.spiSck, (gpio_drive_cap_t)drive);
    gpio_set_drive_capability((gpio_num_t)cfg.spiMosi, (gpio_drive_cap_t)drive);
    for (int n = 0; n < MAX_LANES; n++)
      if (cfg.laneEnabled[n])
        gpio_set_drive_capability((gpio_num_t)cfg.ssPins[n], (gpio_drive_cap_t)drive);
  }
  gpio_drive_cap_t cap;
  gpio_get_drive_capability((gpio_num_t)cfg.spiSck, &cap);
  out["pin_drive"] = (int)cap;

  MFRC522 &r = lanes[i].reader;
  probeReader(i, true);
  JsonObject regs = out["registers"].to<JsonObject>();
  regs["version"] = r.PCD_ReadRegister(MFRC522::VersionReg);
  regs["rf_gain"] = r.PCD_ReadRegister(MFRC522::RFCfgReg);
  regs["tx_ask"] = r.PCD_ReadRegister(MFRC522::TxASKReg);
  regs["mode"] = r.PCD_ReadRegister(MFRC522::ModeReg);
  regs["tx_control"] = r.PCD_ReadRegister(MFRC522::TxControlReg);

  byte version = regs["version"];
  int good = 0;
  for (int n = 0; n < 200; n++)
    if (r.PCD_ReadRegister(MFRC522::VersionReg) == version)
      good++;
  out["read_ok"] = good;
  out["read_total"] = 200;

  const byte pats[] = {0x00, 0xFF, 0x55, 0xAA, 0x0F, 0xF0, 0x81, 0x7E};
  byte saved = r.PCD_ReadRegister(MFRC522::ModWidthReg);
  int wok = 0, patOk[8] = {0};
  byte patLast[8] = {0};
  for (int n = 0; n < 64; n++)
  {
    byte pat = pats[n % 8];
    r.PCD_WriteRegister(MFRC522::ModWidthReg, pat);
    byte got = r.PCD_ReadRegister(MFRC522::ModWidthReg);
    if (got == pat)
    {
      wok++;
      patOk[n % 8]++;
    }
    else
      patLast[n % 8] = got;
  }
  r.PCD_WriteRegister(MFRC522::ModWidthReg, saved);
  // Per pattern: random failures mean noise; the same patterns failing every
  // time mean something deterministic
  JsonArray wpat = out["write_patterns"].to<JsonArray>();
  for (int n = 0; n < 8; n++)
  {
    JsonObject f = wpat.add<JsonObject>();
    f["wrote"] = pats[n];
    f["ok"] = patOk[n];
    f["of"] = 8;
    if (patOk[n] < 8)
      f["read"] = patLast[n];
  }
  out["write_ok"] = wok;
  out["write_total"] = 64;

  r.PCD_AntennaOn();
  delay(FIELD_SETTLE_MS);
  out["tx_control_antenna_on"] = r.PCD_ReadRegister(MFRC522::TxControlReg);
  JsonArray reqs = out["requests"].to<JsonArray>();
  for (int n = 0; n < 20; n++)
  {
    byte atqa[2];
    byte len = sizeof(atqa);
    MFRC522::StatusCode st = r.PICC_WakeupA(atqa, &len);
    String name = MFRC522::GetStatusCodeName(st);
    byte err = r.PCD_ReadRegister(MFRC522::ErrorReg);
    JsonObject match;
    for (JsonObject q : reqs)
      if (q["status"] == name && q["error_reg"] == err)
        match = q;
    if (match.isNull())
    {
      match = reqs.add<JsonObject>();
      match["status"] = name;
      match["ok"] = st == MFRC522::STATUS_OK;
      match["error_reg"] = err;
      match["count"] = 0;
    }
    match["count"] = (int)match["count"] + 1;
    r.PICC_HaltA();
    delay(5);
  }
  r.PCD_AntennaOff();

  out["self_test_passed"] = r.PCD_PerformSelfTest();
  probeReader(i, true); // the self-test leaves the chip reset: set it up again
}

void rfidDumpJson(int i, JsonDocument &out)
{
  if (!laneTagReady(i, out))
    return;
  MFRC522 &r = lanes[i].reader;
  char uid[UID_LEN];
  uidToHex(r.uid, uid, sizeof(uid));
  MFRC522::PICC_Type type = MFRC522::PICC_GetType(r.uid.sak);
  char sak[5];
  snprintf(sak, sizeof(sak), "0x%02X", r.uid.sak);
  out["uid"] = uid;
  out["uid_bytes"] = r.uid.size;
  out["sak"] = sak;
  out["type"] = typeName(r.uid.sak);
  JsonArray blocks = out["blocks"].to<JsonArray>();

  switch (type)
  {
  case MFRC522::PICC_TYPE_MIFARE_UL:
  {
    out["block_size"] = 4;
    String err;
    MFRC522::Uid sel = r.uid;
    JsonObject spool = out["spool"].to<JsonObject>();
    if (ot3dRead(r, spool, err) == OT3D_OK)
      spool["format"] = "opentag3d";
    else
    {
      out.remove("spool");
      out["note"] = err;
    }
    reselect(r, sel);
    dumpUltralight(r, blocks);
    break;
  }
  case MFRC522::PICC_TYPE_MIFARE_MINI:
  case MFRC522::PICC_TYPE_MIFARE_1K:
  case MFRC522::PICC_TYPE_MIFARE_4K:
  {
    out["block_size"] = 16;
    int locked = 0;
    int sectors = type == MFRC522::PICC_TYPE_MIFARE_MINI ? 5 : 16; // 4K: first 16 only
    byte keys[16][6];
    bool tryBambu = type == MFRC522::PICC_TYPE_MIFARE_1K && r.uid.size == 4;
    if (tryBambu)
    {
      BambuTag bambu;
      MFRC522::Uid sel = r.uid;
      if (bambuReadTag(r, bambu))
      {
        JsonObject spool = out["spool"].to<JsonObject>();
        spool["format"] = "bambu";
        bambuToJson(bambu, spool);
      }
      reselect(r, sel);
      bambuDeriveKeys(r.uid.uidByte, keys);
    }
    dumpClassic(r, sectors, blocks, locked, tryBambu ? keys : nullptr);
    if (locked)
      out["note"] = String(locked) + " of " + sectors +
                    " sectors are locked (neither the Bambu key nor the default key "
                    "opened them).";
    break;
  }
  default:
    out["note"] = "Tag type not supported for memory reads; UID only.";
  }

  finishTag(r);
  logEvent("Lane %d: tag dump %s (%s), %d blocks", i + 1, uid, typeName(r.uid.sak),
           (int)blocks.size());
}

bool rfidWriteTag(int i, JsonObjectConst fields, JsonDocument &out)
{
  if (!laneTagReady(i, out))
    return false;
  Lane &lane = lanes[i];
  MFRC522 &r = lane.reader;
  char uid[UID_LEN];
  uidToHex(r.uid, uid, sizeof(uid));
  out["uid"] = uid;

  if (MFRC522::PICC_GetType(r.uid.sak) != MFRC522::PICC_TYPE_MIFARE_UL)
  {
    out["error"] = String("This is a ") + typeName(r.uid.sak) +
                   " tag. OpenTag3D needs an NTAG215 or NTAG216 sticker" +
                   (r.uid.size == 4 ? " (Bambu tags are signed and can't be rewritten)." : ".");
    finishTag(r);
    return false;
  }
  String err;
  bool ok = ot3dWrite(r, fields, err);
  finishTag(r);
  if (!ok)
  {
    out["error"] = err;
    logEvent("Lane %d: tag write failed: %s", i + 1, err.c_str());
    return false;
  }
  logEvent("Lane %d: wrote OpenTag3D data to tag %s (verified)", i + 1, uid);
  // Re-decode on the next poll so the lane reports the new data
  lane.infoFor[0] = '\0';
  return true;
}
