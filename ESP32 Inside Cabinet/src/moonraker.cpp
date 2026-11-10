#include "moonraker.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include "settings.h"
#include "rfid.h"
#include "diag.h"
#include "spoolman.h"
#include <time.h>

#define POLL_MS 1000
#define MAX_TOOLS 16

static int states[MAX_TOOLS];
static int toolCount = 0;
static bool haveStates = false; // first successful poll seen
static long lastSeq = -1;
static bool connected = false;
static String lastError;
static unsigned long lastPoll = 0;
static String printState, idleState, tcStatus; // print_stats, idle_timeout, toolchanger
static String sensorPattern;                   // _STORON's sensor_pattern
static int sensors[MAX_TOOLS];                 // tool's filament sensor: -1 unknown, 0, 1
static unsigned long mismatchSince[MAX_TOOLS]; // when state and sensor started to disagree
#define MISMATCH_MS 30000                      // ...for this long, with the printer idle
#define LANE_DATA_MS 3000                      // check lane_data at most this often
static String laneDataSent[MAX_TOOLS];         // what was last written per tool
static unsigned long lastLaneData = 0;
static void syncLaneData(unsigned long now);

int toolLoaded(int tool)
{
  if (!connected || !haveStates || tool < 0 || tool >= toolCount)
    return -1;
  return states[tool];
}

static void toolChanged(int tool, int before, int after)
{
  for (int lane = 0; lane < MAX_LANES; lane++)
  {
    if (!laneActive(lane) || cfg.laneTool[lane] != tool)
      continue;
    if (after == 0 && before == 1)
    {
      logEvent("Lane %d: T%d unloaded", lane + 1, tool);
      rfidClearLane(lane);
    }
    else if (after == 1)
    {
      const char *uid = laneReported(lane);
      logEvent("Lane %d: T%d loaded with %s", lane + 1, tool, uid[0] ? uid : "no tag seen yet");
    }
  }
}

void updateMoonraker(unsigned long now)
{
  if (!cfg.moonrakerUrl[0] || WiFi.status() != WL_CONNECTED || now - lastPoll < POLL_MS)
    return;
  lastPoll = now;

  // _STORON, print / busy state, toolchanger, and each tool's filament
  // sensor (names from _STORON's sensor_pattern; unknown objects are skipped)
  String q = "/printer/objects/query?gcode_macro%20_STORON&print_stats=state&idle_timeout=state&toolchanger=status";
  for (int t = 0; t < toolCount && sensorPattern.length(); t++)
  {
    String name = sensorPattern;
    name.replace("{n}", String(t));
    name.replace(" ", "%20");
    q += "&filament_switch_sensor%20" + name + "=filament_detected";
  }
  HTTPClient http;
  http.setTimeout(2000);
  http.begin(String(cfg.moonrakerUrl) + q);
  int code = http.GET();
  String body = code == 200 ? http.getString() : String();
  http.end();
  bool wasConnected = connected;
  if (code != 200)
  {
    connected = false;
    lastError = code < 0 ? String("Moonraker unreachable") : String("Moonraker HTTP ") + code;
    if (wasConnected)
      logEvent("Printer link lost: %s", lastError.c_str());
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, body))
  {
    lastError = "Bad reply from Moonraker";
    return;
  }
  JsonObjectConst m = doc["result"]["status"]["gcode_macro _STORON"];
  if (m.isNull() || !m["tool_loaded"].is<JsonArrayConst>())
  {
    connected = false;
    lastError = "_STORON macro not found: add storon.cfg to the printer config";
    if (wasConnected || lastSeq == -1)
      logEvent("Printer link: %s", lastError.c_str());
    lastSeq = -2; // log once
    return;
  }
  connected = true;
  lastError = "";
  if (!wasConnected)
  {
    logEvent("Printer link up (%s)", cfg.moonrakerUrl);
    for (String &s : laneDataSent)
      s = ""; // write everything again
  }
  JsonObjectConst st = doc["result"]["status"];
  printState = st["print_stats"]["state"] | "";
  idleState = st["idle_timeout"]["state"] | "";
  tcStatus = st["toolchanger"]["status"] | "";
  // Older storon.cfg has no sensor_pattern: assume the usual naming. An
  // explicit "" means no sensors.
  sensorPattern = m["sensor_pattern"].isNull() ? "T{n}_filament_sensor" : String(m["sensor_pattern"] | "");

  JsonArrayConst arr = m["tool_loaded"];
  long seq = m["seq"] | 0L;
  int n = min((int)arr.size(), MAX_TOOLS);
  // Klipper restarted (seq went backwards): adopt the new state without acting
  bool restarted = haveStates && seq < lastSeq;
  for (int t = 0; t < n; t++)
  {
    int v = arr[t] | -1;
    if (haveStates && !restarted && t < toolCount && v != states[t] && v >= 0 && states[t] >= 0)
      toolChanged(t, states[t], v);
    else if (haveStates && !restarted && t < toolCount && v == 1 && states[t] == -1)
      toolChanged(t, -1, v); // first event after a restart's unknown state
    states[t] = v;
  }
  toolCount = n;
  haveStates = true;
  lastSeq = seq;

  String why;
  bool busy = printerBusy(why);
  for (int t = 0; t < n; t++)
  {
    String name = sensorPattern;
    name.replace("{n}", String(t));
    JsonVariantConst s = st[String("filament_switch_sensor ") + name]["filament_detected"];
    sensors[t] = s.isNull() ? -1 : (s.as<bool>() ? 1 : 0);
    bool differs = states[t] >= 0 && sensors[t] >= 0 && states[t] != sensors[t] && !busy;
    if (!differs)
      mismatchSince[t] = 0;
    else if (!mismatchSince[t])
      mismatchSince[t] = now | 1;
  }
  syncLaneData(now);
}

