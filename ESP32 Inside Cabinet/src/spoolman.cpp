#include "spoolman.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include "settings.h"
#include "rfid.h"
#include "diag.h"

#define SYNC_STEP_MS 1000   // at most one Spoolman sync per second
#define SYNC_RETRY_MS 60000 // retry after Spoolman was unreachable

static String spoolUrl(int id) { return String(cfg.spoolmanUrl) + "/api/v1/spool/" + id; }
static String hex6(const char *color);

static bool getSpool(int id, JsonDocument &spool, String &err)
{
  if (!cfg.spoolmanUrl[0])
  {
    err = "Set the Spoolman URL in Settings first";
    return false;
  }
  HTTPClient http;
  http.setTimeout(5000);
  if (!http.begin(spoolUrl(id)))
  {
    err = "Bad Spoolman URL";
    return false;
  }
  int code = http.GET();
  if (code != 200)
  {
    err = code == 404 ? String("Spool #") + id + " not found in Spoolman"
                      : String("Spoolman request failed (") +
                            (code < 0 ? http.errorToString(code) : String("HTTP ") + code) + ")";
    http.end();
    return false;
  }
  DeserializationError e = deserializeJson(spool, http.getStream());
  http.end();
  if (e)
  {
    err = String("Couldn't parse Spoolman's reply: ") + e.c_str();
    return false;
  }
  return true;
}

// Fit text into an OpenTag3D field of maxLen bytes, cutting at a character
// boundary so UTF-8 isn't split
static String fit(const char *s, size_t maxLen)
{
  String out = s ? s : "";
  while (out.length() > maxLen)
  {
    out.remove(out.length() - 1);
    while (out.length() && (out[out.length() - 1] & 0xC0) == 0x80)
      out.remove(out.length() - 1);
  }
  return out;
}

bool spoolmanFetch(int spoolId, JsonDocument &out, String &err)
{
  JsonDocument spool;
  if (!getSpool(spoolId, spool, err))
    return false;
  JsonObjectConst fil = spool["filament"];
  JsonObjectConst vendor = fil["vendor"];
  JsonObject f = out["fields"].to<JsonObject>();

  // Material: OpenTag3D allows 5 bytes for the base and 5 for a modifier,
  // so "PETG-CF" or "PLA Matte" splits into base + modifier
  String material = fil["material"] | "";
  int split = material.indexOf('-');
  if (split < 0)
    split = material.indexOf(' ');
  if (material.length() > 5 && split > 0)
  {
    f["material"] = fit(material.substring(0, split).c_str(), 5);
    f["material_mod"] = fit(material.substring(split + 1).c_str(), 5);
  }
  else if (material.length())
    f["material"] = fit(material.c_str(), 5);

  if (vendor["name"].is<const char *>())
    f["manufacturer"] = fit(vendor["name"], 16);
  if (fil["name"].is<const char *>())
    f["color_name"] = fit(fil["name"], 32);

  // Colors: color_hex (6 or 8 hex digits) or multi_color_hexes "aaaaaa,bbbbbb"
  String multi = fil["multi_color_hexes"] | "";
  if (multi.length())
  {
    int slot = 1, start = 0;
    while (slot <= 4 && start < (int)multi.length())
    {
      int comma = multi.indexOf(',', start);
      String hex = multi.substring(start, comma < 0 ? multi.length() : comma);
      hex.trim();
      if (hex.length())
        f[String("color_") + slot++] = "#" + hex;
      if (comma < 0)
        break;
      start = comma + 1;
    }
  }
  else if (fil["color_hex"].is<const char *>())
    f["color_1"] = String("#") + fil["color_hex"].as<const char *>();

  if (!fil["diameter"].isNull())
    f["diameter"] = fil["diameter"];
  if (!fil["density"].isNull())
    f["density"] = fil["density"];
  if (!spool["initial_weight"].isNull())
    f["weight"] = spool["initial_weight"];
  else if (!fil["weight"].isNull())
    f["weight"] = fil["weight"];
  if (!spool["spool_weight"].isNull())
    f["empty_spool_weight"] = spool["spool_weight"];
  else if (!fil["spool_weight"].isNull())
    f["empty_spool_weight"] = fil["spool_weight"];
  else if (!vendor["empty_spool_weight"].isNull())
    f["empty_spool_weight"] = vendor["empty_spool_weight"];
  if (!fil["settings_extruder_temp"].isNull())
    f["print_temp"] = fil["settings_extruder_temp"];
  if (!fil["settings_bed_temp"].isNull())
    f["bed_temp"] = fil["settings_bed_temp"];
  if (fil["article_number"].is<const char *>())
    f["sku"] = fit(fil["article_number"], 16);
  if (spool["lot_nr"].is<const char *>())
    f["serial"] = fit(spool["lot_nr"], 32);

  JsonObject s = out["spool"].to<JsonObject>();
  s["id"] = spoolId;
  s["name"] = fil["name"];
  s["vendor"] = vendor["name"];
  s["material"] = fil["material"];
  s["remaining_weight"] = spool["remaining_weight"];
  return true;
}

