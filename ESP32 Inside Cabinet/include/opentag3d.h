#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <MFRC522.h>

// OpenTag3D (opentag3d.info, spec 2.004) on NTAG215/216 stickers: one NDEF
// record of MIME type "application/opentag3d" holding a fixed binary layout
// (big-endian integers, temperatures stored as degC / 5). Written from the
// public spec; field names match the spec's field IDs.
//
// JSON uses human units: temps in degC, diameter in mm, density in g/cm3,
// weights in g, lengths in m, colors as "#RRGGBB" or "#RRGGBBAA",
// mfg_date "YYYY-MM-DD", mfg_time "HH:MM:SS".

#define OT3D_VERSION 2004 // spec version this firmware writes (2.004)

enum Ot3dResult
{
  OT3D_OK,
  OT3D_NOT_OT3D, // read fine, but no OpenTag3D record (blank or other data)
  OT3D_IO_ERROR  // tag dropped out or a read failed
};

// Read a selected NTAG and decode its OpenTag3D record into out
Ot3dResult ot3dRead(MFRC522 &r, JsonObject out, String &err);

// Encode fields (see above) and write them to a selected NTAG, then read back
// to verify. Only user memory is written; the capability container, lock and
// config pages are never touched, and locked or non-NDEF tags are refused.
bool ot3dWrite(MFRC522 &r, JsonObjectConst fields, String &err);

// Field list for the web UI: [{id, type, len, scale, required}, ...]
void ot3dFieldsJson(JsonArray out);
