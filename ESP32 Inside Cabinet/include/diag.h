#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// printf-style event log: goes to Serial and to a small ring buffer that the
// web UI's diagnostics page shows (newest first).
void logEvent(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void logToJson(JsonArray out);