bool spoolmanSetTag(int spoolId, const char *tagId, String &err)
{
  // PATCH replaces the whole extra map, so start from the spool's current one
  // to keep other fields (e.g. OpenSpoolMan's active_tray)
  JsonDocument spool;
  if (!getSpool(spoolId, spool, err))
    return false;
  JsonDocument body;
  JsonObject extra = body["extra"].to<JsonObject>();
  for (JsonPairConst kv : spool["extra"].as<JsonObjectConst>())
    extra[kv.key()] = kv.value();
  extra["tag"] = String("\"") + tagId + "\""; // Spoolman extra values are JSON-encoded
  String payload;
  serializeJson(body, payload);

  HTTPClient http;
  http.setTimeout(5000);
  http.begin(spoolUrl(spoolId));
  http.addHeader("Content-Type", "application/json");
  int code = http.PATCH(payload);
  String reply = code > 0 ? http.getString() : "";
  http.end();
  if (code == 200)
    return true;
  err = code < 0 ? String("Spoolman request failed (") + http.errorToString(code) + ")"
                 : String("Spoolman rejected it (HTTP ") + code + ")";
  if (reply.indexOf("tag") >= 0)
    err += ". Add a spool extra field with key \"tag\" (type Text) in Spoolman's settings";
  return false;
}

// ── Lane sync ─────────────────────────────────────────────
struct LaneSync
{
  bool pending = false;
  unsigned long retryAt = 0;
  char key[48] = ""; // spool key the lane was last synced for
  int spoolId = 0;   // matching Spoolman spool, 0 = none
  String name;       // "Vendor Name (material)" for display
  float remaining = -1;
  int temp = 0;     // filament's nozzle temp in Spoolman, 0 = not set
  int bed = 0;      // filament's bed temp in Spoolman, 0 = not set
  String material;
  String color;     // "RRGGBB" from Spoolman, "" = not set
  String vendor, filName; // manufacturer and filament name, separately
  int filamentId = 0;     // Spoolman filament record
  float initial = -1;     // spool's filament weight when new
  String orcaId;          // filament extra "orca_filament_id" (RFC 0001 §6)
  String error; // last problem, shown in the UI
  bool force = false; // re-check Spoolman even though the key is unchanged
};
static LaneSync syncs[MAX_LANES];

void spoolSyncLaneChanged(int lane)
{
  if (lane < 0 || lane >= MAX_LANES)
    return;
  syncs[lane].pending = true;
  syncs[lane].retryAt = 0;
}

static String laneLocation(int lane)
{
  return String(cfg.spoolmanLocation) + " Lane " + (lane + 1);
}

static bool patchSpool(int id, JsonDocument &body, String &err)
{
  String payload;
  serializeJson(body, payload);
  HTTPClient http;
  http.setTimeout(5000);
  http.begin(spoolUrl(id));
  http.addHeader("Content-Type", "application/json");
  int code = http.PATCH(payload);
  http.end();
  if (code == 200)
    return true;
  err = code < 0 ? String("Spoolman unreachable (") + http.errorToString(code) + ")"
                 : String("Spoolman rejected the update (HTTP ") + code + ")";
  return false;
}

static bool setLocation(int id, const String &location, String &err)
{
  JsonDocument body;
  body["location"] = location;
  return patchSpool(id, body, err);
}

// Spoolman stores extra values JSON-encoded: the tag ABC arrives as "ABC" in quotes
static String extraString(JsonVariantConst v)
{
  String s = v | "";
  if (s.length() >= 2 && s[0] == '"' && s[s.length() - 1] == '"')
    s = s.substring(1, s.length() - 1);
  return s;
}

