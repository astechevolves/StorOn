#include "settings.h"
#include <Preferences.h>

// Optional build-time defaults. Copy config.example.h to config.h to pre-fill
// WiFi/MQTT so a fresh board connects without the setup hotspot. Values saved
// from the web UI always win over these.
#if __has_include("config.h")
#include "config.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif
#ifndef MQTT_BROKER
#define MQTT_BROKER ""
#endif
#ifndef MQTT_PORT
#define MQTT_PORT 1883
#endif
#ifndef MQTT_USER
#define MQTT_USER ""
#endif
#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD ""
#endif

Settings cfg;

static const char *NS = "cfg"; // NVS namespace (the fan's HA settings live in "fan")

// ESP32-S3 (N16R8) GPIOs that are safe to use. Left out: 0, 3, 45, 46
// (boot strapping), 19-20 (USB), 26-37 (flash and octal PSRAM), 43-44 (serial
// console).
static const int8_t ALLOWED_PINS[] = {1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
                                      15, 16, 17, 18, 21, 38, 39, 40, 41, 42, 47, 48};

bool pinAllowed(int pin)
{
  for (int8_t p : ALLOWED_PINS)
    if (p == pin)
      return true;
  return false;
}

static void setDefaults(Settings &s)
{
  memset(&s, 0, sizeof(s));
  strlcpy(s.deviceName, "Filament Monitor", sizeof(s.deviceName));
  strlcpy(s.deviceId, "filament_monitor", sizeof(s.deviceId));
  strlcpy(s.wifiSsid, WIFI_SSID, sizeof(s.wifiSsid));
  strlcpy(s.wifiPass, WIFI_PASSWORD, sizeof(s.wifiPass));
  strlcpy(s.mqttHost, MQTT_BROKER, sizeof(s.mqttHost));
  s.mqttPort = MQTT_PORT;
  strlcpy(s.mqttUser, MQTT_USER, sizeof(s.mqttUser));
  strlcpy(s.mqttPass, MQTT_PASSWORD, sizeof(s.mqttPass));
  s.dhtHighPin = 4;
  s.dhtLowPin = 5;
  s.fanPin = 14;
  s.ledPin = 48; // onboard WS2812 (GPIO 38 on DevKitC-1 v1.1 boards)
  s.spiSck = 12; // the S3's native SPI pins
  s.spiMiso = 13;
  s.spiMosi = 11;
  strlcpy(s.spoolmanLocation, "StorOn", sizeof(s.spoolmanLocation));
  const int8_t ss[MAX_LANES] = {10, 9, 8, 7, 6, 15};
  memcpy(s.ssPins, ss, sizeof(ss));
  for (int i = 0; i < MAX_LANES; i++)
  {
    s.laneEnabled[i] = true; // disable unused lanes in the web UI
    s.laneTool[i] = i;       // lane 1 feeds T0, lane 2 feeds T1, ...
  }
  s.sensorIntervalS = 30;
  s.fanCycleMin = 60;
  s.fanOnMin = 5;
  s.rfidCycleMs = 500; // fast enough to catch a tag passing as the spool turns
  s.laneReleaseMin = 0;
  s.ledBrightness = 5;
}

static void getStr(Preferences &p, const char *key, char *buf, size_t len)
{
  if (p.isKey(key))
    p.getString(key, buf, len);
}

