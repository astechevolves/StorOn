# /// script
# requires-python = ">=3.12"
# dependencies = []
#
# [tool.orcaslicer.plugin]
# name = "Lane Spool Presets"
# description = "Puts each tool's loaded Spoolman spool into its filament slot. Reads which spool is on which tool from the printer (Moonraker lane_data, as written by StorOn, AFC or Happy Hare) and selects your per-spool filament preset (e.g. PipSpool's \"(#78) ...\") for that slot. No restart, no lookups."
# author = "AsTechEvolves (StorOn)"
# version = "0.2.0"
# ///
"""Lane Spool Presets: fill Orca's filament slots from the spools loaded on the printer.

How it fits together:

  printer (Klipper/Moonraker)  ->  lane_data database: one entry per tool, with the
                                   Spoolman spool_id of the spool feeding it
                                   (written by StorOn, AFC, Happy Hare, ...)
  your filament presets        ->  one preset per Spoolman spool, with the spool number
                                   in its name, e.g. "(#78) PETG Matte White - JAYO - PipSpool"
                                   (PipSpool makes these; hand-made ones work too)
  this plugin                  ->  for each tool, selects that spool's preset in slot
                                   tool + 1, through Orca's normal preset selection

Run it once from the Plugins menu: it applies the loaded spools and opens a
docked "Lane Spool Presets" panel beside the 3D view, whose Apply button does it
again after the next spool swap. Orca's own "sync
filaments" can't do this: it only picks system presets or presets that don't
inherit from one, and per-spool presets inherit from a system preset.

Settings (Plugins -> Lane Spool Presets -> configure):
  moonraker_url   printer address; empty = the current printer preset's host
  name_pattern    how a preset name carries the spool number; {id} is the number.
                  Default "(#{id})" matches PipSpool's names. The preset name must
                  start with it.
  slot_offset     slot = tool number + 1 + slot_offset (default 0: T0 -> slot 1)
"""
from __future__ import annotations

import json
import re
import threading
import urllib.error
import urllib.parse
import urllib.request

import orca

PLUGIN_NAME = "Lane Spool Presets"
DEFAULTS = {
    "moonraker_url": "",
    "name_pattern": "(#{id})",
    "slot_offset": 0,
}
TIMEOUT_S = 5


# ── settings and printer address ─────────────────────────────────────────────

def load_config(capability) -> dict:
    cfg = dict(DEFAULTS)
    try:
        stored = json.loads(capability.get_config() or "{}")
        if isinstance(stored, dict):
            cfg.update({k: v for k, v in stored.items() if k in DEFAULTS})
    except (AttributeError, TypeError, ValueError):
        pass
    return cfg


def normalize_url(host: str, port: str = "") -> str:
    """'192.168.1.20', 'printer.local:7125' or 'http://x/' -> 'http://host[:port]'."""
    host = (host or "").strip()
    if not host:
        return ""
    if "://" not in host:
        host = "http://" + host
    parts = urllib.parse.urlsplit(host)
    netloc = parts.netloc
    port = str(port or "").strip()
    if port and port not in ("0", "80") and ":" not in netloc:
        netloc += ":" + port
    return f"{parts.scheme}://{netloc}"


def printer_preset_url() -> str:
    """The Moonraker address of the current printer preset (its print host)."""
    try:
        printer = orca.host.preset_bundle().current_printer_preset()
        return normalize_url(str(printer.config_value("print_host") or ""),
                             str(printer.config_value("printhost_port") or ""))
    except Exception:
        return ""


def moonraker_url(cfg: dict) -> str:
    return normalize_url(str(cfg.get("moonraker_url") or "")) or printer_preset_url()


# ── what's loaded on the printer ─────────────────────────────────────────────