// Slicer filament sync: Moonraker's "lane_data" database holds one record
// per tool (OrcaSlicer reads it to fill its filament slots; HelixScreen reads
// and writes it). StorOn writes whole records in the RFC 0001 lane status
// format (the one HelixScreen documents), keyed "T<n>", with
// writer = "storon". Writer rule: a record another program has claimed
// (its "writer" names someone else) is never overwritten; a record with no
// writer is legacy and gets replaced.
static const char *LANE_FIELDS[] = {"color", "material", "filament_type", "vendor_name", "name",
                                    "spool_id", "spoolman_filament_id", "filament_id", "bed_temp",
                                    "nozzle_temp", "remaining_weight_g", "total_weight_g"};
static bool laneDataBlocked[MAX_TOOLS]; // claimed by another writer (logged once)
static unsigned long laneDataChecked[MAX_TOOLS]; // last time the record was read back
#define LANE_DATA_VERIFY_MS 60000 // re-read each record this often: an edit made
                                  // elsewhere (HelixScreen 1.0.x writes without
                                  // claiming) would otherwise stick until StorOn's
                                  // own data changes

static void syncLaneData(unsigned long now)
{
  if (!connected || now - lastLaneData < LANE_DATA_MS)
    return;
  lastLaneData = now;
  for (int lane = 0; lane < MAX_LANES; lane++)
  {
    int tool = cfg.laneTool[lane];
    if (!laneActive(lane) || tool < 0 || tool >= MAX_TOOLS)
      continue;
    JsonDocument want;
    laneFilamentJson(lane, want.to<JsonObject>());
    String wantStr;
    serializeJson(want, wantStr);
    bool verify = wantStr == laneDataSent[tool];
    if (verify && now - laneDataChecked[tool] < LANE_DATA_VERIFY_MS)
      continue;
    laneDataChecked[tool] = now;

    String key = String("T") + tool;
    String base = String(cfg.moonrakerUrl) + "/server/database/item";
    HTTPClient http;
    http.setTimeout(2000);
    http.begin(base + "?namespace=lane_data&key=" + key);
    int code = http.GET();
    JsonDocument cur;
    if (code == 200)
      deserializeJson(cur, http.getString());
    http.end();
    if (code != 200 && code != 404)
      return; // try again next round
    JsonObjectConst old = cur["result"]["value"];
    String owner = old["writer"] | "";
    if (owner.length() && owner != "storon")
    {
      if (!laneDataBlocked[tool])
        logEvent("Lane %d: printer lane_data %s belongs to %s; not overwriting it", lane + 1,
                 key.c_str(), owner.c_str());
      laneDataBlocked[tool] = true;
      laneDataSent[tool] = wantStr; // don't ask again until something changes
      continue;
    }
    laneDataBlocked[tool] = false;
    // Read back: still ours and still what we wrote? Then nothing to do.
    if (verify && owner == "storon")
    {
      bool same = true;
      for (const char *k : LANE_FIELDS)
        if (!(old[k] == want[k]) && !(old[k].isNull() && want[k].isNull()))
          same = false;
      if (same)
        continue;
    }

    JsonDocument body;
    body["namespace"] = "lane_data";
    body["key"] = key;
    JsonObject val = body["value"].to<JsonObject>();
    val["lss_version"] = "0.1";
    val["writer"] = "storon";
    val["lane"] = String(tool); // a string: OrcaSlicer drops a numeric lane
    val["lane_name"] = key;
    val["loaded"] = !want.as<JsonObjectConst>().isNull() && want.size() > 0;
    for (const char *k : LANE_FIELDS) // every field present, null when unknown
      if (want[k].isNull())
        val[k] = nullptr;
      else
        val[k] = want[k];
    time_t t = time(nullptr);
    if (t > 1700000000) // clock set by NTP
    {
      char iso[24];
      strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", gmtime(&t));
      val["scan_time"] = iso;
    }
    else
      val["scan_time"] = nullptr;
    if (old["helix_fingerprint"].is<const char *>())
      val["helix_fingerprint"] = old["helix_fingerprint"]; // HelixScreen asks writers to keep it
    String payload;
    serializeJson(body, payload);
    http.begin(base);
    http.addHeader("Content-Type", "application/json");
    code = http.POST(payload);
    http.end();
    if (code != 200)
    {
      logEvent("Lane %d: couldn't update printer lane_data %s (HTTP %d)", lane + 1, key.c_str(), code);
      return;
    }
    laneDataSent[tool] = wantStr;
    String mat = val["material"] | "";
    logEvent("Lane %d: %s lane data %s = %s %s", lane + 1, verify ? "restored" : "slicer", key.c_str(),
             mat.length() ? mat.c_str() : "(empty)", String(val["color"] | "").c_str());
    return; // one write per round
  }
}