struct SpoolMatch
{
  int id = 0;
  String name, location, material;
  float remaining = -1;
  int temp = 0, bed = 0;
  String color, vendor, filName, orcaId;
  int filamentId = 0;
  float initial = -1;
};

// Find the (non-archived) spool whose "tag" extra field equals key. Spoolman
// can't filter on extra fields, so list spools and keep only what we need.
static bool findSpoolByTag(const char *key, SpoolMatch &m, String &err)
{
  HTTPClient http;
  http.setTimeout(8000);
  http.begin(String(cfg.spoolmanUrl) + "/api/v1/spool");
  int code = http.GET();
  if (code != 200)
  {
    err = code < 0 ? String("Spoolman unreachable (") + http.errorToString(code) + ")"
                   : String("Spoolman spool list failed (HTTP ") + code + ")";
    http.end();
    return false;
  }
  String body = http.getString(); // large lists land in PSRAM
  http.end();

  JsonDocument filter;
  JsonObject f = filter[0].to<JsonObject>();
  f["id"] = true;
  f["location"] = true;
  f["remaining_weight"] = true;
  f["extra"]["tag"] = true;
  f["filament"]["name"] = true;
  f["filament"]["material"] = true;
  f["filament"]["vendor"]["name"] = true;
  f["filament"]["settings_extruder_temp"] = true;
  f["filament"]["settings_bed_temp"] = true;
  f["filament"]["color_hex"] = true;
  f["filament"]["multi_color_hexes"] = true;
  f["filament"]["id"] = true;
  f["filament"]["weight"] = true;
  f["filament"]["extra"]["orca_filament_id"] = true;
  f["initial_weight"] = true;
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (e)
  {
    err = String("Couldn't parse Spoolman's spool list: ") + e.c_str();
    return false;
  }
  for (JsonObjectConst s : doc.as<JsonArrayConst>())
  {
    if (!extraString(s["extra"]["tag"]).equalsIgnoreCase(key))
      continue;
    m.id = s["id"] | 0;
    m.location = s["location"] | "";
    m.remaining = s["remaining_weight"] | -1.0f;
    JsonObjectConst fil = s["filament"];
    m.temp = fil["settings_extruder_temp"] | 0;
    m.bed = fil["settings_bed_temp"] | 0;
    m.color = fil["color_hex"] | "";
    if (!m.color.length())
      m.color = String(fil["multi_color_hexes"] | "").substring(0, 6);
    m.material = fil["material"] | "";
    m.vendor = fil["vendor"]["name"] | "";
    m.filName = fil["name"] | "";
    m.filamentId = fil["id"] | 0;
    m.initial = s["initial_weight"] | (fil["weight"] | -1.0f);
    m.orcaId = extraString(fil["extra"]["orca_filament_id"]);
    m.name = String(fil["vendor"]["name"] | "") + " " + (fil["name"] | "");
    m.name.trim();
    if (fil["material"].is<const char *>())
      m.name += String(" (") + fil["material"].as<const char *>() + ")";
    return true;
  }
  return true; // listed fine, no match (m.id == 0)
}

// A spool left the lane: move it to the configured away location (e.g.
// "StorOn Shelves"), or clear it, but only if it still points at this lane
static bool releaseSpool(int lane, int id, String &err)
{
  if (!cfg.spoolmanLocation[0] || id <= 0)
    return true;
  JsonDocument spool;
  if (!getSpool(id, spool, err))
    return err.indexOf("not found") >= 0; // deleted spool: nothing to clear
  if (laneLocation(lane) != (spool["location"] | ""))
    return true; // moved elsewhere since; leave it alone
  return setLocation(id, cfg.spoolmanAwayLocation, err);
}

