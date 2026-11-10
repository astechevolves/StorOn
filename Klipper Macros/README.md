# Klipper macros for StorOn

Optional. With these, StorOn ties the spool in each lane to the printer tool
it feeds: a tool's **unload** releases its lane's spool (and clears its
Spoolman location). StorOn's ESP32 reads the state through Moonraker; no Home
Assistant needed.

You keep your own load and unload macros. StorOn only needs one line added to
the end of each.

## Why it's event-based

StorOn identifies a spool by catching its tag as the spool turns during the
feed. When the spool is still, the tag can sit out of the reader's view
indefinitely, so a missing tag can't mean "spool removed". Lanes hold their
spool until an unload, a different tag, or **Clear** in StorOn's web UI.

## Install

1. Copy `storon.cfg` next to your other macro files and include it from
   `printer.cfg`:
   ```
   [include storon.cfg]
   ```
2. Add one line to the **end of a successful load** in your load macro, and
   one to the **end of a successful unload**:

   **Toolchanger** (klipper-toolchanger), or any macro that knows the tool:
   ```
   STORON_TOOL_EVENT EVENT=loaded TOOL={tool_number}
   STORON_TOOL_EVENT EVENT=unloaded TOOL={tool_number}
   ```
   If your macro tracks the extruder name instead, `EXTRUDER=extruder1` works
   too. With neither, the active tool is used.

   **Single extruder:**
   ```
   STORON_TOOL_EVENT EVENT=loaded
   STORON_TOOL_EVENT EVENT=unloaded
   ```

   To keep your macros working on a printer without `storon.cfg`, guard it:
   ```
   {% if "gcode_macro STORON_TOOL_EVENT" in printer %}
       STORON_TOOL_EVENT EVENT=loaded TOOL={tool_number}
   {% endif %}
   ```
3. Optional: set `variable_sensor_pattern` in `_STORON` to your filament
   sensor names (default `T{n}_filament_sensor`; `{n}` is the tool number).
   It's only used to seed each tool's state at startup.
4. Optional, recommended: in `PRINT_START`, before any heating or purging,
   stop the print if a tool it uses isn't loaded (for example, unloaded
   after a spool swap, with the old filament still at its sensor):
   ```
   STORON_CHECK_LOADED TOOLS="{params.USED_TOOLS|default('')}"
   ```
   Pass the slicer's list of used tools. In OrcaSlicer's machine start
   G-code, for example:
   `USED_TOOLS="{if is_extruder_used[0]}0,{endif}{if is_extruder_used[1]}1,{endif}…"`.
   Without `TOOLS`, only the active tool is checked.

   With `[save_variables]` in your config, each tool's state survives a
   Klipper restart. Without it, tools are re-seeded from their filament
   sensors at startup, which reads an unloaded tool with the old filament
   still at its sensor as loaded.
5. Restart Klipper (`FIRMWARE_RESTART`), then run `_STORON` in the console to
   see each tool's state.
6. In StorOn's web UI, Settings: set the **Moonraker URL** and which tool each
   lane **feeds**. The status page should show **Printer link: connected**.

## Example in use

The StorOn build's own printer (a 5-tool klipper-toolchanger with LLL-Plus
buffers) calls these from its load/unload macros:
[KlipperBackup / Load_Unload.cfg](https://github.com/astechevolves/KlipperBackup/blob/main/printer_data/config/Macros/Load_Unload.cfg)
(look for `# StorOn:`).