def fetch_lane_spools(base_url: str) -> dict[int, dict]:
    """{tool number: lane entry} for lanes that name a Spoolman spool_id."""
    url = base_url + "/server/database/item?namespace=lane_data"
    try:
        with urllib.request.urlopen(url, timeout=TIMEOUT_S) as reply:
            data = json.load(reply)
    except urllib.error.HTTPError as exc:
        if exc.code == 404:
            raise RuntimeError("the printer has no lane data (nothing writes Moonraker's lane_data yet)")
        raise RuntimeError(f"Moonraker answered HTTP {exc.code}")
    except (urllib.error.URLError, OSError) as exc:
        raise RuntimeError(f"can't reach Moonraker at {base_url}: {getattr(exc, 'reason', exc)}")
    lanes = ((data or {}).get("result") or {}).get("value") or {}
    out: dict[int, dict] = {}
    for key, lane in lanes.items():
        if not isinstance(lane, dict):
            continue
        try:
            tool = int(str(lane.get("lane", "")).strip())
            spool = int(str(lane.get("spool_id", "")).strip())
        except ValueError:
            continue
        if tool >= 0 and spool > 0:
            out[tool] = dict(lane, _key=key, spool_id=spool)
    return out


# ── matching spools to presets ───────────────────────────────────────────────

def pattern_regex(name_pattern: str) -> re.Pattern:
    pattern = name_pattern if "{id}" in name_pattern else DEFAULTS["name_pattern"]
    before, after = pattern.split("{id}", 1)
    return re.compile("^" + re.escape(before) + r"(\d+)" + re.escape(after))


def spool_presets(name_pattern: str) -> dict[int, str]:
    """{spool number: preset name}, for selectable filament presets whose name carries
    a spool number. A compatible preset wins over one that isn't."""
    regex = pattern_regex(name_pattern)
    filaments = orca.host.preset_bundle().filaments
    found: dict[int, tuple[bool, str]] = {}
    for name in filaments.preset_names():
        match = regex.match(name)
        if not match:
            continue
        preset = filaments.find_preset(name)
        if preset is None or not getattr(preset, "is_visible", True):
            continue
        compatible = bool(getattr(preset, "is_compatible", True))
        spool = int(match.group(1))
        if spool not in found or (compatible and not found[spool][0]):
            found[spool] = (compatible, name)
    return {spool: name for spool, (_, name) in found.items()}


def current_slots() -> list[str]:
    try:
        return list(orca.host.preset_bundle().current_filament_preset_names())
    except Exception:
        return []


# ── the command ──────────────────────────────────────────────────────────────

def sync_slots(cfg: dict) -> tuple[bool, list[str]]:
    """Select each loaded spool's preset in its slot. Returns (ok, report lines)."""
    base = moonraker_url(cfg)
    if not base:
        return False, ["No printer address: set the current printer preset's host, "
                       "or moonraker_url in this plugin's settings."]
    try:
        lanes = fetch_lane_spools(base)
    except RuntimeError as exc:
        return False, [f"Couldn't read the printer's lanes: {exc}."]
    if not lanes:
        return True, ["No tool on the printer has a Spoolman spool assigned."]

    presets = spool_presets(str(cfg.get("name_pattern") or DEFAULTS["name_pattern"]))
    slots = current_slots()
    try:
        offset = int(cfg.get("slot_offset") or 0)
    except (TypeError, ValueError):
        offset = 0

    report, changed, ok = [], 0, True
    for tool in sorted(lanes):
        lane = lanes[tool]
        spool = lane["spool_id"]
        slot = tool + 1 + offset
        label = f"T{tool} (slot {slot})"
        name = presets.get(spool)
        if name is None:
            report.append(f"{label}: no preset for spool #{spool} yet. Sync PipSpool (or make a "
                          f"preset named like \"{cfg.get('name_pattern', '(#{id})').replace('{id}', str(spool))} ...\").")
            continue
        if slot < 1 or (slots and slot > len(slots)):
            report.append(f"{label}: this project has {len(slots)} filament slot(s); add slots for spool #{spool}.")
            ok = False
            continue
        if slots and slots[slot - 1] == name:
            report.append(f"{label}: already {name}")
            continue
        try:
            orca.host.preset.filament.begin_update(slot_number=slot, preset_name=name)
        except Exception as exc:  # cancelled unsaved-edits dialog, preset not selectable, ...
            report.append(f"{label}: couldn't select {name}: {exc}")
            ok = False
            continue
        changed += 1
        report.append(f"{label}: {name}")
    report.insert(0, f"{changed} slot(s) updated from {base}.")
    return ok, report


