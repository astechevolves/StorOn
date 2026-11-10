#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// RC522 readers on one shared SPI bus, one per lane slot (MAX_LANES slots,
// each enabled or disabled in settings). Only the reader being polled has its
// antenna on; the rest stay off, which saves power and stops neighbouring
// readers from interfering.

void setupRfid();
void updateRfid(unsigned long now);

bool laneActive(int lane);          // slot enabled in settings
bool laneKnown(int lane);           // a state has been settled
const char *laneReported(int lane); // settled UID, "" = empty
// Decoded spool data for the lane's current tag: {"format": "bambu" |
// "opentag3d", ...fields}. False if there is none.
bool laneTagInfo(int lane, JsonObject out);
// The ID that identifies the lane's spool in Spoolman's "tag" field: the
// tray_uuid for Bambu tags (what the AMS and OpenSpoolMan use), else the tag
// UID. False until the tag has settled and decoding has finished.
bool laneSpoolKey(int lane, char *out, size_t len);

void rfidStatusJson(JsonArray out);
// Release the lane's spool (web UI "Clear", or an unload)
void rfidClearLane(int lane);
// Reads everything the tag on a lane will give up (UID, type, decoded spool
// data, raw memory) for the diagnostics page.
void rfidDumpJson(int lane, JsonDocument &out);
// Reader hardware check: registers, raw tag requests, chip self-test
void rfidDiagJson(int lane, JsonDocument &out, int drive = -1); // drive 0-3: set SPI pin strength first
// Writes OpenTag3D fields to the NTAG sticker on a lane and verifies it
bool rfidWriteTag(int lane, JsonObjectConst fields, JsonDocument &out);

// Implemented in main.cpp: called when a lane's settled state changes
void publishTagState(int lane);
// Implemented in spoolman.cpp: queue a Spoolman sync for the lane
void spoolSyncLaneChanged(int lane);