static void syncLane(int i, unsigned long now)
{
  LaneSync &s = syncs[i];
  String err;
  const char *uid = laneReported(i);

  if (!laneActive(i) || !laneKnown(i) || !uid[0])
  {
    // Lane emptied (or disabled): release the spool that was there
    if (s.spoolId > 0 && !releaseSpool(i, s.spoolId, err))
    {
      s.error = err;
      s.retryAt = now + SYNC_RETRY_MS;
      return;
    }
    if (s.spoolId > 0)
      logEvent("Lane %d: Spoolman spool #%d left the lane%s%s", i + 1, s.spoolId,
               cfg.spoolmanAwayLocation[0] ? ", now at " : "", cfg.spoolmanAwayLocation);
    s = LaneSync();
    publishTagState(i);
    return;
  }

  char key[48];
  if (!laneSpoolKey(i, key, sizeof(key)))
    return; // still decoding; stays pending
  bool same = strcmp(key, s.key) == 0;
  if (same && s.error.isEmpty() && !s.force)
  {
    s.pending = false; // nothing new
    return;
  }
  s.force = false;
  // A different spool replaced the last one without the lane emptying
  if (s.spoolId > 0 && !same)
    releaseSpool(i, s.spoolId, err);

  SpoolMatch m;
  if (!findSpoolByTag(key, m, err))
  {
    s.error = err;
    s.retryAt = now + SYNC_RETRY_MS;
    return;
  }
  strlcpy(s.key, key, sizeof(s.key));
  s.pending = false;
  if (!m.id)
  {
    s.spoolId = 0;
    s.name = "";
    s.remaining = -1;
    s.error = "Not linked to a Spoolman spool";
    logEvent("Lane %d: tag %s isn't linked to a Spoolman spool", i + 1, key);
    publishTagState(i);
    return;
  }
  if (cfg.spoolmanLocation[0] && m.location != laneLocation(i) &&
      !setLocation(m.id, laneLocation(i), err))
  {
    s.error = err;
    s.pending = true;
    s.retryAt = now + SYNC_RETRY_MS;
    s.key[0] = '\0';
    return;
  }
  s.spoolId = m.id;
  s.name = m.name;
  s.remaining = m.remaining;
  s.temp = m.temp;
  s.bed = m.bed;
  s.color = m.color;
  s.material = m.material;
  s.vendor = m.vendor;
  s.filName = m.filName;
  s.filamentId = m.filamentId;
  s.initial = m.initial;
  s.orcaId = m.orcaId;
  s.error = "";
  logEvent("Lane %d: Spoolman spool #%d %s", i + 1, m.id, m.name.c_str());
  publishTagState(i);
}

void updateSpoolSync(unsigned long now)
{
  static unsigned long lastStep = 0;
  if (!cfg.spoolmanUrl[0] || WiFi.status() != WL_CONNECTED || now - lastStep < SYNC_STEP_MS)
    return;
  lastStep = now;
  // One lane per step, so a slow Spoolman never stalls the loop for long
  static int next = 0;
  for (int n = 0; n < MAX_LANES; n++)
  {
    int i = (next + n) % MAX_LANES;
    if (syncs[i].pending && (long)(now - syncs[i].retryAt) >= 0)
    {
      next = (i + 1) % MAX_LANES;
      syncLane(i, now);
      return;
    }
  }
}

void spoolSyncJson(int lane, JsonObject out)
{
  const LaneSync &s = syncs[lane];
  if (!cfg.spoolmanUrl[0])
    return;
  if (s.spoolId > 0)
  {
    out["spoolman_id"] = s.spoolId;
    out["spoolman_name"] = s.name;
    if (s.remaining >= 0)
      out["spoolman_remaining_g"] = (int)(s.remaining + 0.5f);
    if (cfg.spoolmanLocation[0])
      out["spoolman_location"] = laneLocation(lane);
  }
  if (s.error.length())
    out["spoolman_error"] = s.error;
  if (s.pending)
    out["spoolman_pending"] = true;
  String material;
  int temp = laneNozzleTemp(lane, material);
  if (temp)
    out["nozzle_temp"] = temp;
  if (material.length())
    out["material"] = material;
  char key[48];
  if (laneActive(lane) && laneSpoolKey(lane, key, sizeof(key)))
    out["spool_key"] = key;
}

// Re-check every lane against Spoolman (a spool's tag changed, and it may
// have been in another lane)
void spoolSyncRefreshAll()
{
  for (int i = 0; i < MAX_LANES; i++)
    if (laneActive(i))
    {
      syncs[i].force = true;
      spoolSyncLaneChanged(i);
    }
}