void loadSettings()
{
  setDefaults(cfg);
  Preferences p;
  // Read-write so a missing namespace is created instead of logging an error
  if (!p.begin(NS, false))
    return;
  getStr(p, "dev_name", cfg.deviceName, sizeof(cfg.deviceName));
  getStr(p, "dev_id", cfg.deviceId, sizeof(cfg.deviceId));
  getStr(p, "admin_pass", cfg.adminPass, sizeof(cfg.adminPass));
  getStr(p, "wifi_ssid", cfg.wifiSsid, sizeof(cfg.wifiSsid));
  getStr(p, "wifi_pass", cfg.wifiPass, sizeof(cfg.wifiPass));
  getStr(p, "mqtt_host", cfg.mqttHost, sizeof(cfg.mqttHost));
  cfg.mqttPort = p.getUShort("mqtt_port", cfg.mqttPort);
  getStr(p, "mqtt_user", cfg.mqttUser, sizeof(cfg.mqttUser));
  getStr(p, "mqtt_pass", cfg.mqttPass, sizeof(cfg.mqttPass));
  cfg.dhtHighPin = p.getChar("pin_dht_hi", cfg.dhtHighPin);
  cfg.dhtLowPin = p.getChar("pin_dht_lo", cfg.dhtLowPin);
  cfg.fanPin = p.getChar("pin_fan", cfg.fanPin);
  cfg.ledPin = p.getChar("pin_led", cfg.ledPin);
  cfg.spiSck = p.getChar("pin_sck", cfg.spiSck);
  cfg.spiMiso = p.getChar("pin_miso", cfg.spiMiso);
  cfg.spiMosi = p.getChar("pin_mosi", cfg.spiMosi);
  getStr(p, "spoolman_url", cfg.spoolmanUrl, sizeof(cfg.spoolmanUrl));
  getStr(p, "sm_location", cfg.spoolmanLocation, sizeof(cfg.spoolmanLocation));
  getStr(p, "sm_away_loc", cfg.spoolmanAwayLocation, sizeof(cfg.spoolmanAwayLocation));
  getStr(p, "mr_url", cfg.moonrakerUrl, sizeof(cfg.moonrakerUrl));
  getStr(p, "load_cmd", cfg.loadCmd, sizeof(cfg.loadCmd));
  getStr(p, "unload_cmd", cfg.unloadCmd, sizeof(cfg.unloadCmd));
  if (p.isKey("lane_tool"))
    p.getBytes("lane_tool", cfg.laneTool, sizeof(cfg.laneTool));
  if (p.isKey("lane_ss"))
    p.getBytes("lane_ss", cfg.ssPins, sizeof(cfg.ssPins));
  if (p.isKey("lane_en"))
  {
    uint8_t mask = p.getUChar("lane_en", 0);
    for (int i = 0; i < MAX_LANES; i++)
      cfg.laneEnabled[i] = mask & (1 << i);
  }
  else if (p.isKey("lane_count"))
  {
    // Firmware 0.2.0 stored a reader count and an 8-entry pin list
    int count = p.getUChar("lane_count", 0);
    int8_t old[8];
    if (p.getBytes("pin_ss", old, sizeof(old)) == sizeof(old))
      memcpy(cfg.ssPins, old, sizeof(cfg.ssPins));
    for (int i = 0; i < MAX_LANES; i++)
      cfg.laneEnabled[i] = i < count;
  }
  cfg.sensorIntervalS = p.getUShort("sensor_s", cfg.sensorIntervalS);
  cfg.fanCycleMin = p.getUShort("fan_cycle", cfg.fanCycleMin);
  cfg.fanOnMin = p.getUShort("fan_on", cfg.fanOnMin);
  cfg.rfidCycleMs = p.getUShort("rfid_ms", cfg.rfidCycleMs);
  cfg.laneReleaseMin = p.getUShort("lane_rel_min", cfg.laneReleaseMin);
  cfg.ledBrightness = p.getUChar("led_bright", cfg.ledBrightness);
  p.end();
}

static void saveSettings(const Settings &s)
{
  Preferences p;
  p.begin(NS, false);
  p.putString("dev_name", s.deviceName);
  p.putString("dev_id", s.deviceId);
  p.putString("admin_pass", s.adminPass);
  p.putString("wifi_ssid", s.wifiSsid);
  p.putString("wifi_pass", s.wifiPass);
  p.putString("mqtt_host", s.mqttHost);
  p.putUShort("mqtt_port", s.mqttPort);
  p.putString("mqtt_user", s.mqttUser);
  p.putString("mqtt_pass", s.mqttPass);
  p.putChar("pin_dht_hi", s.dhtHighPin);
  p.putChar("pin_dht_lo", s.dhtLowPin);
  p.putChar("pin_fan", s.fanPin);
  p.putChar("pin_led", s.ledPin);
  p.putChar("pin_sck", s.spiSck);
  p.putChar("pin_miso", s.spiMiso);
  p.putChar("pin_mosi", s.spiMosi);
  p.putString("spoolman_url", s.spoolmanUrl);
  p.putString("sm_location", s.spoolmanLocation);
  p.putString("sm_away_loc", s.spoolmanAwayLocation);
  p.putString("mr_url", s.moonrakerUrl);
  p.putString("load_cmd", s.loadCmd);
  p.putString("unload_cmd", s.unloadCmd);
  p.putBytes("lane_tool", s.laneTool, sizeof(s.laneTool));
  uint8_t mask = 0;
  for (int i = 0; i < MAX_LANES; i++)
    if (s.laneEnabled[i])
      mask |= 1 << i;
  p.putUChar("lane_en", mask);
  p.putBytes("lane_ss", s.ssPins, sizeof(s.ssPins));
  p.putUShort("sensor_s", s.sensorIntervalS);
  p.putUShort("fan_cycle", s.fanCycleMin);
  p.putUShort("fan_on", s.fanOnMin);
  p.putUShort("rfid_ms", s.rfidCycleMs);
  p.putUShort("lane_rel_min", s.laneReleaseMin);
  p.putUChar("led_bright", s.ledBrightness);
  p.end();
}