bool printerBusy(String &why)
{
  if (!connected)
    why = "the printer isn't connected";
  else if (printState == "printing" || printState == "paused")
    why = "a print is " + printState;
  else if (idleState == "Printing")
    why = "the printer is running a command";
  else if (tcStatus.length() && tcStatus != "ready")
    why = "the toolchanger is " + tcStatus;
  else
    return false;
  return true;
}

int toolSensor(int tool)
{
  return connected && tool >= 0 && tool < toolCount ? sensors[tool] : -1;
}

bool toolMismatch(int tool)
{
  return toolSensor(tool) >= 0 && mismatchSince[tool] && millis() - mismatchSince[tool] >= MISMATCH_MS;
}

static String urlEncode(const String &s)
{
  String out;
  char hex[4];
  for (char c : s)
  {
    if (isalnum((unsigned char)c) || strchr("-_.~", c))
      out += c;
    else
    {
      snprintf(hex, sizeof(hex), "%%%02X", (unsigned char)c);
      out += hex;
    }
  }
  return out;
}

// Send G-code through Moonraker. It answers when the G-code has finished,
// which for a filament load takes minutes: wait 3 s, which is long enough to
// catch errors (unknown command, macro error), then report it as started.
static bool runGcode(const String &script, String &err, bool &finished)
{
  HTTPClient http;
  http.setTimeout(3000);
  http.begin(String(cfg.moonrakerUrl) + "/printer/gcode/script?script=" + urlEncode(script));
  int code = http.POST("");
  String reply = code > 0 ? http.getString() : String();
  http.end();
  finished = code == 200;
  if (code == 200 || code == HTTPC_ERROR_READ_TIMEOUT)
    return true;
  JsonDocument doc;
  String msg;
  if (code > 0 && !deserializeJson(doc, reply))
    msg = doc["error"]["message"] | "";
  err = msg.length() ? "Printer: " + msg
                     : code < 0 ? String("Moonraker unreachable") : String("Moonraker HTTP ") + code;
  return false;
}

