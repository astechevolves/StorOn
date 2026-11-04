#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "../include/config.h"

// ── Pin config ────────────────────────────────────────────
#define DHT_PIN_HIGH 4
#define DHT_PIN_LOW 5
#define DHT_TYPE DHT22
// Fan wiring (MOSFET module channel 1): DC+ (24V) -> 1.5k 1/4W -> PWM1+,
// GND1 -> 2N3904 collector, emitter -> ESP32 GND (shared with 24V DC-),
// GPIO 14 -> 1k -> base. The module's input LEDs need more than 5V.
#define FAN_PIN 14
#define STATUS_LED_PIN 48 // onboard WS2812 RGB LED (GPIO 38 on DevKitC-1 v1.1 boards)

// ── Timing ────────────────────────────────────────────────
#define READ_INTERVAL_MS 30000          // read sensors every 30 seconds
#define FAN_CYCLE_MS (60UL * 60 * 1000) // start a circulation run every 60 minutes
#define FAN_ON_MS (5UL * 60 * 1000)     // each run lasts 5 minutes
#define LED_IDLE_PERIOD_MS 2000         // heartbeat every 2 s while the fan is off
#define LED_FAN_PERIOD_MS 1000          // heartbeat every 1 s while the fan runs
#define LED_BRIGHTNESS 5                // 0-255; the onboard LED is very bright

// ── Fan defaults (HA can change these; they're saved to flash) ──
#define FAN_SPEED_PCT 60      // default speed for scheduled and manual runs
#define FAN_MIN_PCT 30        // 2-wire fans may stall below ~30-40%
#define FAN_MANUAL_RUN_MIN 30 // a manual "on" from HA runs this long (0 = until turned off)
#define FAN_MAX_RUN_MIN 240

// ── MQTT topics ───────────────────────────────────────────
#define DEVICE_ID "filament_monitor"
#define TOPIC_STATUS DEVICE_ID "/status" // "online" / "offline" (last will)
#define TOPIC_HIGH_TEMP DEVICE_ID "/filament_high/temperature"
#define TOPIC_HIGH_HUMIDITY DEVICE_ID "/filament_high/humidity"
#define TOPIC_LOW_TEMP DEVICE_ID "/filament_low/temperature"
#define TOPIC_LOW_HUMIDITY DEVICE_ID "/filament_low/humidity"
#define TOPIC_FAN_STATE DEVICE_ID "/circulation_fan/state" // "ON" / "OFF"
#define TOPIC_FAN_SET DEVICE_ID "/circulation_fan/set"
#define TOPIC_FAN_PCT DEVICE_ID "/circulation_fan/percentage"
#define TOPIC_FAN_PCT_SET DEVICE_ID "/circulation_fan/percentage/set"
#define TOPIC_FAN_RUN DEVICE_ID "/circulation_fan/run_minutes"
#define TOPIC_FAN_RUN_SET DEVICE_ID "/circulation_fan/run_minutes/set"

// ── Objects ───────────────────────────────────────────────
DHT dhtHigh(DHT_PIN_HIGH, DHT_TYPE);
DHT dhtLow(DHT_PIN_LOW, DHT_TYPE);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
Preferences prefs;

unsigned long lastReadTime = 0;

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

bool scheduledOn(unsigned long now) { return (now % FAN_CYCLE_MS) < FAN_ON_MS; }

// ── WiFi ──────────────────────────────────────────────────
void connectWifi()
{
  if (WiFi.status() == WL_CONNECTED)
    return;
  Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nWiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());
}

// ── HA Discovery ──────────────────────────────────────────
// Fields every entity shares: availability and the device it belongs to
void addCommon(JsonDocument &doc)
{
  doc["availability_topic"] = TOPIC_STATUS;
  JsonObject device = doc["device"].to<JsonObject>();
  device["identifiers"][0] = DEVICE_ID;
  device["name"] = "Filament Monitor";
  device["model"] = "ESP32-S3 + DHT22";
  device["manufacturer"] = "DIY";
}

void sendDiscovery(const char *topic, JsonDocument &doc)
{
  addCommon(doc);
  char payload[900]; // must stay under the MQTT buffer (1024) minus the topic
  if (measureJson(doc) >= sizeof(payload))
  {
    Serial.printf("Discovery payload too big for %s\n", topic);
    return;
  }
  serializeJson(doc, payload);

  Serial.printf("Discovery topic: %s\n", topic);
  Serial.printf("Discovery payload: %s\n", payload);

  mqtt.publish(topic, payload, true);
}

void publishDiscovery(const char *sensorId, const char *sensorName,
                      const char *stateTopic, const char *unit,
                      const char *deviceClass)
{
  char topic[128];
  snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/config", sensorId);

  JsonDocument doc;
  doc["name"] = sensorName;
  doc["state_topic"] = stateTopic;
  doc["unit_of_measurement"] = unit;
  doc["device_class"] = deviceClass;
  doc["state_class"] = "measurement";
  doc["unique_id"] = sensorId;
  sendDiscovery(topic, doc);
}