def lane_overview(cfg: dict) -> dict:
    """What the panel shows: per tool, the spool, its preset and the slot's current preset."""
    base = moonraker_url(cfg)
    if not base:
        return {"error": "No printer address: set the printer preset's host or moonraker_url."}
    try:
        lanes = fetch_lane_spools(base)
    except RuntimeError as exc:
        return {"error": f"Couldn't read the printer's lanes: {exc}."}
    presets = spool_presets(str(cfg.get("name_pattern") or DEFAULTS["name_pattern"]))
    slots = current_slots()
    try:
        offset = int(cfg.get("slot_offset") or 0)
    except (TypeError, ValueError):
        offset = 0
    rows = []
    for tool in sorted(lanes):
        lane = lanes[tool]
        slot = tool + 1 + offset
        preset = presets.get(lane["spool_id"], "")
        current = slots[slot - 1] if 1 <= slot <= len(slots) else ""
        label = " ".join(str(x) for x in (lane.get("vendor_name"),
                                          lane.get("name") or lane.get("material")) if x)
        rows.append({
            "tool": tool, "slot": slot, "spool": lane["spool_id"],
            "color": lane.get("color") or "", "label": label,
            "preset": preset, "current": current,
            "state": "missing" if not preset else ("ok" if current == preset else "apply"),
        })
    return {"base": base, "rows": rows}


# Docked panel: a table of tools and an Apply button. The page talks to the
# plugin through orca.postMessage / orca.onMessage.
PANEL_HTML = """
<style>
  body { margin: 0; padding: 10px; font-size: 13px; }
  h3 { margin: 0 0 2px; font-size: 14px; }
  .note { margin: 0 0 10px; color: var(--orca-muted); font-size: 12px; }
  .actions { display: flex; gap: 6px; margin-bottom: 10px; }
  .actions button.quiet { background: transparent; color: var(--orca-fg); border-color: var(--orca-border); }
  table { width: 100%; border-collapse: collapse; }
  td { padding: 4px 3px; border-bottom: 1px solid var(--orca-border); vertical-align: top; }
  .sw { display: inline-block; width: 11px; height: 11px; border-radius: 3px;
        border: 1px solid var(--orca-border); vertical-align: -1px; margin-right: 4px; }
  .muted { color: var(--orca-muted); font-size: 12px; }
  .ok { color: #3fb950; } .warn { color: #d29922; }
  #status { margin-top: 10px; white-space: pre-wrap; color: var(--orca-muted); font-size: 12px; }
</style>
<h3>Lane spools</h3>
<p class="note">Spools loaded on the printer, and the preset each slot gets.</p>
<div class="actions">
  <button type="button" id="apply">Apply to slots</button>
  <button type="button" id="refresh" class="quiet">Refresh</button>
</div>
<table><tbody id="rows"><tr><td class="muted">Reading the printer...</td></tr></tbody></table>
<div id="status"></div>
<script>
(function () {
  function esc(v) {
    var s = document.createElement("span");
    s.textContent = v == null ? "" : String(v);
    return s.innerHTML;
  }
  function state(r) {
    if (r.state === "ok") return '<span class="ok">in slot</span>';
    if (r.state === "apply") return '<span class="warn">apply</span>';
    return '<span class="warn">no preset yet</span>';
  }
  function render(m) {
    var rows = document.getElementById("rows");
    if (m.error) { rows.innerHTML = '<tr><td class="warn">' + esc(m.error) + '</td></tr>'; return; }
    if (!m.rows.length) {
      rows.innerHTML = '<tr><td class="muted">No tool has a Spoolman spool assigned.</td></tr>';
      return;
    }
    rows.innerHTML = m.rows.map(function (r) {
      var sw = r.color ? '<span class="sw" style="background:' + esc(r.color) + '"></span>' : '';
      return '<tr><td><b>T' + r.tool + '</b><br><span class="muted">slot ' + r.slot + '</span></td>' +
        '<td>' + sw + '#' + r.spool + ' ' + esc(r.label) + '<br><span class="muted">' +
        esc(r.preset || 'make or sync a preset for this spool') + '</span></td>' +
        '<td>' + state(r) + '</td></tr>';
    }).join('');
  }
  orca.onMessage(function (m) {
    if (!m) return;
    if (m.command === 'overview') render(m);
    if (m.command === 'status') document.getElementById('status').textContent = m.text;
    if (m.command === 'busy') {
      document.getElementById('apply').disabled = m.on;
      document.getElementById('refresh').disabled = m.on;
    }
  });
  document.getElementById('apply').addEventListener('click', function () { orca.postMessage({ command: 'apply' }); });
  document.getElementById('refresh').addEventListener('click', function () { orca.postMessage({ command: 'refresh' }); });
  orca.postMessage({ command: 'refresh' });
})();
</script>
"""