// The old spool of a moved tag: take the tag off it (the extra field is set
// to empty, which works whether PATCH merges or replaces extra), move it out
// of the lane location, and archive it if asked (a used-up roll)
static bool detachOld(int lane, int id, bool archive, String &err)
{
  JsonDocument spool;
  if (!getSpool(id, spool, err))
    return false;
  JsonDocument body;
  JsonObject extra = body["extra"].to<JsonObject>();
  for (JsonPairConst kv : spool["extra"].as<JsonObjectConst>())
    extra[kv.key()] = kv.value();
  extra["tag"] = "\"\"";
  if (archive)
    body["archived"] = true;
  if (cfg.spoolmanLocation[0] && laneLocation(lane) == (spool["location"] | ""))
    body["location"] = cfg.spoolmanAwayLocation;
  if (!patchSpool(id, body, err))
  {
    err = String("Couldn't update the old spool #") + id + ": " + err;
    return false;
  }
  if (syncs[lane].spoolId == id)
    syncs[lane].spoolId = 0; // already moved out; don't release it again
  return true;
}

// Nozzle temperature and material for a lane's spool: Spoolman's filament
// first, else what the tag says
int laneNozzleTemp(int lane, String &material)
{
  material = "";
  if (lane < 0 || lane >= MAX_LANES || !laneActive(lane) || !laneReported(lane)[0])
    return 0;
  const LaneSync &s = syncs[lane];
  material = s.material;
  int temp = s.spoolId > 0 ? s.temp : 0;
  JsonDocument doc;
  JsonObject info = doc.to<JsonObject>();
  if (laneTagInfo(lane, info))
  {
    if (!material.length())
      material = info["format"] == "bambu" ? String(info["type"] | "") : String(info["material"] | "");
    if (!temp && info["format"] == "bambu")
    {
      int lo = info["hotend_min_c"] | 0, hi = info["hotend_max_c"] | 0;
      if (lo && hi)
        temp = ((lo + hi) / 2 + 2) / 5 * 5;
    }
    else if (!temp)
      temp = info["print_temp"] | 0;
  }
  return temp;
}

void laneFilamentJson(int lane, JsonObject out)
{
  if (lane < 0 || lane >= MAX_LANES || !laneActive(lane) || !laneReported(lane)[0])
    return;
  const LaneSync &s = syncs[lane];
  String material, type, color, vendor, name;
  int nozzle = 0, bed = 0;
  if (s.spoolId > 0)
  {
    out["spool_id"] = s.spoolId;
    if (s.filamentId > 0)
      out["spoolman_filament_id"] = s.filamentId;
    if (s.remaining >= 0)
      out["remaining_weight_g"] = (int)(s.remaining + 0.5f);
    if (s.initial > 0)
      out["total_weight_g"] = (int)(s.initial + 0.5f);
    if (s.orcaId.length())
      out["filament_id"] = s.orcaId; // the user's own Orca filament id, if they set one
    material = s.material;
    color = s.color;
    vendor = s.vendor;
    name = s.filName;
    nozzle = s.temp;
    bed = s.bed;
  }
  JsonDocument doc;
  JsonObject info = doc.to<JsonObject>();
  if (laneTagInfo(lane, info))
  {
    bool bambu = info["format"] == "bambu";
    String tagType = bambu ? String(info["detailed_type"] | "") : String(info["material"] | "");
    if (!bambu && strlen(info["material_mod"] | ""))
      tagType += String("-") + info["material_mod"].as<const char *>();
    String tagBase = bambu ? String(info["type"] | "") : String(info["material"] | "");
    if (!material.length())
      material = tagBase;
    type = tagType;
    if (!color.length())
      color = hex6(info[bambu ? "color" : "color_1"]);
    if (!vendor.length())
      vendor = bambu ? "Bambu Lab" : String(info["manufacturer"] | "");
    if (!name.length())
      name = bambu ? String(info["detailed_type"] | "") : String(info["color_name"] | "");
    if (!nozzle && bambu)
    {
      int lo = info["hotend_min_c"] | 0, hi = info["hotend_max_c"] | 0;
      if (lo && hi)
        nozzle = ((lo + hi) / 2 + 2) / 5 * 5;
    }
    else if (!nozzle)
      nozzle = info["print_temp"] | 0;
    if (!bed)
      bed = info[bambu ? "bed_temp_c" : "bed_temp"] | 0;
  }
  if (!type.length())
    type = material;
  if (material.length())
    out["material"] = material; // what the slicer matches presets on
  if (type.length())
    out["filament_type"] = type; // the precise type ("PETG HF")
  if (color.length() == 6)
    out["color"] = "#" + color;
  if (vendor.length())
    out["vendor_name"] = vendor;
  if (name.length())
    out["name"] = name;
  if (nozzle)
    out["nozzle_temp"] = nozzle;
  if (bed)
    out["bed_temp"] = bed;
}

