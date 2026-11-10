#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "app.h"
#include "settings.h"
#include "rfid.h"
#include "diag.h"
#include "spoolman.h"
#include "moonraker.h"
#include <driver/gpio.h>

// Pins, network and timing come from the web UI (see settings.cpp for the
// defaults). Fan wiring for the default setup (MOSFET module channel 1):
// DC+ (24V) -> 1.5k 1/4W -> PWM1+, GND1 -> 2N3904 collector, emitter -> ESP32
// GND (shared with 24V DC-), fan GPIO -> 1k -> base.

// ── Fixed behaviour ───────────────────────────────────────
#define LED_IDLE_PERIOD_MS 2000 // heartbeat every 2 s while the fan is off
#define LED_FAN_PERIOD_MS 1000  // heartbeat every 1 s while the fan runs
#define WIFI_BOOT_WAIT_MS 20000 // how long to wait for WiFi before starting the setup hotspot
#define WIFI_LOST_AP_MS 120000  // start the setup hotspot after WiFi has been down this long
#define AP_STA_RETRY_MS 60000   // with the hotspot up, retry the saved WiFi this often
#define MQTT_RETRY_MS 10000
#define AP_PASSWORD "storonsetup" // setup hotspot password (WPA2 needs 8+ characters)
#define BOOT_BUTTON_PIN 0         // hold 10 s for a factory reset
#define BOOT_HOLD_MS 10000

// ── Fan defaults (HA can change these; they're saved to flash) ──
#define FAN_SPEED_PCT 60      // default speed for scheduled and manual runs
#define FAN_MIN_PCT 30        // 2-wire fans may stall below ~30-40%
#define FAN_MANUAL_RUN_MIN 30 // a manual "on" from HA runs this long (0 = until turned off)
#define FAN_MAX_RUN_MIN 240

// ── MQTT topics (prefixed with the device ID at boot) ─────
static char topicStatus[80], topicHighTemp[80], topicHighHum[80], topicLowTemp[80],
    topicLowHum[80], topicFanState[80], topicFanSet[80], topicFanPct[80],
    topicFanPctSet[80], topicFanRun[80], topicFanRunSet[80];

static void buildTopics()
{
  const char *id = cfg.deviceId;
  snprintf(topicStatus, sizeof(topicStatus), "%s/status", id); // "online" / "offline" (last will)
  snprintf(topicHighTemp, sizeof(topicHighTemp), "%s/filament_high/temperature", id);
  snprintf(topicHighHum, sizeof(topicHighHum), "%s/filament_high/humidity", id);
  snprintf(topicLowTemp, sizeof(topicLowTemp), "%s/filament_low/temperature", id);
  snprintf(topicLowHum, sizeof(topicLowHum), "%s/filament_low/humidity", id);
  snprintf(topicFanState, sizeof(topicFanState), "%s/circulation_fan/state", id); // "ON" / "OFF"
  snprintf(topicFanSet, sizeof(topicFanSet), "%s/circulation_fan/set", id);
  snprintf(topicFanPct, sizeof(topicFanPct), "%s/circulation_fan/percentage", id);
  snprintf(topicFanPctSet, sizeof(topicFanPctSet), "%s/circulation_fan/percentage/set", id);
  snprintf(topicFanRun, sizeof(topicFanRun), "%s/circulation_fan/run_minutes", id);
  snprintf(topicFanRunSet, sizeof(topicFanRunSet), "%s/circulation_fan/run_minutes/set", id);
}

static void laneTopic(int lane, char *out, size_t len)
{
  snprintf(out, len, "%s/lane_%d/tag", cfg.deviceId, lane + 1); // UID in hex, or "empty"
}

// JSON with the tag UID plus decoded spool data (Bambu: tray_uuid, type, color...)
static void laneAttrTopic(int lane, char *out, size_t len)
{
  snprintf(out, len, "%s/lane_%d/attributes", cfg.deviceId, lane + 1);
}

// ── Objects ───────────────────────────────────────────────
DHT *dhtHigh = nullptr;
DHT *dhtLow = nullptr;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
Preferences prefs;

unsigned long lastReadTime = 0;
unsigned long rebootAt = 0;

