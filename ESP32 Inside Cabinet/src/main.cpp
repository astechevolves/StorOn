#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include "../include/config.h"

// ── Pin config ────────────────────────────────────────────
#define DHT_PIN_HIGH 4
#define DHT_PIN_LOW 5
#define DHT_TYPE DHT22

// ── Timing ────────────────────────────────────────────────
#define READ_INTERVAL_MS 30000 // read sensors every 30 seconds

// ── MQTT topic roots ──────────────────────────────────────
#define DEVICE_ID "filament_monitor"
#define TOPIC_HIGH_TEMP DEVICE_ID "/filament_high/temperature"
#define TOPIC_HIGH_HUMIDITY DEVICE_ID "/filament_high/humidity"
#define TOPIC_LOW_TEMP DEVICE_ID "/filament_low/temperature"
#define TOPIC_LOW_HUMIDITY DEVICE_ID "/filament_low/humidity"

// ── Objects ───────────────────────────────────────────────
DHT dhtHigh(DHT_PIN_HIGH, DHT_TYPE);
DHT dhtLow(DHT_PIN_LOW, DHT_TYPE);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

unsigned long lastReadTime = 0;

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

  JsonObject device = doc["device"].to<JsonObject>();
  device["identifiers"][0] = DEVICE_ID;
  device["name"] = "Filament Monitor";
  device["model"] = "ESP32-S3 + DHT22";
  device["manufacturer"] = "DIY";

  char payload[512];
  serializeJson(doc, payload);

  Serial.printf("Discovery topic: %s\n", topic);
  Serial.printf("Discovery payload: %s\n", payload);

  mqtt.publish(topic, payload, true);
}

void registerSensors()
{
  publishDiscovery("filament_high_temp", "Filament High Temperature",
                   TOPIC_HIGH_TEMP, "°C", "temperature");
  publishDiscovery("filament_high_humidity", "Filament High Humidity",
                   TOPIC_HIGH_HUMIDITY, "%", "humidity");
  publishDiscovery("filament_low_temp", "Filament Low Temperature",
                   TOPIC_LOW_TEMP, "°C", "temperature");
  publishDiscovery("filament_low_humidity", "Filament Low Humidity",
                   TOPIC_LOW_HUMIDITY, "%", "humidity");
  Serial.println("HA discovery published.");
}

// ── MQTT ──────────────────────────────────────────────────
void connectMqtt()
{
  while (!mqtt.connected())
  {
    Serial.print("Connecting to MQTT...");
    if (mqtt.connect(DEVICE_ID, MQTT_USER, MQTT_PASSWORD))
    {
      Serial.println("connected.");
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

// ── Setup / Loop ──────────────────────────────────────────
void setup()
{
  Serial.begin(115200);
  dhtHigh.begin();
  dhtLow.begin();
  connectWifi();
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setBufferSize(1024);
  connectMqtt();
}

void loop()
{
  connectWifi();
  if (!mqtt.connected())
    connectMqtt();
  mqtt.loop();

  unsigned long now = millis();
  if (now - lastReadTime >= READ_INTERVAL_MS)
  {
    lastReadTime = now;
    readAndPublish();
  }
}