bool spoolSyncLink(int lane, int spoolId, bool move, bool archiveOld, String &err)
{
  if (!cfg.spoolmanUrl[0])
  {
    err = "Set the Spoolman URL in Settings first";
    return false;
  }
  char key[48];
  if (!laneActive(lane) || !laneSpoolKey(lane, key, sizeof(key)))
  {
    err = "No settled tag in this lane yet";
    return false;
  }
  // One tag, one spool: another spool with this tag is refused, or moved
  SpoolMatch m;
  if (!findSpoolByTag(key, m, err))
    return false;
  int old = m.id != spoolId ? m.id : 0;
  if (old && !move)
  {
    err = String("This tag is already linked to Spoolman spool #") + old;
    return false;
  }
  if (!spoolmanSetTag(spoolId, key, err))
    return false;
  bool ok = !old || detachOld(lane, old, archiveOld, err);
  if (old && ok)
    logEvent("Lane %d: tag %s moved from Spoolman spool #%d to #%d%s", lane + 1, key, old, spoolId,
             archiveOld ? " (old spool archived)" : "");
  else
    logEvent("Lane %d: linked tag %s to Spoolman spool #%d", lane + 1, key, spoolId);
  spoolSyncRefreshAll();
  return ok;
}

// ── Create from tag ───────────────────────────────────────
static int httpCall(const char *method, const String &url, const String &body, String &resp)
{
  HTTPClient http;
  http.setTimeout(8000);
  http.begin(url);
  if (body.length())
    http.addHeader("Content-Type", "application/json");
  int code = http.sendRequest(method, body);
  resp = code > 0 ? http.getString() : http.errorToString(code);
  http.end();
  return code;
}

static String apiErr(const char *what, int code, const String &resp)
{
  String e = code < 0 ? String("Spoolman unreachable (") + resp + ")"
                      : String("Spoolman couldn't ") + what + " (HTTP " + code + ")";
  if (code > 0 && resp.indexOf("tag") >= 0)
    e += ". Add a spool extra field with key \"tag\" (type Text) in Spoolman's settings";
  return e;
}

// Typical densities (g/cm3) when a tag doesn't carry one (Bambu tags don't)
static float defaultDensity(const String &material)
{
  String m = material;
  m.toUpperCase();
  struct
  {
    const char *prefix;
    float density;
  } table[] = {{"PETG", 1.27f}, {"PET", 1.27f}, {"PLA", 1.24f}, {"ABS", 1.04f}, {"ASA", 1.07f},
               {"TPU", 1.21f},  {"PC", 1.20f},  {"PA", 1.14f},  {"PVA", 1.23f}, {"HIPS", 1.04f}};
  for (auto &t : table)
    if (m.startsWith(t.prefix))
      return t.density;
  return 1.24f;
}

static String hex6(const char *color) // "#RRGGBB(AA)" -> "RRGGBB"
{
  String s = color ? color : "";
  if (s.startsWith("#"))
    s = s.substring(1);
  s = s.substring(0, 6);
  s.toUpperCase();
  return s;
}