// Fill a command template. A word holding a placeholder without a value is
// dropped, so "LOAD_FILAMENT TOOL={tool} TEMP={temp}" works with or without
// a known temperature.
static String fillTemplate(const char *tpl, int lane, int tool, int temp, const String &material)
{
  String out, word, src = tpl;
  src += ' ';
  for (char c : src)
  {
    if (c != ' ')
    {
      word += c;
      continue;
    }
    if (!word.length())
      continue;
    bool skip = (word.indexOf("{temp}") >= 0 && !temp) ||
                (word.indexOf("{material}") >= 0 && !material.length());
    word.replace("{tool}", String(tool));
    word.replace("{lane}", String(lane + 1));
    word.replace("{temp}", String(temp));
    word.replace("{material}", material);
    if (!skip)
      out += (out.length() ? " " : "") + word;
    word = "";
  }
  return out;
}

bool laneToolAction(int lane, const String &action, String &msg)
{
  if (lane < 0 || lane >= MAX_LANES || !laneActive(lane))
  {
    msg = "Lane is disabled";
    return false;
  }
  int tool = cfg.laneTool[lane];
  if (tool < 0)
  {
    msg = "This lane doesn't feed a tool (Settings)";
    return false;
  }
  String why;
  if (printerBusy(why))
  {
    msg = "Not now: " + why;
    return false;
  }
  String script;
  if (action == "load" || action == "unload")
  {
    const char *tpl = action == "load" ? cfg.loadCmd : cfg.unloadCmd;
    if (!tpl[0])
    {
      msg = "Set the " + action + " command in Settings first";
      return false;
    }
    String material;
    int temp = laneNozzleTemp(lane, material);
    script = fillTemplate(tpl, lane, tool, temp, material);
  }
  else if (action == "mark_loaded" || action == "mark_unloaded")
    script = String("STORON_TOOL_EVENT EVENT=") + (action == "mark_loaded" ? "loaded" : "unloaded") +
             " TOOL=" + tool;
  else
  {
    msg = "Unknown action";
    return false;
  }
  bool finished;
  if (!runGcode(script, msg, finished))
  {
    logEvent("Lane %d: %s failed: %s", lane + 1, script.c_str(), msg.c_str());
    return false;
  }
  logEvent("Lane %d: sent %s", lane + 1, script.c_str());
  msg = (finished ? "Done: " : "Started on the printer: ") + script;
  return true;
}

void moonrakerStatusJson(JsonObject out)
{
  out["configured"] = cfg.moonrakerUrl[0] != '\0';
  out["connected"] = connected;
  if (lastError.length())
    out["error"] = lastError;
  JsonArray tools = out["tool_loaded"].to<JsonArray>();
  for (int t = 0; t < toolCount; t++)
    tools.add(states[t]);
  if (connected && printState.length())
    out["print_state"] = printState;
  String why;
  if (printerBusy(why))
    out["busy"] = why;
  out["load_command"] = cfg.loadCmd[0] != '\0';
  out["unload_command"] = cfg.unloadCmd[0] != '\0';
}
