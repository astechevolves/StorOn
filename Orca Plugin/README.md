# Lane Spool Presets (OrcaSlicer plugin)

Fills OrcaSlicer's filament slots with the spools loaded on your printer, using
your own per-spool filament presets. Load or swap spools, run the plugin from
the Plugins menu, and each slot gets that spool's preset: no Spoolman number
lookups, no Orca restart.

```
T0 (slot 1): (#78) PETG Matte White High Speed - JAYO - PipSpool
T1 (slot 2): (#77) PLA Support for PLAPETG Nature - Bambu Lab - PipSpool
...
```

## How it works

1. **The printer knows which spool is on each tool.** Something writes the
   Spoolman `spool_id` for each tool into Moonraker's `lane_data` database.
   StorOn does this from the RFID tag in each lane; AFC and Happy Hare use the
   same database.
2. **You have one filament preset per Spoolman spool**, with the spool number
   in its name, such as `(#78) PETG Matte White High Speed - JAYO - PipSpool`.
   The PipSpool plugin (by Donko, in Orca's plugin list) creates these from Spoolman; presets
   you name yourself in the same pattern work too.
3. **The plugin connects the two.** It reads `lane_data`, finds the preset for
   each tool's spool, and selects it in slot tool + 1 through Orca's normal
   preset selection, the same as picking it from the slot's dropdown.

Why a plugin: Orca's built-in "Synchronize filament list" only picks system
presets or presets that don't inherit from one. Per-spool presets inherit from a
system preset, so Orca's sync falls back to "Generic PETG" and similar.

## Requirements

- OrcaSlicer 2.5.0 or newer (plugin preset API).
- A Klipper printer with Moonraker, and something filling `lane_data` with
  `spool_id` per tool (for example StorOn; see `Klipper Macros/` and the StorOn
  firmware in this repo).
- Per-spool filament presets named with the spool number (PipSpool's default
  naming works as is).

## Install

1. OrcaSlicer: **File → Plugins**, install from file, and choose
   `lane_spool_presets.py`.
2. Allow network access to your printer when Orca asks.
3. The printer address is taken from the current printer preset's host
   (**Printer settings → Basic information → host**). Set `moonraker_url` in the
   plugin's settings to override it.

## Use

Run **Lane Spool Presets** from the Plugins menu once per Orca session. It
applies the loaded spools to the slots and opens a **Lane Spool Presets** panel
docked beside the 3D view: a table of tools, spools and presets, with an
**Apply to slots** button for the next spool swap and **Refresh** to re-read the
printer. The panel stays open until you close it; drag its caption to move it.

Slot colours are a project setting the plugin API can't change. Orca's own
filament sync button sets them from the same printer data, so after a swap:
click Orca's sync (colours), then **Apply to slots** (presets).

- A spool without a preset yet (new spool, PipSpool not synced) is skipped and
  reported. Sync PipSpool once for that spool; Orca loads new presets on restart.
- If a slot's current preset has unsaved edits, Orca asks whether to keep or
  discard them, the same as switching presets by hand.
- The project needs at least as many filament slots as tools.

## Settings

| Setting | Default | Meaning |
|---|---|---|
| `moonraker_url` | empty | Printer address; empty uses the current printer preset's host |
| `name_pattern` | `(#{id})` | Start of a preset name; `{id}` is the Spoolman spool number |
| `slot_offset` | `0` | Slot = tool number + 1 + offset |

## License

MIT, as the rest of StorOn.