// Create the spool for a known filament, already tagged and located, then
// move the tag off the old spool if there was one
static bool createSpool(int lane, const char *key, int filamentId, bool bambu, JsonObjectConst info,
                        JsonObjectConst opts, int oldId, bool dryRun, JsonDocument &out, String &err)
{
  JsonObject plan = out["plan"].is<JsonObject>() ? out["plan"].as<JsonObject>() : out["plan"].to<JsonObject>();
  plan["tag"] = key;
  if (filamentId)
    plan["filament_id"] = filamentId;
  if (cfg.spoolmanLocation[0])
    plan["location"] = laneLocation(lane);
  if (oldId)
    plan["linked_spool"] = oldId;
  if (dryRun)
    return true;

  JsonDocument sb;
  sb["filament_id"] = filamentId;
  if (cfg.spoolmanLocation[0])
    sb["location"] = laneLocation(lane);
  sb["extra"]["tag"] = String("\"") + key + "\"";
  for (const char *k : {"initial_weight", "spool_weight", "price"})
    if ((opts[k] | 0.0f) > 0)
      sb[k] = opts[k];
  for (const char *k : {"lot_nr", "comment"})
    if (strlen(opts[k] | ""))
      sb[k] = opts[k];
  if (sb["lot_nr"].isNull() && !bambu && info["serial"].is<const char *>())
    sb["lot_nr"] = info["serial"];
  String body, resp;
  serializeJson(sb, body);
  int code = httpCall("POST", String(cfg.spoolmanUrl) + "/api/v1/spool", body, resp);
  JsonDocument created;
  if (code != 200 || deserializeJson(created, resp))
  {
    err = apiErr("create the spool", code, resp);
    return false;
  }
  int spoolId = created["id"];
  out["spool_id"] = spoolId;
  logEvent("Lane %d: created Spoolman spool #%d (filament #%d) for tag %s", lane + 1, spoolId,
           filamentId, key);
  bool ok = !oldId || detachOld(lane, oldId, opts["archive_old"] | false, err);
  if (oldId && ok)
    logEvent("Lane %d: tag moved off old spool #%d%s", lane + 1, oldId,
             (opts["archive_old"] | false) ? " (archived)" : "");
  spoolSyncRefreshAll();
  return ok;
}