// Fan entity: on/off plus a 1-100% speed slider
void publishFanDiscovery()
{
  JsonDocument doc;
  doc["name"] = "Circulation Fan";
  doc["unique_id"] = "filament_circulation_fan";
  doc["state_topic"] = TOPIC_FAN_STATE;
  doc["command_topic"] = TOPIC_FAN_SET;
  doc["percentage_state_topic"] = TOPIC_FAN_PCT;
  doc["percentage_command_topic"] = TOPIC_FAN_PCT_SET;
  doc["speed_range_min"] = 1;
  doc["speed_range_max"] = 100;
  sendDiscovery("homeassistant/fan/filament_circulation_fan/config", doc);
}

// Number entity: how long a manual "on" from HA lasts
void publishRunTimeDiscovery()
{
  JsonDocument doc;
  doc["name"] = "Fan Manual Run Time";
  doc["unique_id"] = "filament_circulation_fan_run_minutes";
  doc["state_topic"] = TOPIC_FAN_RUN;
  doc["command_topic"] = TOPIC_FAN_RUN_SET;
  doc["min"] = 0;
  doc["max"] = FAN_MAX_RUN_MIN;
  doc["step"] = 5;
  doc["unit_of_measurement"] = "min";
  doc["mode"] = "box";
  doc["icon"] = "mdi:timer-outline";
  doc["entity_category"] = "config";
  sendDiscovery("homeassistant/number/filament_circulation_fan_run_minutes/config", doc);
}

void publishFanState()
{
  if (fanDuty < 0 || !mqtt.connected())
    return; // not decided yet, or offline (re-sent on reconnect)
  mqtt.publish(TOPIC_FAN_STATE, fanRunning ? "ON" : "OFF", true);
  mqtt.publish(TOPIC_FAN_PCT, String(fanSpeedPct).c_str(), true);
}

void publishRunTime()
{
  if (mqtt.connected())
    mqtt.publish(TOPIC_FAN_RUN, String(fanRunMin).c_str(), true);
}

void registerSensors()
{
  publishFanDiscovery();
  publishRunTimeDiscovery();
  publishDiscovery("filament_high_temp", "Filament High Temperature",
                   TOPIC_HIGH_TEMP, "°C", "temperature");
  publishDiscovery("filament_high_humidity", "Filament High Humidity",
                   TOPIC_HIGH_HUMIDITY, "%", "humidity");
  publishDiscovery("filament_low_temp", "Filament Low Temperature",
                   TOPIC_LOW_TEMP, "°C", "temperature");
  publishDiscovery("filament_low_humidity", "Filament Low Humidity",
                   TOPIC_LOW_HUMIDITY, "%", "humidity");
  publishFanState(); // catch HA up if anything changed while we were offline
  publishRunTime();
  Serial.println("HA discovery published.");
}

// ── Fan commands from HA ──────────────────────────────────
void startManualRun()
{
  fanMode = FAN_MANUAL_ON;
  manualStart = millis();
  if (fanRunMin > 0)
    Serial.printf("Fan on from HA for %d min\n", fanRunMin);
  else
    Serial.println("Fan on from HA until turned off");
}

void stopFan()
{
  // Stopping mid-schedule skips the rest of that run, not future ones
  fanMode = scheduledOn(millis()) ? FAN_MANUAL_OFF : FAN_AUTO;
  Serial.println("Fan off from HA");
}

void onMqttMessage(char *topic, byte *payload, unsigned int length)
{
  char msg[16];
  length = min(length, (unsigned int)sizeof(msg) - 1);
  memcpy(msg, payload, length);
  msg[length] = '\0';

  if (strcmp(topic, TOPIC_FAN_SET) == 0)
  {
    if (strcmp(msg, "ON") == 0)
      startManualRun();
    else if (strcmp(msg, "OFF") == 0)
      stopFan();
  }
  else if (strcmp(topic, TOPIC_FAN_PCT_SET) == 0)
  {
    int pct = atoi(msg);
    if (pct <= 0)
    {
      stopFan(); // HA sends 0% to mean off
      return;
    }
    fanSpeedPct = constrain(pct, FAN_MIN_PCT, 100);
    prefs.putInt("speed", fanSpeedPct);
    Serial.printf("Fan speed set to %d%%\n", fanSpeedPct);
    if (!fanRunning)
      startManualRun(); // moving the slider on an idle fan turns it on
    publishFanState();  // report the clamped value back
  }
  else if (strcmp(topic, TOPIC_FAN_RUN_SET) == 0)
  {
    fanRunMin = constrain(atoi(msg), 0, FAN_MAX_RUN_MIN);
    prefs.putInt("run_min", fanRunMin);
    Serial.printf("Manual run time set to %d min\n", fanRunMin);
    publishRunTime();
  }
}

