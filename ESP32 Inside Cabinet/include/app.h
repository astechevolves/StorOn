#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Release builds set this from the git tag (see .github/workflows)
#ifndef FW_VERSION
#define FW_VERSION "0.3.0"
#endif

// Implemented in main.cpp, used by the web UI
void appStatusJson(JsonDocument &doc); // live state for the diagnostics page
void scheduleReboot(unsigned long delayMs = 1000);
bool setupApActive();

// Implemented in web_ui.cpp
void setupWeb();
void updateWeb();