bool spoolCreateForLane(int lane, bool dryRun, JsonObjectConst opts, JsonDocument &out, String &err)
{
  if (!cfg.spoolmanUrl[0])
  {
    err = "Set the Spoolman URL in Settings first";
    return false;
  }
  char key[48];
  if (!laneActive(lane) || !laneSpoolKey(lane, key, sizeof(key)))
  {
    err = "No settled tag in this lane yet";
    return false;
  }
  int chosenFilament = opts["filament_id"] | 0;
  bool move = opts["move"] | false;
  JsonDocument infoDoc;
  JsonObject info = infoDoc.to<JsonObject>();
  bool hasInfo = laneTagInfo(lane, info);
  if (!hasInfo && !chosenFilament)
  {
    err = "This lane's tag has no spool data: pick a filament";
    return false;
  }
  SpoolMatch existing;
  if (!findSpoolByTag(key, existing, err))
    return false;
  if (existing.id && !move && !dryRun)
  {
    err = String("This tag already belongs to Spoolman spool #") + existing.id;
    return false;
  }
  if (chosenFilament)
    return createSpool(lane, key, chosenFilament, false, info, opts, existing.id, dryRun, out, err);

  // What the tag says, in Spoolman's terms
  bool bambu = info["format"] == "bambu";
  String vendor, name, material, color, article;
  JsonDocument fil; // fields for a new filament, if needed
  if (bambu)
  {
    vendor = "Bambu Lab";
    // Material = detailed type ("PETG HF", "PLA Matte"): the AMS reports that
    // as tray_sub_brands, and OpenSpoolMan expects the filament material to
    // equal it, otherwise it flags a mismatch
    material = info["detailed_type"] | "";
    if (!material.length())
      material = info["type"] | "";
    name = material;
    color = hex6(info["color"]);
    article = info["variant_id"] | "";
    fil["diameter"] = info["diameter_mm"] | 1.75f;
    fil["density"] = defaultDensity(material);
    if ((info["spool_weight_g"] | 0) > 0)
      fil["weight"] = info["spool_weight_g"];
    int tmin = info["hotend_min_c"] | 0, tmax = info["hotend_max_c"] | 0;
    if (tmin && tmax)
      fil["settings_extruder_temp"] = ((tmin + tmax) / 2 + 2) / 5 * 5;
    if ((info["bed_temp_c"] | 0) > 0)
      fil["settings_bed_temp"] = info["bed_temp_c"];
    if (article.length())
      fil["article_number"] = article;
  }
  else
  {
    vendor = info["manufacturer"] | "";
    material = info["material"] | "";
    if (info["material_mod"].is<const char *>())
      material += String("-") + info["material_mod"].as<const char *>();
    name = info["color_name"] | material.c_str();
    color = hex6(info["color_1"]);
    article = info["sku"] | "";
    fil["diameter"] = info["diameter"] | 1.75f;
    fil["density"] = info["density"] | defaultDensity(material);
    if (!info["weight"].isNull())
      fil["weight"] = info["weight"];
    if (!info["empty_spool_weight"].isNull())
      fil["spool_weight"] = info["empty_spool_weight"];
    if (!info["print_temp"].isNull())
      fil["settings_extruder_temp"] = info["print_temp"];
    if (!info["bed_temp"].isNull())
      fil["settings_bed_temp"] = info["bed_temp"];
    if (article.length())
      fil["article_number"] = article;
    String multi;
    for (const char *k : {"color_2", "color_3", "color_4"})
      if (info[k].is<const char *>())
        multi += "," + hex6(info[k]);
    if (multi.length())
      fil["multi_color_hexes"] = color + multi;
  }
  fil["name"] = name;
  fil["material"] = material;
  if (!fil["multi_color_hexes"].is<const char *>() && color.length())
    fil["color_hex"] = color;

  // Look for an existing filament: Bambu by article number (variant ID
  // prefix), others by vendor + material + color (+ name / SKU)
  String resp;
  int code = httpCall("GET", String(cfg.spoolmanUrl) + "/api/v1/filament", "", resp);
  if (code != 200)
  {
    err = apiErr("list filaments", code, resp);
    return false;
  }
  JsonDocument filter;
  JsonObject ff = filter[0].to<JsonObject>();
  for (const char *k : {"id", "name", "material", "color_hex", "article_number"})
    ff[k] = true;
  ff["vendor"]["name"] = true;
  JsonDocument fils;
  if (deserializeJson(fils, resp, DeserializationOption::Filter(filter)))
  {
    err = "Couldn't parse Spoolman's filament list";
    return false;
  }
  resp = String(); // free the raw list
  int filamentId = 0;
  String filamentLabel;
  for (JsonObjectConst f : fils.as<JsonArrayConst>())
  {
    if (!vendor.equalsIgnoreCase(f["vendor"]["name"] | ""))
      continue;
    String art = f["article_number"] | "";
    bool match;
    if (bambu)
      match = article.length() && (art == article || art.startsWith(article + "-"));
    else if (article.length() && art.length())
      match = art.equalsIgnoreCase(article);
    else
      match = material.equalsIgnoreCase(f["material"] | "") &&
              color.equalsIgnoreCase(f["color_hex"] | "") &&
              (name == material || name.equalsIgnoreCase(f["name"] | ""));
    if (match)
    {
      filamentId = f["id"];
      filamentLabel = String(f["name"] | "") + " (" + (f["material"] | "") + ")";
      break;
    }
  }

  JsonObject plan = out["plan"].to<JsonObject>();
  plan["vendor"] = vendor;
  if (filamentId)
    plan["filament"] = filamentLabel;
  else
    plan["new_filament"] = fil.as<JsonObjectConst>();
  if (dryRun)
    return createSpool(lane, key, filamentId, bambu, info, opts, existing.id, true, out, err);

  // Create the vendor and filament if needed
  if (!filamentId)
  {
    int vendorId = 0;
    code = httpCall("GET", String(cfg.spoolmanUrl) + "/api/v1/vendor", "", resp);
    if (code != 200)
    {
      err = apiErr("list vendors", code, resp);
      return false;
    }
    JsonDocument vendors;
    deserializeJson(vendors, resp);
    for (JsonObjectConst v : vendors.as<JsonArrayConst>())
      if (vendor.equalsIgnoreCase(v["name"] | ""))
        vendorId = v["id"];
    if (!vendorId && vendor.length())
    {
      JsonDocument vb;
      vb["name"] = vendor;
      String body;
      serializeJson(vb, body);
      code = httpCall("POST", String(cfg.spoolmanUrl) + "/api/v1/vendor", body, resp);
      JsonDocument created;
      if (code != 200 || deserializeJson(created, resp))
      {
        err = apiErr("create the vendor", code, resp);
        return false;
      }
      vendorId = created["id"];
      logEvent("Spoolman: created vendor #%d %s", vendorId, vendor.c_str());
    }
    if (vendorId)
      fil["vendor_id"] = vendorId;
    String body;
    serializeJson(fil, body);
    code = httpCall("POST", String(cfg.spoolmanUrl) + "/api/v1/filament", body, resp);
    JsonDocument created;
    if (code != 200 || deserializeJson(created, resp))
    {
      err = apiErr("create the filament", code, resp);
      return false;
    }
    filamentId = created["id"];
    logEvent("Spoolman: created filament #%d %s", filamentId, name.c_str());
  }
  return createSpool(lane, key, filamentId, bambu, info, opts, existing.id, false, out, err);
}