struct Reading
{
  bool ok = false;
  bool tried = false; // at least one read attempted since boot
  float temp = 0, hum = 0;
  unsigned long at = 0;
};
Reading readingHigh, readingLow;

// ── Network state ─────────────────────────────────────────
bool apActive = false;
char apSsid[33] = "";
bool wifiWasConnected = false;
unsigned long wifiConnectedAt = 0;
unsigned long wifiLostAt = 0;
unsigned long lastStaRetry = 0;
unsigned long lastMqttAttempt = 0;
bool mdnsStarted = false;

bool setupApActive() { return apActive; }
void scheduleReboot(unsigned long delayMs) { rebootAt = millis() + delayMs; }

// ── Fan state ─────────────────────────────────────────────
// AUTO: follow the schedule. MANUAL_ON: HA turned it on (times out after
// fanRunMin, unless 0). MANUAL_OFF: HA turned off a scheduled run; the
// schedule takes over again once that run's window ends.
enum FanMode
{
  FAN_AUTO,
  FAN_MANUAL_ON,
  FAN_MANUAL_OFF
};
FanMode fanMode = FAN_AUTO;
unsigned long manualStart = 0;
int fanSpeedPct = FAN_SPEED_PCT;
int fanRunMin = FAN_MANUAL_RUN_MIN;
int fanDuty = -1; // current PWM duty, -1 until the first updateFan()
bool fanRunning = false;

bool scheduledOn(unsigned long now)
{
  unsigned long cycle = cfg.fanCycleMin * 60000UL;
  return (now % cycle) < cfg.fanOnMin * 60000UL;
}

// ── HA Discovery ──────────────────────────────────────────
// Entity IDs: the default device keeps the IDs it had before the device ID was
// configurable, so existing Home Assistant entities carry over. Any other
// device ID gets its own prefixed set, so several cabinets can coexist.
static void entityId(char *out, size_t len, const char *legacy, const char *suffix)
{
  if (strcmp(cfg.deviceId, "filament_monitor") == 0)
    strlcpy(out, legacy, len);
  else
    snprintf(out, len, "%s_%s", cfg.deviceId, suffix);
}

// Fields every entity shares: availability and the device it belongs to
void addCommon(JsonDocument &doc)
{
  doc["availability_topic"] = topicStatus;
  JsonObject device = doc["device"].to<JsonObject>();
  device["identifiers"][0] = cfg.deviceId;
  device["name"] = cfg.deviceName;
  device["model"] = "StorOn ESP32-S3 monitor";
  device["manufacturer"] = "DIY";
  device["sw_version"] = FW_VERSION;
  device["configuration_url"] = "http://" + WiFi.localIP().toString() + "/";
}

void sendDiscovery(const char *topic, JsonDocument &doc)
{
  addCommon(doc);
  char payload[900]; // must stay under the MQTT buffer (1024) minus the topic
  if (measureJson(doc) >= sizeof(payload))
  {
    logEvent("Discovery payload too big for %s", topic);
    return;
  }
  serializeJson(doc, payload);
  mqtt.publish(topic, payload, true);
}

void publishDiscovery(const char *legacyId, const char *suffix, const char *sensorName,
                      const char *stateTopic, const char *unit, const char *deviceClass)
{
  char id[64], topic[128];
  entityId(id, sizeof(id), legacyId, suffix);
  snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/config", id);

  JsonDocument doc;
  doc["name"] = sensorName;
  doc["state_topic"] = stateTopic;
  doc["unit_of_measurement"] = unit;
  doc["device_class"] = deviceClass;
  doc["state_class"] = "measurement";
  doc["unique_id"] = id;
  sendDiscovery(topic, doc);
}

// Fan entity: on/off plus a 1-100% speed slider
void publishFanDiscovery()
{
  char id[64], topic[128];
  entityId(id, sizeof(id), "filament_circulation_fan", "circulation_fan");
  snprintf(topic, sizeof(topic), "homeassistant/fan/%s/config", id);
  JsonDocument doc;
  doc["name"] = "Circulation Fan";
  doc["unique_id"] = id;
  doc["state_topic"] = topicFanState;
  doc["command_topic"] = topicFanSet;
  doc["percentage_state_topic"] = topicFanPct;
  doc["percentage_command_topic"] = topicFanPctSet;
  doc["speed_range_min"] = 1;
  doc["speed_range_max"] = 100;
  sendDiscovery(topic, doc);
}

