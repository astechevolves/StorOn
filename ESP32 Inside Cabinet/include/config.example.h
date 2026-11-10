#pragma once

// OPTIONAL. Everything here can be set from the web UI instead.
//
// Copy this file to config.h (git-ignored) to pre-fill WiFi and MQTT, so a
// freshly flashed board connects straight away without the setup hotspot.
// Settings saved from the web UI always take priority over these values.

#define WIFI_SSID       "your_wifi_name"
#define WIFI_PASSWORD   "your_wifi_password"

#define MQTT_BROKER     "192.168.x.x"   // your HA VM's IP
#define MQTT_PORT       1883
#define MQTT_USER       "your_mqtt_user"
#define MQTT_PASSWORD   "your_mqtt_password"