// ── MQTT ──────────────────────────────────────────────────
void connectMqtt()
{
  while (!mqtt.connected())
  {
    Serial.print("Connecting to MQTT...");
    // Last will: the broker marks us offline if we drop without saying goodbye
    if (mqtt.connect(DEVICE_ID, MQTT_USER, MQTT_PASSWORD,
                     TOPIC_STATUS, 0, true, "offline"))
    {
      Serial.println("connected.");
      mqtt.publish(TOPIC_STATUS, "online", true);
      mqtt.subscribe(TOPIC_FAN_SET);
      mqtt.subscribe(TOPIC_FAN_PCT_SET);
      mqtt.subscribe(TOPIC_FAN_RUN_SET);
      registerSensors();
    }
    else
    {
      Serial.printf("failed (rc=%d), retrying in 5s\n", mqtt.state());
      delay(5000);
    }
  }
}

// ── Sensor reading ────────────────────────────────────────
void readAndPublish()
{
  float highTemp = dhtHigh.readTemperature();
  float highHum = dhtHigh.readHumidity();
  float lowTemp = dhtLow.readTemperature();
  float lowHum = dhtLow.readHumidity();

  if (!isnan(highTemp) && !isnan(highHum))
  {
    mqtt.publish(TOPIC_HIGH_TEMP, String(highTemp, 1).c_str());
    mqtt.publish(TOPIC_HIGH_HUMIDITY, String(highHum, 1).c_str());
    Serial.printf("High  — Temp: %.1f°C  Humidity: %.1f%%\n", highTemp, highHum);
  }
  else
  {
    Serial.println("Filament High sensor read failed.");
  }

  if (!isnan(lowTemp) && !isnan(lowHum))
  {
    mqtt.publish(TOPIC_LOW_TEMP, String(lowTemp, 1).c_str());
    mqtt.publish(TOPIC_LOW_HUMIDITY, String(lowHum, 1).c_str());
    Serial.printf("Low   — Temp: %.1f°C  Humidity: %.1f%%\n", lowTemp, lowHum);
  }
  else
  {
    Serial.println("Filament Low sensor read failed.");
  }
}

// ── Circulation fan ───────────────────────────────────────
void updateFan(unsigned long now)
{
  bool scheduled = scheduledOn(now);

  if (fanMode == FAN_MANUAL_ON && fanRunMin > 0 &&
      now - manualStart >= (unsigned long)fanRunMin * 60000UL)
  {
    fanMode = FAN_AUTO;
    Serial.println("Manual fan run finished, back to schedule");
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
  analogWrite(FAN_PIN, duty);
  if (changed)
    Serial.printf("Circulation fan %s\n", on ? "on" : "off");
  publishFanState();
}

// ── Status LED ────────────────────────────────────────────
// Double-pulse heartbeat: green while idle, blue and twice as fast while
// the fan runs. It freezes if the loop is stuck reconnecting WiFi/MQTT.
void updateStatusLed(unsigned long now)
{
  static int lastState = -1;
  unsigned long period = fanRunning ? LED_FAN_PERIOD_MS : LED_IDLE_PERIOD_MS;
  unsigned long phase = now % period;
  bool lit = phase < 80 || (phase >= 200 && phase < 280);
  int state = lit ? (fanRunning ? 2 : 1) : 0; // 0 off, 1 green, 2 blue
  if (state == lastState)
    return; // only push to the LED when it changes
  lastState = state;
  if (state == 1)
    neopixelWrite(STATUS_LED_PIN, 0, LED_BRIGHTNESS, 0);
  else if (state == 2)
    neopixelWrite(STATUS_LED_PIN, 0, 0, LED_BRIGHTNESS);
  else
    neopixelWrite(STATUS_LED_PIN, 0, 0, 0);
}

// ── Setup / Loop ──────────────────────────────────────────
void setup()
{
  Serial.begin(115200);

  prefs.begin("fan", false);
  fanSpeedPct = constrain(prefs.getInt("speed", FAN_SPEED_PCT), FAN_MIN_PCT, 100);
  fanRunMin = constrain(prefs.getInt("run_min", FAN_MANUAL_RUN_MIN), 0, FAN_MAX_RUN_MIN);
  Serial.printf("Fan speed %d%%, manual run time %d min\n", fanSpeedPct, fanRunMin);

  dhtHigh.begin();
  dhtLow.begin();
  connectWifi();
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setBufferSize(1024);
  mqtt.setCallback(onMqttMessage);
  connectMqtt();
}

void loop()
{
  connectWifi();
  if (!mqtt.connected())
    connectMqtt();
  mqtt.loop();

  unsigned long now = millis();
  updateFan(now);
  updateStatusLed(now);
  if (now - lastReadTime >= READ_INTERVAL_MS)
  {
    lastReadTime = now;
    readAndPublish();
  }
}