// Number entity: how long a manual "on" from HA lasts
void publishRunTimeDiscovery()
{
  char id[64], topic[128];
  entityId(id, sizeof(id), "filament_circulation_fan_run_minutes", "circulation_fan_run_minutes");
  snprintf(topic, sizeof(topic), "homeassistant/number/%s/config", id);
  JsonDocument doc;
  doc["name"] = "Fan Manual Run Time";
  doc["unique_id"] = id;
  doc["state_topic"] = topicFanRun;
  doc["command_topic"] = topicFanRunSet;
  doc["min"] = 0;
  doc["max"] = FAN_MAX_RUN_MIN;
  doc["step"] = 5;
  doc["unit_of_measurement"] = "min";
  doc["mode"] = "box";
  doc["icon"] = "mdi:timer-outline";
  doc["entity_category"] = "config";
  sendDiscovery(topic, doc);
}

// Text sensor per lane: the spool tag's UID, or "empty". A disabled lane gets
// an empty retained config instead, which removes its entity from HA.
void publishTagDiscovery(int lane)
{
  char id[64], name[40], stateTopic[80], attrTopic[80], topic[128];
  snprintf(id, sizeof(id), "%s_lane_%d_tag", cfg.deviceId, lane + 1);
  snprintf(name, sizeof(name), "Lane %d Spool Tag", lane + 1);
  laneTopic(lane, stateTopic, sizeof(stateTopic));
  laneAttrTopic(lane, attrTopic, sizeof(attrTopic));
  snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/config", id);
  if (!laneActive(lane))
  {
    mqtt.publish(topic, (const uint8_t *)"", 0, true);
    return;
  }

  JsonDocument doc;
  doc["name"] = name;
  doc["unique_id"] = id;
  doc["state_topic"] = stateTopic;
  doc["json_attributes_topic"] = attrTopic;
  doc["icon"] = "mdi:nfc-variant";
  sendDiscovery(topic, doc);
}

void publishTagState(int lane)
{
  if (!laneActive(lane) || !laneKnown(lane) || !mqtt.connected())
    return; // disabled, not settled yet, or offline (re-sent on reconnect)
  char stateTopic[80];
  laneTopic(lane, stateTopic, sizeof(stateTopic));
  const char *uid = laneReported(lane);

  // Attributes first, so automations triggered by the state change see them
  char attrTopic[80], payload[940]; // fits the 1024-byte MQTT buffer with the topic
  laneAttrTopic(lane, attrTopic, sizeof(attrTopic));
  JsonDocument attrs;
  JsonObject o = attrs.to<JsonObject>();
  laneTagInfo(lane, o); // decoded spool data, when there is any
  spoolSyncJson(lane, o); // matching Spoolman spool, when configured
  o["tag_uid"] = uid;
  serializeJson(attrs, payload, sizeof(payload));
  mqtt.publish(attrTopic, payload, true);

  mqtt.publish(stateTopic, uid[0] ? uid : "empty", true);
}

void publishFanState()
{
  if (cfg.fanPin < 0 || fanDuty < 0 || !mqtt.connected())
    return; // no fan, not decided yet, or offline (re-sent on reconnect)
  mqtt.publish(topicFanState, fanRunning ? "ON" : "OFF", true);
  mqtt.publish(topicFanPct, String(fanSpeedPct).c_str(), true);
}

void publishRunTime()
{
  if (cfg.fanPin >= 0 && mqtt.connected())
    mqtt.publish(topicFanRun, String(fanRunMin).c_str(), true);
}