class LaneSpoolPresetsCapability(orca.script.ScriptPluginCapabilityBase):
    """Running it applies the loaded spools to the slots and opens (or brings
    back) the docked panel, whose Apply button does it again later."""

    panel = None

    def get_name(self):
        return PLUGIN_NAME

    def get_default_config(self):
        return DEFAULTS

    def execute(self):
        try:
            self._open_panel()
            ok, report = sync_slots(load_config(self))  # Script worker: may select presets
            self._post_result(report)
        except Exception as exc:
            return orca.ExecutionResult.failure(orca.PluginResult.RecoverableError,
                                                f"{PLUGIN_NAME} failed: {exc}")
        message = "\n".join(report)
        if ok:
            return orca.ExecutionResult.success(message)
        return orca.ExecutionResult.failure(orca.PluginResult.RecoverableError, message)

    def _open_panel(self):
        try:
            if self.panel is not None and self.panel.is_open():
                self.panel.show()
                return
            self.panel = orca.host.ui.create_dock_panel(
                html=PANEL_HTML, title=PLUGIN_NAME, width=340, height=420,
                on_message=self.on_message, on_close=self.on_close, dock="right")
        except Exception:
            self.panel = None  # no dock panels on this host: the menu command still works

    def _post(self, payload):
        if self.panel is not None:
            try:
                self.panel.post(payload)  # marshalled to the UI thread by Orca
            except Exception:
                pass

    def _post_result(self, report):
        self._post({"command": "status", "text": "\n".join(report)})
        self._post(dict(lane_overview(load_config(self)), command="overview"))

    # on_message runs on Orca's UI thread, where selecting a preset isn't allowed
    # (the selection itself waits on the UI thread), so the work runs on a thread.
    def on_message(self, message):
        command = (message or {}).get("command")
        if command in ("apply", "refresh"):
            threading.Thread(target=self._run, args=(command,), daemon=True).start()

    def _run(self, command):
        self._post({"command": "busy", "on": True})
        try:
            if command == "apply":
                _, report = sync_slots(load_config(self))
                self._post_result(report)
            else:
                self._post(dict(lane_overview(load_config(self)), command="overview"))
        except Exception as exc:
            self._post({"command": "status", "text": f"{PLUGIN_NAME} failed: {exc}"})
        finally:
            self._post({"command": "busy", "on": False})

    def on_close(self):
        self.panel = None


@orca.plugin
class LaneSpoolPresetsPlugin(orca.base):
    def register_capabilities(self):
        # Ask once, at load: HTTP to the printer, and a thread for the panel's buttons
        base = moonraker_url(dict(DEFAULTS))
        try:
            if base:
                orca.request_permissions(network=[base], threading=True)
            else:
                orca.request_permissions(threading=True)
        except Exception:
            pass  # older hosts: Orca asks on first use instead
        orca.register_capability(LaneSpoolPresetsCapability)
