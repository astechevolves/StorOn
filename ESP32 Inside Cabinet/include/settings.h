#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Everything a user can change from the web UI. Saved to flash (NVS) and
// loaded at boot; pin and network changes take effect after a reboot.

// Fixed lane slots. A lane keeps its number when others are disabled, so
// Home Assistant entities stay stable. Six covers common 6-tool changers.
#define MAX_LANES 6

struct Settings
{
  // Device
  char deviceName[32]; // shown in Home Assistant
  char deviceId[32];   // MQTT topic prefix and HA identifier: a-z, 0-9, _
  char adminPass[33];  // web UI password for user "admin"; empty = no password

  // Network
  char wifiSsid[33];
  char wifiPass[65];
  char mqttHost[64];
  uint16_t mqttPort;
  char mqttUser[33];
  char mqttPass[65];
  char spoolmanUrl[96]; // e.g. http://192.168.1.10:7912 ; empty = not used
  char spoolmanLocation[32]; // spools in lane N get location "<this> Lane N"; empty = don't set
  char spoolmanAwayLocation[32]; // location for a spool that leaves a lane; empty = clear it

  // Pins (-1 = not used)
  int8_t dhtHighPin;
  int8_t dhtLowPin;
  int8_t fanPin;
  int8_t ledPin;
  int8_t spiSck;
  int8_t spiMiso;
  int8_t spiMosi;
  bool laneEnabled[MAX_LANES]; // RC522 reader installed in this lane
  int8_t ssPins[MAX_LANES];    // SS (board "SDA") pin per lane
  int8_t laneTool[MAX_LANES];  // printer tool this lane feeds (T number), -1 = none
  char moonrakerUrl[96];       // e.g. http://192.168.1.20 ; empty = no printer link
  // G-code the Load / Unload buttons send to the printer. Placeholders:
  // {tool} {lane} {temp} {material}; a word whose placeholder has no value is
  // left out. Empty = no button.
  char loadCmd[96];
  char unloadCmd[96];

  // Timing
  uint16_t sensorIntervalS; // DHT22 read interval
  uint16_t fanCycleMin;     // start a circulation run this often
  uint16_t fanOnMin;        // each run lasts this long
  uint16_t rfidCycleMs;     // time to poll every lane once
  uint16_t laneReleaseMin;  // release a lane whose tag is unseen this long; 0 = never
  uint8_t ledBrightness;    // 0-255
};

extern Settings cfg;

void loadSettings();
// Validates and saves settings from the web UI. On failure, nothing changes
// and err says why.
bool applySettings(JsonDocument &doc, String &err);
// Never includes passwords, only whether each one is set
void settingsToJson(JsonDocument &doc);
void factoryReset(); // erases saved settings and reboots
bool pinAllowed(int pin);