void registerSensors()
{
  if (cfg.fanPin >= 0)
  {
    publishFanDiscovery();
    publishRunTimeDiscovery();
  }
  if (cfg.dhtHighPin >= 0)
  {
    publishDiscovery("filament_high_temp", "high_temp", "Filament High Temperature",
                     topicHighTemp, "°C", "temperature");
    publishDiscovery("filament_high_humidity", "high_humidity", "Filament High Humidity",
                     topicHighHum, "%", "humidity");
  }
  if (cfg.dhtLowPin >= 0)
  {
    publishDiscovery("filament_low_temp", "low_temp", "Filament Low Temperature",
                     topicLowTemp, "°C", "temperature");
    publishDiscovery("filament_low_humidity", "low_humidity", "Filament Low Humidity",
                     topicLowHum, "%", "humidity");
  }
  for (int i = 0; i < MAX_LANES; i++)
    publishTagDiscovery(i);
  publishFanState(); // catch HA up if anything changed while we were offline
  publishRunTime();
  for (int i = 0; i < MAX_LANES; i++)
    publishTagState(i);
  logEvent("HA discovery published");
}

// ── Fan commands from HA ──────────────────────────────────
void startManualRun()
{
  fanMode = FAN_MANUAL_ON;
  manualStart = millis();
  if (fanRunMin > 0)
    logEvent("Fan on from HA for %d min", fanRunMin);
  else
    logEvent("Fan on from HA until turned off");
}

void stopFan()
{
  // Stopping mid-schedule skips the rest of that run, not future ones
  fanMode = scheduledOn(millis()) ? FAN_MANUAL_OFF : FAN_AUTO;
  logEvent("Fan off from HA");
}

void onMqttMessage(char *topic, byte *payload, unsigned int length)
{
  char msg[16];
  length = min(length, (unsigned int)sizeof(msg) - 1);
  memcpy(msg, payload, length);
  msg[length] = '\0';

  if (strcmp(topic, topicFanSet) == 0)
  {
    if (strcmp(msg, "ON") == 0)
      startManualRun();
    else if (strcmp(msg, "OFF") == 0)
      stopFan();
  }
  else if (strcmp(topic, topicFanPctSet) == 0)
  {
    int pct = atoi(msg);
    if (pct <= 0)
    {
      stopFan(); // HA sends 0% to mean off
      return;
    }
    fanSpeedPct = constrain(pct, FAN_MIN_PCT, 100);
    prefs.putInt("speed", fanSpeedPct);
    logEvent("Fan speed set to %d%%", fanSpeedPct);
    if (!fanRunning)
      startManualRun(); // moving the slider on an idle fan turns it on
    publishFanState();  // report the clamped value back
  }
  else if (strcmp(topic, topicFanRunSet) == 0)
  {
    fanRunMin = constrain(atoi(msg), 0, FAN_MAX_RUN_MIN);
    prefs.putInt("run_min", fanRunMin);
    logEvent("Manual run time set to %d min", fanRunMin);
    publishRunTime();
  }
}

// ── WiFi ──────────────────────────────────────────────────
// Setup hotspot: comes up when no WiFi is saved, when the saved network can't
// be reached at boot, or after WiFi has been down for a while. It goes away
// once WiFi is back and nobody is connected to it.
void startSetupAp()
{
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(apSsid, sizeof(apSsid), "StorOn-Setup-%02X%02X", mac[4], mac[5]);
  WiFi.setAutoReconnect(false); // background STA scans make the hotspot drop clients
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSsid, AP_PASSWORD);
  apActive = true;
  lastStaRetry = millis();
  logEvent("Setup hotspot \"%s\" (password %s) at http://%s/", apSsid, AP_PASSWORD,
           WiFi.softAPIP().toString().c_str());
}

void stopSetupAp()
{
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  apActive = false;
  logEvent("Setup hotspot off");
}

// Log why a WiFi connection attempt failed (once per distinct reason, so a
// retry loop doesn't flood the log). Common codes: 201 network not found,
// 202/15/204 wrong password, 2/4/8 router dropped us.
void onWifiDisconnected(WiFiEvent_t, WiFiEventInfo_t info)
{
  static uint8_t lastReason = 0;
  uint8_t reason = info.wifi_sta_disconnected.reason;
  if (reason == lastReason)
    return;
  lastReason = reason;
  const char *hint = reason == 201                                      ? "network not found"
                     : (reason == 202 || reason == 15 || reason == 204) ? "check the WiFi password"
                                                                        : "";
  logEvent("WiFi disconnected, reason %u %s", reason, hint);
}

