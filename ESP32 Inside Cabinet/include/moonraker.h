#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Printer link through Moonraker (optional). Polls the _STORON macro's
// variables (see "Klipper Macros/storon.cfg") once a second:
//   tool goes unloaded -> release the spool held in the lane feeding it
//   tool goes loaded   -> log it; the lane's held spool is that tool's spool
// Only changes count, so a Klipper restart never releases a lane.

void updateMoonraker(unsigned long now);
// -1 unknown / no link, 0 unloaded, 1 loaded
int toolLoaded(int tool);
// The tool's filament sensor: -1 unknown, 0 no filament, 1 filament
int toolSensor(int tool);
// True once the tool's state and its sensor have disagreed for 30 s with the
// printer idle (filament pulled by hand, a load that failed, ...)
bool toolMismatch(int tool);
// False when the printer can't take a command now; why says what's going on
bool printerBusy(String &why);
// Lane buttons: "load" / "unload" send the configured command for the tool
// the lane feeds; "mark_loaded" / "mark_unloaded" only correct the state
bool laneToolAction(int lane, const String &action, String &msg);
void moonrakerStatusJson(JsonObject out);