void factoryReset()
{
  Preferences p;
  p.begin(NS, false);
  p.clear();
  p.end();
  p.begin("fan", false);
  p.clear();
  p.end();
  delay(200);
  ESP.restart();
}

// ── Validation helpers ────────────────────────────────────
static bool takeStr(JsonDocument &doc, const char *key, char *out, size_t len,
                    String &err)
{
  if (!doc[key].is<const char *>())
    return true; // not sent: keep the current value
  const char *v = doc[key];
  if (strlen(v) >= len)
  {
    err = String(key) + " is too long (max " + (len - 1) + " characters)";
    return false;
  }
  strlcpy(out, v, len);
  return true;
}

static bool takeInt(JsonDocument &doc, const char *key, long lo, long hi,
                    long &out, String &err)
{
  if (doc[key].isNull())
    return true;
  if (!doc[key].is<long>())
  {
    err = String(key) + " must be a whole number";
    return false;
  }
  long v = doc[key];
  if (v < lo || v > hi)
  {
    err = String(key) + " must be between " + lo + " and " + hi;
    return false;
  }
  out = v;
  return true;
}

static bool takePin(JsonDocument &doc, const char *key, bool optional,
                    int8_t &out, String &err)
{
  if (doc[key].isNull())
    return true;
  long v = doc[key] | -2L;
  if (optional && v == -1)
  {
    out = -1;
    return true;
  }
  if (!pinAllowed(v))
  {
    err = String(key) + ": GPIO " + v + " can't be used on this board";
    return false;
  }
  out = v;
  return true;
}