void setupNetwork()
{
  String host = String(cfg.deviceId);
  host.replace('_', '-');
  WiFi.onEvent(onWifiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.setHostname(host.c_str()); // must come before WiFi.mode() on this core
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (cfg.wifiSsid[0])
  {
    logEvent("Connecting to WiFi: %s", cfg.wifiSsid);
    WiFi.begin(cfg.wifiSsid, cfg.wifiPass);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_BOOT_WAIT_MS)
      delay(250);
  }
  if (WiFi.status() != WL_CONNECTED)
  {
    logEvent(cfg.wifiSsid[0] ? "WiFi not reachable" : "No WiFi saved");
    wifiLostAt = millis();
    startSetupAp();
  }
}

void updateNetwork(unsigned long now)
{
  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected)
  {
    if (!wifiWasConnected)
    {
      wifiConnectedAt = now;
      wifiLostAt = 0;
      logEvent("WiFi connected to %s, IP %s", cfg.wifiSsid, WiFi.localIP().toString().c_str());
      configTime(0, 0, "pool.ntp.org", "time.nist.gov"); // UTC clock, for lane_data scan_time
      if (!mdnsStarted)
      {
        String host = String(cfg.deviceId);
        host.replace('_', '-');
        if (MDNS.begin(host.c_str()))
        {
          MDNS.addService("http", "tcp", 80);
          mdnsStarted = true;
          logEvent("Web UI at http://%s.local/", host.c_str());
        }
      }
    }
    if (apActive && now - wifiConnectedAt > 30000 && WiFi.softAPgetStationNum() == 0)
      stopSetupAp();
  }
  else
  {
    if (wifiWasConnected)
    {
      wifiLostAt = now;
      logEvent("WiFi lost");
    }
    if (!apActive && wifiLostAt && now - wifiLostAt > WIFI_LOST_AP_MS)
      startSetupAp();
    // Retry the saved network only while nobody is using the hotspot, since
    // joining it moves the radio to the router's channel and drops clients
    if (apActive && cfg.wifiSsid[0] && WiFi.softAPgetStationNum() == 0 &&
        now - lastStaRetry > AP_STA_RETRY_MS)
    {
      lastStaRetry = now;
      logEvent("Retrying WiFi: %s", cfg.wifiSsid);
      WiFi.begin(cfg.wifiSsid, cfg.wifiPass);
    }
  }
  wifiWasConnected = connected;
}

// ── MQTT ──────────────────────────────────────────────────
// Non-blocking: one connection attempt every MQTT_RETRY_MS, so the web UI
// keeps working while the broker is down or misconfigured.
void updateMqtt(unsigned long now)
{
  if (WiFi.status() != WL_CONNECTED || cfg.mqttHost[0] == '\0')
    return;
  if (mqtt.connected())
  {
    mqtt.loop();
    return;
  }
  if (lastMqttAttempt && now - lastMqttAttempt < MQTT_RETRY_MS)
    return;
  lastMqttAttempt = now;
  // Last will: the broker marks us offline if we drop without saying goodbye
  if (mqtt.connect(cfg.deviceId, cfg.mqttUser, cfg.mqttPass, topicStatus, 0, true, "offline"))
  {
    logEvent("MQTT connected to %s:%u", cfg.mqttHost, cfg.mqttPort);
    mqtt.publish(topicStatus, "online", true);
    if (cfg.fanPin >= 0)
    {
      mqtt.subscribe(topicFanSet);
      mqtt.subscribe(topicFanPctSet);
      mqtt.subscribe(topicFanRunSet);
    }
    registerSensors();
  }
  else
    logEvent("MQTT connect to %s:%u failed (rc=%d), retrying in %ds", cfg.mqttHost,
             cfg.mqttPort, mqtt.state(), MQTT_RETRY_MS / 1000);
}

