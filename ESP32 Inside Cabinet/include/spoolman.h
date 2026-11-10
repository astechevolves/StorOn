#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Minimal Spoolman (github.com/Donkie/Spoolman) client, using the URL from
// settings. Used to pre-fill the OpenTag3D write form from a spool record.

// Fetch a spool and map its filament data to OpenTag3D field names.
// out: {"fields": {...}, "spool": {id, name, vendor, remaining_weight}}
bool spoolmanFetch(int spoolId, JsonDocument &out, String &err);

// Store a tag ID in the spool's "tag" extra field (the same field
// OpenSpoolMan matches on). The field must exist in Spoolman's settings.
bool spoolmanSetTag(int spoolId, const char *tagId, String &err);

// ── Lane sync (runs on the ESP32; no Home Assistant needed) ──
// When a lane's tag settles, find the Spoolman spool whose "tag" matches the
// lane's spool key and set its location to "<prefix> Lane N". When the lane
// empties, clear that location again (only if it's still ours).
void updateSpoolSync(unsigned long now);
// Spoolman fields for a lane (spool id, name, remaining weight, sync error)
void spoolSyncJson(int lane, JsonObject out);
// Link the tag currently in a lane to a Spoolman spool, then sync. If the tag
// already belongs to another spool (a reusable spool with a new roll), it is
// refused unless move is set: then the tag moves to spoolId and the old spool
// loses it, leaves the lane location, and is archived if archiveOld.
bool spoolSyncLink(int lane, int spoolId, bool move, bool archiveOld, String &err);
// Re-read every lane's spool from Spoolman (after an edit made elsewhere)
void spoolSyncRefreshAll();
// Nozzle temp for the lane's spool (Spoolman filament, else the tag), 0 if
// unknown; material likewise ("" if unknown)
int laneNozzleTemp(int lane, String &material);
// The lane's spool in lane_data (RFC 0001) field names: material,
// filament_type, color ("#RRGGBB"), vendor_name, name, spool_id,
// spoolman_filament_id, filament_id (Spoolman extra orca_filament_id),
// nozzle_temp, bed_temp, remaining_weight_g, total_weight_g. Only known
// fields are set; empty when the lane holds no spool.
void laneFilamentJson(int lane, JsonObject out);

// Create a Spoolman spool for the lane's tag, already tagged and located.
// The filament is opts["filament_id"] if given, else found (or created) from
// the lane's decoded tag data (Bambu or OpenTag3D).
// opts (all optional): filament_id, initial_weight, spool_weight, price,
// lot_nr, comment, move + archive_old (as for spoolSyncLink).
// dryRun: only report the plan in out["plan"], create nothing.
bool spoolCreateForLane(int lane, bool dryRun, JsonObjectConst opts, JsonDocument &out, String &err);