bool applySettings(JsonDocument &doc, String &err)
{
  Settings s = cfg; // validate on a copy; commit only if everything passes

  if (!takeStr(doc, "device_name", s.deviceName, sizeof(s.deviceName), err) ||
      !takeStr(doc, "device_id", s.deviceId, sizeof(s.deviceId), err) ||
      !takeStr(doc, "wifi_ssid", s.wifiSsid, sizeof(s.wifiSsid), err) ||
      !takeStr(doc, "mqtt_host", s.mqttHost, sizeof(s.mqttHost), err) ||
      !takeStr(doc, "mqtt_user", s.mqttUser, sizeof(s.mqttUser), err))
    return false;
  // Passwords: a blank value means "keep the saved one", so a form submitted
  // with empty password boxes can never wipe them
  for (const char *k : {"wifi_pass", "mqtt_pass", "admin_pass"})
    if (doc[k].is<const char *>() && strlen(doc[k].as<const char *>()) == 0)
      doc.remove(k);
  if (!takeStr(doc, "wifi_pass", s.wifiPass, sizeof(s.wifiPass), err) ||
      !takeStr(doc, "mqtt_pass", s.mqttPass, sizeof(s.mqttPass), err) ||
      !takeStr(doc, "admin_pass", s.adminPass, sizeof(s.adminPass), err))
    return false;
  if (doc["clear_admin_pass"] | false)
    s.adminPass[0] = '\0';

  if (s.deviceId[0] == '\0')
  {
    err = "device_id can't be empty";
    return false;
  }
  for (const char *c = s.deviceId; *c; c++)
    if (!(islower(*c) || isdigit(*c) || *c == '_'))
    {
      err = "device_id may only use a-z, 0-9 and _";
      return false;
    }

  long v;
  v = s.mqttPort;
  if (!takeInt(doc, "mqtt_port", 1, 65535, v, err))
    return false;
  s.mqttPort = v;
  v = s.sensorIntervalS;
  if (!takeInt(doc, "sensor_interval_s", 5, 3600, v, err))
    return false;
  s.sensorIntervalS = v;
  v = s.fanCycleMin;
  if (!takeInt(doc, "fan_cycle_min", 1, 1440, v, err))
    return false;
  s.fanCycleMin = v;
  v = s.fanOnMin;
  if (!takeInt(doc, "fan_on_min", 0, 1440, v, err))
    return false;
  s.fanOnMin = v;
  v = s.rfidCycleMs;
  if (!takeInt(doc, "rfid_cycle_ms", 200, 60000, v, err))
    return false;
  s.rfidCycleMs = v;
  v = s.laneReleaseMin;
  if (!takeInt(doc, "lane_release_min", 0, 10080, v, err))
    return false;
  s.laneReleaseMin = v;
  v = s.ledBrightness;
  if (!takeInt(doc, "led_brightness", 0, 255, v, err))
    return false;
  s.ledBrightness = v;
  if (s.fanOnMin > s.fanCycleMin)
  {
    err = "fan_on_min can't be longer than fan_cycle_min";
    return false;
  }

  if (!takePin(doc, "pin_dht_high", true, s.dhtHighPin, err) ||
      !takePin(doc, "pin_dht_low", true, s.dhtLowPin, err) ||
      !takePin(doc, "pin_fan", true, s.fanPin, err) ||
      !takePin(doc, "pin_led", true, s.ledPin, err) ||
      !takePin(doc, "pin_sck", false, s.spiSck, err) ||
      !takePin(doc, "pin_miso", false, s.spiMiso, err) ||
      !takePin(doc, "pin_mosi", false, s.spiMosi, err))
    return false;

  if (!takeStr(doc, "spoolman_url", s.spoolmanUrl, sizeof(s.spoolmanUrl), err) ||
      !takeStr(doc, "spoolman_location", s.spoolmanLocation, sizeof(s.spoolmanLocation), err) ||
      !takeStr(doc, "spoolman_away_location", s.spoolmanAwayLocation, sizeof(s.spoolmanAwayLocation), err) ||
      !takeStr(doc, "moonraker_url", s.moonrakerUrl, sizeof(s.moonrakerUrl), err) ||
      !takeStr(doc, "load_command", s.loadCmd, sizeof(s.loadCmd), err) ||
      !takeStr(doc, "unload_command", s.unloadCmd, sizeof(s.unloadCmd), err))
    return false;
  size_t mrLen = strlen(s.moonrakerUrl);
  while (mrLen && s.moonrakerUrl[mrLen - 1] == '/')
    s.moonrakerUrl[--mrLen] = '\0';
  if (mrLen && strncmp(s.moonrakerUrl, "http://", 7) != 0)
  {
    err = "Moonraker URL must start with http:// (e.g. http://192.168.1.20)";
    return false;
  }
  size_t urlLen = strlen(s.spoolmanUrl);
  while (urlLen && s.spoolmanUrl[urlLen - 1] == '/')
    s.spoolmanUrl[--urlLen] = '\0'; // store without a trailing slash
  if (urlLen && strncmp(s.spoolmanUrl, "http://", 7) != 0)
  {
    err = "Spoolman URL must start with http:// (e.g. http://192.168.1.10:7912)";
    return false;
  }

  // Lanes: [{enabled, ss}, ...] in lane order. A disabled lane keeps its pin
  // but isn't checked or used.
  if (doc["lanes"].is<JsonArrayConst>())
  {
    JsonArrayConst arr = doc["lanes"];
    if (arr.size() != MAX_LANES)
    {
      err = String("Expected settings for ") + MAX_LANES + " lanes";
      return false;
    }
    for (int i = 0; i < MAX_LANES; i++)
    {
      JsonVariantConst l = arr[i];
      s.laneEnabled[i] = l["enabled"] | false;
      long pin = l["ss"] | -2L;
      if (s.laneEnabled[i] && !pinAllowed(pin))
      {
        err = String("Lane ") + (i + 1) + " SS: GPIO " + pin + " can't be used on this board";
        return false;
      }
      if (pinAllowed(pin))
        s.ssPins[i] = pin;
      long tool = l["tool"] | (long)s.laneTool[i];
      if (tool < -1 || tool > 63)
      {
        err = String("Lane ") + (i + 1) + ": tool must be -1 (none) or a T number";
        return false;
      }
      s.laneTool[i] = tool;
    }
  }

  // No pin may be used twice
  struct
  {
    const char *name;
    int pin;
  } used[7 + MAX_LANES];
  int n = 0;
  used[n++] = {"DHT high", s.dhtHighPin};
  used[n++] = {"DHT low", s.dhtLowPin};
  used[n++] = {"Fan", s.fanPin};
  used[n++] = {"Status LED", s.ledPin};
  used[n++] = {"SPI SCK", s.spiSck};
  used[n++] = {"SPI MISO", s.spiMiso};
  used[n++] = {"SPI MOSI", s.spiMosi};
  static char ssNames[MAX_LANES][16];
  for (int i = 0; i < MAX_LANES; i++)
  {
    if (!s.laneEnabled[i])
      continue;
    snprintf(ssNames[i], sizeof(ssNames[i]), "Lane %d SS", i + 1);
    used[n++] = {ssNames[i], s.ssPins[i]};
  }
  for (int i = 0; i < n; i++)
    for (int j = i + 1; j < n; j++)
      if (used[i].pin >= 0 && used[i].pin == used[j].pin)
      {
        err = String(used[i].name) + " and " + used[j].name + " both use GPIO " +
              used[i].pin;
        return false;
      }

  cfg = s;
  saveSettings(cfg);
  return true;
}