// ── Sensor reading ────────────────────────────────────────
static void readOne(DHT *dht, Reading &r, const char *label, const char *tTopic,
                    const char *hTopic)
{
  if (!dht)
    return;
  float t = dht->readTemperature();
  float h = dht->readHumidity();
  bool ok = !isnan(t) && !isnan(h);
  if (ok)
  {
    r.temp = t;
    r.hum = h;
    r.at = millis();
    if (mqtt.connected())
    {
      mqtt.publish(tTopic, String(t, 1).c_str());
      mqtt.publish(hTopic, String(h, 1).c_str());
    }
    if (!r.ok && r.tried)
      logEvent("Filament %s sensor reading again", label);
    Serial.printf("%s - Temp: %.1f C  Humidity: %.1f%%\n", label, t, h);
  }
  else if (r.ok || !r.tried)
    // Log the change (or the first failure after boot), not every miss
    logEvent("Filament %s sensor read failed: check its wiring and power", label);
  r.ok = ok;
  r.tried = true;
}

void readAndPublish()
{
  readOne(dhtHigh, readingHigh, "High", topicHighTemp, topicHighHum);
  readOne(dhtLow, readingLow, "Low", topicLowTemp, topicLowHum);
}

// ── Circulation fan ───────────────────────────────────────
void updateFan(unsigned long now)
{
  if (cfg.fanPin < 0)
    return;
  bool scheduled = scheduledOn(now);

  if (fanMode == FAN_MANUAL_ON && fanRunMin > 0 &&
      now - manualStart >= (unsigned long)fanRunMin * 60000UL)
  {
    fanMode = FAN_AUTO;
    logEvent("Manual fan run finished, back to schedule");
  }
  if (fanMode == FAN_MANUAL_OFF && !scheduled)
    fanMode = FAN_AUTO;

  bool on = fanMode == FAN_MANUAL_ON || (fanMode == FAN_AUTO && scheduled);
  int duty = on ? fanSpeedPct * 255 / 100 : 0;
  if (duty == fanDuty)
    return;
  bool changed = on != fanRunning || fanDuty < 0;
  fanDuty = duty;
  fanRunning = on;
  analogWrite(cfg.fanPin, duty);
  if (changed)
    logEvent("Circulation fan %s", on ? "on" : "off");
  publishFanState();
}

// ── Status LED ────────────────────────────────────────────
// Double-pulse heartbeat: green while idle, blue and twice as fast while the
// fan runs, amber while WiFi or MQTT is down. It freezes if the loop hangs.
void updateStatusLed(unsigned long now)
{
  if (cfg.ledPin < 0)
    return;
  static int lastState = -1;
  bool online = WiFi.status() == WL_CONNECTED && mqtt.connected();
  unsigned long period = fanRunning ? LED_FAN_PERIOD_MS : LED_IDLE_PERIOD_MS;
  unsigned long phase = now % period;
  bool lit = phase < 80 || (phase >= 200 && phase < 280);
  int state = !lit ? 0 : !online ? 3 : fanRunning ? 2 : 1; // 0 off, 1 green, 2 blue, 3 amber
  if (state == lastState)
    return; // only push to the LED when it changes
  lastState = state;
  uint8_t b = cfg.ledBrightness;
  if (state == 1)
    neopixelWrite(cfg.ledPin, 0, b, 0);
  else if (state == 2)
    neopixelWrite(cfg.ledPin, 0, 0, b);
  else if (state == 3)
    neopixelWrite(cfg.ledPin, b, b / 2, 0);
  else
    neopixelWrite(cfg.ledPin, 0, 0, 0);
}

// ── BOOT button: hold for a factory reset ─────────────────
void updateBootButton(unsigned long now)
{
  static unsigned long pressedAt = 0;
  if (digitalRead(BOOT_BUTTON_PIN) == LOW)
  {
    if (!pressedAt)
      pressedAt = now;
    else if (now - pressedAt >= BOOT_HOLD_MS)
    {
      logEvent("BOOT held %d s: factory reset", BOOT_HOLD_MS / 1000);
      factoryReset();
    }
  }
  else
    pressedAt = 0;
}

// ── Status for the web UI ─────────────────────────────────
static void readingJson(JsonObject o, DHT *dht, const Reading &r, unsigned long now)
{
  o["enabled"] = dht != nullptr;
  o["ok"] = r.ok;
  if (r.ok)
  {
    o["temp"] = r.temp;
    o["hum"] = r.hum;
    o["age_s"] = (now - r.at) / 1000;
  }
}

void appStatusJson(JsonDocument &doc)
{
  unsigned long now = millis();
  JsonObject d = doc["device"].to<JsonObject>();
  d["name"] = cfg.deviceName;
  d["id"] = cfg.deviceId;
  d["fw"] = FW_VERSION;
  d["uptime_s"] = now / 1000;
  d["heap"] = ESP.getFreeHeap();

  JsonObject w = doc["wifi"].to<JsonObject>();
  bool connected = WiFi.status() == WL_CONNECTED;
  w["connected"] = connected;
  w["ssid"] = cfg.wifiSsid;
  if (connected)
  {
    w["ip"] = WiFi.localIP().toString();
    w["rssi"] = WiFi.RSSI();
  }
  w["ap_active"] = apActive;
  if (apActive)
    w["ap_ssid"] = apSsid;

  JsonObject m = doc["mqtt"].to<JsonObject>();
  m["host"] = cfg.mqttHost;
  m["port"] = cfg.mqttPort;
  m["connected"] = mqtt.connected();
  m["state"] = mqtt.state();
  moonrakerStatusJson(doc["printer"].to<JsonObject>());
  gpio_drive_cap_t cap; // SPI wire drive strength, for the Reader check page
  if (gpio_get_drive_capability((gpio_num_t)cfg.spiSck, &cap) == ESP_OK)
    doc["spi_pin_drive"] = (int)cap;
  doc["spoolman"] = cfg.spoolmanUrl[0] != '\0';
  doc["spoolman_url"] = cfg.spoolmanUrl;

  JsonObject s = doc["sensors"].to<JsonObject>();
  readingJson(s["high"].to<JsonObject>(), dhtHigh, readingHigh, now);
  readingJson(s["low"].to<JsonObject>(), dhtLow, readingLow, now);

  JsonObject f = doc["fan"].to<JsonObject>();
  f["enabled"] = cfg.fanPin >= 0;
  f["running"] = fanRunning;
  f["pct"] = fanSpeedPct;

  rfidStatusJson(doc["lanes"].to<JsonArray>());
  logToJson(doc["log"].to<JsonArray>());
}

// ── Setup / Loop ──────────────────────────────────────────
void setup()
{
  Serial.begin(115200);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  loadSettings();
  buildTopics();
  logEvent("StorOn monitor %s, device \"%s\" (%s)", FW_VERSION, cfg.deviceName, cfg.deviceId);

  prefs.begin("fan", false);
  fanSpeedPct = constrain(prefs.getInt("speed", FAN_SPEED_PCT), FAN_MIN_PCT, 100);
  fanRunMin = constrain(prefs.getInt("run_min", FAN_MANUAL_RUN_MIN), 0, FAN_MAX_RUN_MIN);
  Serial.printf("Fan speed %d%%, manual run time %d min\n", fanSpeedPct, fanRunMin);

  if (cfg.dhtHighPin >= 0)
  {
    dhtHigh = new DHT(cfg.dhtHighPin, DHT22);
    dhtHigh->begin();
  }
  if (cfg.dhtLowPin >= 0)
  {
    dhtLow = new DHT(cfg.dhtLowPin, DHT22);
    dhtLow->begin();
  }
  setupRfid();

  mqtt.setServer(cfg.mqttHost, cfg.mqttPort);
  mqtt.setBufferSize(1024);
  mqtt.setSocketTimeout(5);
  mqtt.setCallback(onMqttMessage);
  setupNetwork();
  setupWeb();
}

void loop()
{
  unsigned long now = millis();
  updateNetwork(now);
  updateMqtt(now);
  updateWeb();
  updateFan(now);
  updateStatusLed(now);
  updateRfid(now);
  updateSpoolSync(now);
  updateMoonraker(now);
  updateBootButton(now);
  if (now - lastReadTime >= cfg.sensorIntervalS * 1000UL)
  {
    lastReadTime = now;
    readAndPublish();
  }
  if (rebootAt && (long)(now - rebootAt) >= 0)
    ESP.restart();
}