void settingsToJson(JsonDocument &doc)
{
  doc["device_name"] = cfg.deviceName;
  doc["device_id"] = cfg.deviceId;
  doc["admin_pass_set"] = cfg.adminPass[0] != '\0';
  doc["wifi_ssid"] = cfg.wifiSsid;
  doc["wifi_pass_set"] = cfg.wifiPass[0] != '\0';
  doc["mqtt_host"] = cfg.mqttHost;
  doc["mqtt_port"] = cfg.mqttPort;
  doc["mqtt_user"] = cfg.mqttUser;
  doc["mqtt_pass_set"] = cfg.mqttPass[0] != '\0';
  doc["pin_dht_high"] = cfg.dhtHighPin;
  doc["pin_dht_low"] = cfg.dhtLowPin;
  doc["pin_fan"] = cfg.fanPin;
  doc["pin_led"] = cfg.ledPin;
  doc["pin_sck"] = cfg.spiSck;
  doc["pin_miso"] = cfg.spiMiso;
  doc["pin_mosi"] = cfg.spiMosi;
  doc["spoolman_url"] = cfg.spoolmanUrl;
  doc["spoolman_location"] = cfg.spoolmanLocation;
  doc["spoolman_away_location"] = cfg.spoolmanAwayLocation;
  doc["moonraker_url"] = cfg.moonrakerUrl;
  doc["load_command"] = cfg.loadCmd;
  doc["unload_command"] = cfg.unloadCmd;
  JsonArray lanes = doc["lanes"].to<JsonArray>();
  for (int i = 0; i < MAX_LANES; i++)
  {
    JsonObject l = lanes.add<JsonObject>();
    l["enabled"] = cfg.laneEnabled[i];
    l["ss"] = cfg.ssPins[i];
    l["tool"] = cfg.laneTool[i];
  }
  doc["sensor_interval_s"] = cfg.sensorIntervalS;
  doc["fan_cycle_min"] = cfg.fanCycleMin;
  doc["fan_on_min"] = cfg.fanOnMin;
  doc["rfid_cycle_ms"] = cfg.rfidCycleMs;
  doc["lane_release_min"] = cfg.laneReleaseMin;
  doc["led_brightness"] = cfg.ledBrightness;
  JsonArray allowed = doc["allowed_pins"].to<JsonArray>();
  for (int8_t p : ALLOWED_PINS)
    allowed.add(p);
  doc["max_lanes"] = MAX_LANES;
}
