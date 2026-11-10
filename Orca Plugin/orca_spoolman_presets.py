# /// script
# requires-python = ">=3.12"
# dependencies = []
#
# [tool.orcaslicer.plugin]
# id = "orca-spoolman-presets"
# name = "Orca Spoolman Presets"
# description = "Assigns your filament presets to the slots of a Moonraker printer from the Spoolman spools loaded in its lanes, and creates tunable presets for Spoolman filaments OrcaSlicer has none for."
# author = "DIEHARDave"
# version = "0.1.0"
# ///
"""Orca Spoolman Presets.

Run it from the Plugins dialog. It opens a window with two actions:

Sync slots
    Reads lane_data from the printer's Moonraker, asks Spoolman (through Moonraker's
    /server/spoolman/proxy) about each lane's spool, and selects a filament preset for the
    matching slot (lane 0 is slot 1). Run Orca's own filament sync first so the slot count
    and colors are right; this replaces the generic presets it picks.

    The preset is, in order:
      1. the "orca_preset" extra field of the spool, then of its filament;
      2. the compatible preset whose name, without any " @Printer" suffix, is the longest
         whole-word prefix of "<vendor> <filament name>" or "<vendor> <material> <filament name>",
         ignoring case. On a tie your own preset wins over one of this plugin's, which wins
         over a built-in one.

Create missing presets
    Lists the Spoolman filaments no preset matches, grouped by a proposed name such as
    "Sunlu HS PLA+ 2.0" (vendor plus product line, without the color). Each preset created
    inherits a built-in preset: the vendor's own when OrcaSlicer has one, else Generic, and
    takes the nozzle and bed temperature, density and diameter from Spoolman. They go into
    a local preset bundle (user/<folder>/_local/spoolman), which Orca treats as read-only:
    tune one and save it as your own preset, which then wins the match.
"""

import json
import os
import queue
import re
import tempfile
import urllib.error
import urllib.request
from collections import Counter

try:
    import orca
except ImportError:  # unit tests and plain Python
    orca = None

BUNDLE_ID = "spoolman"
BUNDLE_NAME = "Orca Spoolman Presets"
PLUGIN_VERSION = "0.1.0"
HTTP_TIMEOUT = 5

RANK_USER, RANK_BUNDLE, RANK_SYSTEM = 0, 1, 2

MATERIALS = {"PLA", "PETG", "PET", "PCTG", "ABS", "ASA", "TPU", "TPE", "PA", "PA6", "PA12", "PC",
             "PVA", "HIPS", "PP", "PPS", "PEEK", "NYLON", "CF", "GF"}
# Words that name a product line rather than a color: "Silk PLA+ Light Gold" keeps "Silk PLA+".
LINE_WORDS = {"HS", "HIGH", "SPEED", "SILK", "MATTE", "META", "FLUORESCENT", "TRANSPARENT",
              "TRANSLUCENT", "DUAL-COLOR", "TRI-COLOR", "MULTICOLOR", "RAINBOW", "MARBLE", "WOOD",
              "GLOW", "GALAXY", "SPARKLE", "METALLIC", "BASIC", "ULTRA", "PRO", "LITE", "PLUS",
              "TOUGH", "SNAPSPEED", "RAPID", "HYPER", "CARBON", "FIBER", "SOLEYIN"}
VERSION_RE = re.compile(r"^v?\d+(\.\d+)*\+?$", re.IGNORECASE)
BAD_FILENAME_CHARS = set('\\/:*?"<>|')


# Matching and naming. Plain functions: no orca, no network.

def decode_extra(value):
    """Spoolman stores extra field values JSON-encoded, so a text field arrives as '"name"'."""
    if not isinstance(value, str):
        return ""
    try:
        decoded = json.loads(value)
    except ValueError:
        return value
    return decoded if isinstance(decoded, str) else ""


def base_name(name, bundle_id=""):
    """A preset's name as the user sees it, without the bundle prefix or " @Printer" suffix."""
    if bundle_id:
        prefix = "_local/%s/" % bundle_id
        if name.startswith(prefix):
            name = name[len(prefix):]
        else:
            name = name.rsplit("/", 1)[-1]
    return name.split(" @", 1)[0].strip()


def _is_word_prefix(text, prefix):
    if not prefix or len(prefix) > len(text) or text[:len(prefix)].casefold() != prefix.casefold():
        return False
    return len(text) == len(prefix) or text[len(prefix)] == " "


def spool_keys(vendor, material, name):
    """The names a spool is matched by: '<vendor> <name>', then with the material inserted."""
    vendor, material, name = (s.strip() for s in (vendor or "", material or "", name or ""))
    keys = [" ".join(p for p in (vendor, name) if p)]
    if material and not any(_material_word(w) for w in name.split()):
        keys.append(" ".join(p for p in (vendor, material, name) if p))
    return [k for k in keys if k]


def match_preset(keys, candidates, name=""):
    """Best candidate for any key: the longest base that is a whole-word prefix, lowest rank on a tie.

    candidates: dicts with "name", "base" and "rank". When no key matches, the bare filament name
    is tried against your own and this plugin's presets only, for presets named without the vendor.
    """
    def best_of(keys, pool):
        best = None
        for key in keys:
            for cand in pool:
                if not _is_word_prefix(key, cand["base"]):
                    continue
                if best is None or len(cand["base"]) > len(best["base"]) or (
                        len(cand["base"]) == len(best["base"]) and cand["rank"] < best["rank"]):
                    best = cand
        return best

    found = best_of(keys, candidates)
    if found is None and name.strip():
        found = best_of([name.strip()], [c for c in candidates if c["rank"] != RANK_SYSTEM])
    return found


def find_named_preset(name, candidates):
    """The candidate an orca_preset value names: its full name, or its base name ignoring case."""
    for cand in candidates:
        if cand["name"] == name:
            return cand
    named = [c for c in candidates if c["base"].casefold() == base_name(name).casefold()]
    return min(named, key=lambda c: c["rank"]) if named else None


def _material_word(word):
    parts = word.upper().rstrip("+").replace("+", "").split("-")
    return any(p in MATERIALS for p in parts)


def _line_word(word):
    return _material_word(word) or word.upper() in LINE_WORDS or bool(VERSION_RE.match(word)) or any(
        p in LINE_WORDS for p in word.upper().split("-"))


def propose_line(name, material):
    """The product line of a Spoolman filament name, without its color.

    "HS PLA+ 2.0 Olive Green" -> "HS PLA+ 2.0"; "Matte Beige" (PLA) -> "PLA Matte"; "Black" (PLA+) -> "PLA+".
    """
    words = (name or "").split()
    last = -1
    for i, word in enumerate(words):
        if _line_word(word):
            last = i
        elif last >= 0 and i > last:
            break
    line = words[:last + 1]
    if not any(_material_word(w) for w in line) and material:
        line = [material] + line
    return " ".join(line)


def _tokens(text, loose=False):
    tokens = {t.casefold() for t in re.split(r"\s+", text or "") if t}
    return {t.rstrip("+") for t in tokens} if loose else tokens


def choose_parent(vendor, material, line, systems):
    """The built-in preset a new one inherits.

    systems: dicts with "name", "base", "vendor", "type" and "global" (compatible with any printer).
    A preset for any printer always beats a printer-specific one, so the new preset is usable on
    every printer: the vendor's own for the material, else Generic <material>, and only then a
    printer-specific one. Among the vendor's presets the one sharing most words with the line and
    adding fewest others wins ("PLA" and "PLA+" count as the same word).
    """
    material_cf = (material or "").casefold()
    vendor_cf = (vendor or "").casefold()
    line_loose, line_exact = _tokens(line, True), _tokens(line)

    def pick(pool):
        if not pool:
            return None
        return max(pool, key=lambda s: (len(line_loose & _tokens(s["base"], True)),
                                        -len(_tokens(s["base"], True) - line_loose),
                                        len(line_exact & _tokens(s["base"])), -len(s["base"])))

    typed = [s for s in systems if (s["type"] or "").casefold() == material_cf]
    own = [s for s in typed if (s["vendor"] or "").casefold() == vendor_cf]
    generic = [s for s in typed if s["base"].casefold().startswith("generic ")]
    exact_generic = [s for s in generic if s["base"].casefold() == "generic " + material_cf]
    for pool in (own, exact_generic, generic):
        found = pick([s for s in pool if s["global"]])
        if found:
            return found
    return pick(own) or pick(generic) or pick(typed)


def most_common(values):
    values = [v for v in values if v not in (None, "", 0)]
    return Counter(values).most_common(1)[0][0] if values else None


def missing_presets(filaments, candidates, systems):
    """Proposed presets for Spoolman filaments that no candidate matches, grouped by name."""
    groups = {}
    for fil in filaments:
        vendor = ((fil.get("vendor") or {}).get("name") or "").strip()
        material = (fil.get("material") or "").strip()
        name = (fil.get("name") or "").strip()
        extra = fil.get("extra") or {}
        if decode_extra(extra.get("orca_preset")) or not material:
            continue
        if match_preset(spool_keys(vendor, material, name), candidates, name):
            continue
        preset_name = " ".join(p for p in (vendor, propose_line(name, material)) if p)
        group = groups.setdefault(preset_name.casefold(), {
            "name": preset_name, "vendor": vendor, "material": material, "filaments": []})
        group["filaments"].append(fil)
    result = []
    for group in groups.values():
        fils = group.pop("filaments")
        parent = choose_parent(group["vendor"], group["material"], group["name"], systems)
        group.update({
            "parent": parent["name"] if parent else "",
            "count": len(fils),
            "examples": sorted({f.get("name") or "" for f in fils})[:4],
            "nozzle_temp": most_common(f.get("settings_extruder_temp") for f in fils),
            "bed_temp": most_common(f.get("settings_bed_temp") for f in fils),
            "density": most_common(f.get("density") for f in fils),
            "diameter": most_common(f.get("diameter") for f in fils),
        })
        result.append(group)
    return sorted(result, key=lambda g: g["name"].casefold())


def preset_json(item):
    """A local-bundle filament preset inheriting item["parent"] with Spoolman's defaults."""
    data = {
        "type": "filament",
        "name": item["name"],
        "from": "User",
        "instantiation": "true",
        "version": PLUGIN_VERSION,
        "inherits": item["parent"],
        "filament_settings_id": [item["name"]],
    }
    if item.get("vendor"):
        data["filament_vendor"] = [item["vendor"]]
    if item.get("nozzle_temp"):
        data["nozzle_temperature"] = data["nozzle_temperature_initial_layer"] = [str(int(item["nozzle_temp"]))]
    if item.get("bed_temp"):
        bed = [str(int(item["bed_temp"]))]
        for key in ("hot_plate_temp", "hot_plate_temp_initial_layer",
                    "textured_plate_temp", "textured_plate_temp_initial_layer"):
            data[key] = bed
    if item.get("density"):
        data["filament_density"] = ["%g" % float(item["density"])]
    if item.get("diameter"):
        data["filament_diameter"] = ["%g" % float(item["diameter"])]
    return data


def valid_preset_name(name):
    return bool(name) and name == name.strip() and not any(c in BAD_FILENAME_CHARS for c in name) \
        and " @" not in name and not name.startswith(".")


def normalize_color(value):
    """'#RRGGBB' from '#rrggbb', 'RRGGBB', '0xRRGGBB' or with an alpha byte; '' when not a color."""
    text = str(value or "").strip()
    if text[:2].lower() == "0x":
        text = text[2:]
    text = text.lstrip("#")
    if len(text) == 8:
        text = text[:6]
    return "#" + text.upper() if re.fullmatch(r"[0-9A-Fa-f]{6}", text) else ""


def lanes_from_lane_data(value):
    """(slot number, spool id, lane material, lane color) for each lane, slot = lane + 1."""
    lanes = []
    for lane in (value or {}).values():
        if not isinstance(lane, dict):
            continue
        try:
            index = int(str(lane.get("lane", "")))
        except ValueError:
            continue
        spool_id = lane.get("spool_id")
        lanes.append((index + 1, spool_id if isinstance(spool_id, int) and spool_id > 0 else None,
                      lane.get("material") or "", normalize_color(lane.get("color"))))
    return sorted(lanes)


def set_slot_color(slot, color):
    """Set a slot's color where Orca lets plugins (preset.filament.set_slot_color); False otherwise."""
    setter = getattr(orca.host.preset.filament, "set_slot_color", None)
    if setter is None or not color:
        return False
    setter(slot, [color])
    return True


# Moonraker and Spoolman

class Moonraker:
    def __init__(self, url):
        self.url = (url or "").strip().rstrip("/")
        if not re.match(r"^https?://[^/]+", self.url):
            raise ValueError("Set the printer's Moonraker URL first, e.g. http://10.0.1.107:7125")

    def _request(self, path, body=None):
        data = json.dumps(body).encode() if body is not None else None
        request = urllib.request.Request(self.url + path, data=data, method="POST" if data else "GET",
                                         headers={"Content-Type": "application/json"} if data else {})
        try:
            with urllib.request.urlopen(request, timeout=HTTP_TIMEOUT) as response:
                return json.loads(response.read().decode("utf-8"))
        except urllib.error.HTTPError as error:
            raise RuntimeError("Moonraker %s answered HTTP %d" % (path, error.code))
        except (urllib.error.URLError, OSError) as error:
            raise RuntimeError("Cannot reach Moonraker at %s: %s" % (self.url, getattr(error, "reason", error)))

    def lane_data(self):
        return self._request("/server/database/item?namespace=lane_data").get("result", {}).get("value") or {}

    def spoolman(self, path):
        result = self._request("/server/spoolman/proxy",
                               {"use_v2_response": True, "request_method": "GET", "path": path}).get("result") or {}
        if result.get("error"):
            raise RuntimeError("Spoolman: %s" % (result["error"].get("message") if isinstance(result["error"], dict)
                                                 else result["error"]))
        return result.get("response")


# Orca side

def _write_json_atomic(path, value):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix="." + os.path.basename(path) + ".", suffix=".tmp",
                                             dir=os.path.dirname(path))
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(value, stream, ensure_ascii=False, indent=4)
            stream.write("\n")
        os.replace(temporary, path)
    except Exception:
        try:
            os.unlink(temporary)
        except OSError:
            pass
        raise


def _data_dir():
    parts = os.path.abspath(__file__).replace("\\", "/").split("/")
    if "orca_plugins" not in parts:
        raise RuntimeError("Install this file as an OrcaSlicer plugin")
    return "/".join(parts[:parts.index("orca_plugins")])


def preset_snapshot(bundle):
    """Filament presets as plain dicts. Runs on the UI thread, which owns the live presets."""
    filaments = bundle.filaments
    candidates, systems, folder = [], [], None
    for index in range(filaments.size()):
        preset = filaments.preset(index)
        if preset.is_default:
            continue
        if folder is None and preset.file:
            parts = os.path.normpath(preset.file).replace("\\", "/").split("/")
            if "user" in parts and parts.index("user") + 1 < len(parts):
                folder = parts[parts.index("user") + 1]
        rank = RANK_SYSTEM if preset.is_system else RANK_BUNDLE if preset.bundle_id else RANK_USER
        base = base_name(preset.name, preset.bundle_id)
        if preset.is_compatible and preset.is_visible:
            candidates.append({"name": preset.name, "base": base, "rank": rank})
        if preset.is_system:
            systems.append({
                "name": preset.name, "base": base,
                "vendor": _first(preset.config_value("filament_vendor")),
                "type": _first(preset.config_value("filament_type")),
                "global": not _first(preset.config_value("compatible_printers"))
                          and not _first(preset.config_value("compatible_printers_condition")),
            })
    return {"candidates": candidates, "systems": systems, "folder": folder or "default",
            "slots": list(bundle.current_filament_preset_names())}


def _first(value):
    """First entry of a serialized config value ('"a";"b"' or 'a')."""
    if value is None:
        return ""
    text = str(value)
    if text.startswith('"'):
        end = text.find('"', 1)
        return text[1:end] if end > 0 else text.strip('"')
    return text.split(";", 1)[0].strip()


def sync_slots(moonraker, snap):
    """Select a preset per lane; returns one report row per lane."""
    rows = []
    slots = snap["slots"]
    for slot, spool_id, lane_material, lane_color in lanes_from_lane_data(moonraker.lane_data()):
        row = {"slot": slot, "spool": spool_id, "spool_name": "", "preset": "", "status": ""}
        rows.append(row)
        if spool_id is None:
            row["status"] = "No Spoolman spool in this lane" if lane_material else "Empty lane"
            continue
        spool = moonraker.spoolman("/v1/spool/%d" % spool_id) or {}
        fil = spool.get("filament") or {}
        vendor = ((fil.get("vendor") or {}).get("name") or "")
        row["spool_name"] = " ".join(p for p in (vendor, fil.get("name") or "") if p)
        named = decode_extra((spool.get("extra") or {}).get("orca_preset")) or \
            decode_extra((fil.get("extra") or {}).get("orca_preset"))
        if named:
            cand = find_named_preset(named, snap["candidates"])
            if cand is None:
                row["status"] = 'orca_preset "%s" is not a compatible preset' % named
                continue
            how = "orca_preset"
        else:
            cand = match_preset(spool_keys(vendor, fil.get("material"), fil.get("name")), snap["candidates"],
                                fil.get("name") or "")
            if cand is None:
                row["status"] = "No matching preset; see Create missing presets"
                continue
            how = "name match"
        row["preset"] = cand["base"]
        if slot > len(slots):
            row["status"] = "Orca has only %d filament slots; run Orca's sync first" % len(slots)
            continue
        if slots[slot - 1] == cand["name"]:
            row["status"] = "Already set (%s)" % how
        else:
            orca.host.preset.filament.begin_update(slot, cand["name"]).commit()
            row["status"] = "Selected (%s)" % how
        # Selecting a preset gives the slot that preset's default color; put the spool's back.
        if set_slot_color(slot, lane_color or normalize_color(fil.get("color_hex"))):
            row["status"] += ", color set"
    return rows


def create_presets(items, snap):
    """Write the chosen presets into the local bundle and reload it."""
    taken = {c["base"].casefold() for c in snap["candidates"] if c["rank"] != RANK_BUNDLE}
    bundle_dir = os.path.join(_data_dir(), "user", snap["folder"], "_local", BUNDLE_ID)
    created = []
    for item in items:
        name = (item.get("name") or "").strip()
        if not valid_preset_name(name):
            raise ValueError('"%s" is not a usable preset name' % name)
        if not item.get("parent"):
            raise ValueError('No built-in preset to inherit for "%s"' % name)
        if name.casefold() in taken:
            raise ValueError('A preset named "%s" already exists' % name)
        _write_json_atomic(os.path.join(bundle_dir, "filament", name + ".json"), preset_json(dict(item, name=name)))
        created.append(name)
    _write_json_atomic(os.path.join(bundle_dir, "bundle_metadata.json"),
                       {"id": BUNDLE_ID, "name": BUNDLE_NAME, "version": PLUGIN_VERSION})
    orca.host.preset.bundles.reload_local(BUNDLE_ID)
    return created


PAGE = r"""<!doctype html>
<html><head><meta charset="utf-8"><style>
:root { color-scheme: light dark; --line: rgba(127,127,127,.35); --soft: rgba(127,127,127,.12); --a: #009688; }
* { box-sizing: border-box; }
body { margin: 0; padding: 14px 16px; font: 13px/1.45 var(--orca-font, system-ui, sans-serif);
       color: var(--orca-fg, CanvasText); background: var(--orca-bg, Canvas); }
h2 { font-size: 14px; margin: 18px 0 6px; }
.row { display: flex; gap: 8px; align-items: center; flex-wrap: wrap; }
input[type=text] { flex: 1; min-width: 200px; font: inherit; color: inherit; background: transparent;
                   border: 1px solid var(--line); border-radius: 6px; padding: 6px 8px; }
button { font: inherit; border: 1px solid var(--a); background: var(--a); color: #fff; border-radius: 6px;
         padding: 6px 12px; cursor: pointer; }
button.quiet { background: transparent; color: inherit; border-color: var(--line); }
button:disabled { opacity: .5; cursor: default; }
.note { opacity: .7; font-size: 12px; margin: 2px 0 8px; }
table { width: 100%; border-collapse: collapse; margin-top: 6px; }
th, td { text-align: left; padding: 5px 6px; border-bottom: 1px solid var(--line); vertical-align: top; }
th { font-weight: 600; font-size: 12px; opacity: .8; }
td input[type=text] { min-width: 150px; width: 100%; padding: 3px 6px; }
.dim { opacity: .65; font-size: 12px; }
#status { margin-top: 12px; min-height: 18px; }
.error { color: #d13c32; }
</style></head><body>
<div class="row"><label for="url">Moonraker URL</label>
  <input type="text" id="url" placeholder="http://10.0.1.107:7125"><button class="quiet" id="save">Save</button></div>

<h2>Sync slots</h2>
<p class="note">Run Orca's own filament sync first, then this to replace its generic presets with yours.</p>
<div class="row"><button id="sync">Sync slots from printer</button></div>
<table id="syncTable" hidden><thead><tr><th>Slot</th><th>Spool</th><th>Preset</th><th>Result</th></tr></thead><tbody></tbody></table>

<h2>Create missing presets</h2>
<p class="note">Spoolman filaments no preset matches. Created presets are read-only templates in the
"spoolman" bundle: tune one and save it as your own preset.</p>
<div class="row"><button id="scan">Find missing presets</button>
  <button id="create" disabled>Create selected</button></div>
<table id="missTable" hidden><thead><tr><th></th><th>Preset name</th><th>Inherits</th><th>Defaults</th><th>Spoolman filaments</th></tr></thead><tbody></tbody></table>

<div id="status"></div>
<script>
'use strict';
const $ = id => document.getElementById(id);
let missing = [], busy = false;
const esc = v => String(v ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
function setBusy(value, text) {
  busy = value;
  ['save', 'sync', 'scan'].forEach(id => $(id).disabled = value);
  $('create').disabled = value || !missing.length;
  if (text !== undefined) status(text);
}
function status(text, error) { $('status').textContent = text || ''; $('status').className = error ? 'error' : ''; }
function send(command, extra) { setBusy(true, 'Working…'); orca.postMessage(Object.assign({command, url: $('url').value}, extra || {})); }
$('save').onclick = () => send('save_url');
$('sync').onclick = () => send('sync');
$('scan').onclick = () => send('scan');
$('create').onclick = () => {
  const items = [];
  document.querySelectorAll('#missTable tbody tr').forEach((tr, i) => {
    if (tr.querySelector('input[type=checkbox]').checked)
      items.push(Object.assign({}, missing[i], {name: tr.querySelector('input[type=text]').value}));
  });
  if (items.length) send('create', {items});
};
function renderSync(rows) {
  $('syncTable').hidden = false;
  $('syncTable').tBodies[0].innerHTML = rows.map(r => '<tr><td>' + r.slot + '</td><td>' + esc(r.spool_name || (r.spool ? '#' + r.spool : '—')) +
    '</td><td>' + esc(r.preset) + '</td><td>' + esc(r.status) + '</td></tr>').join('') || '<tr><td colspan="4" class="dim">No lanes in lane_data</td></tr>';
}
function renderMissing(items) {
  missing = items;
  $('missTable').hidden = false;
  $('missTable').tBodies[0].innerHTML = items.map(m => {
    const d = [m.nozzle_temp && m.nozzle_temp + ' °C', m.bed_temp && 'bed ' + m.bed_temp + ' °C', m.density && m.density + ' g/cm³'].filter(Boolean).join(', ');
    return '<tr><td><input type="checkbox"' + (m.parent ? ' checked' : ' disabled') + '></td><td><input type="text" value="' + esc(m.name) +
      '"></td><td class="dim">' + esc(m.parent || 'none found') + '</td><td class="dim">' + esc(d || '—') + '</td><td class="dim">' + m.count + ': ' +
      esc(m.examples.join(', ')) + (m.count > m.examples.length ? ', …' : '') + '</td></tr>';
  }).join('') || '<tr><td colspan="5" class="dim">Every Spoolman filament has a preset</td></tr>';
}
orca.onMessage(m => {
  if (!m) return;
  if (m.url !== undefined && document.activeElement !== $('url')) $('url').value = m.url;
  if (m.sync) renderSync(m.sync);
  if (m.missing) renderMissing(m.missing);
  setBusy(false);
  status(m.error || m.message || '', !!m.error);
});
orca.postMessage({command: 'init'});
</script></body></html>"""


if orca is not None:
    class SpoolmanPresets(orca.script.ScriptPluginCapabilityBase):
        window = None
        _commands = None
        _cancelled = False

        def get_name(self):
            return "Orca Spoolman Presets"

        def get_default_config(self):
            return {"moonraker_url": ""}

        def _config(self):
            config = {"moonraker_url": ""}
            try:
                config.update(json.loads(self.get_config() or "{}"))
            except ValueError:
                pass
            return config

        # Runs on the Script worker: network and begin_update() block here, never the UI thread.
        def execute(self):
            if self.window is not None and self.window.is_open():
                self.window.show()
                return orca.ExecutionResult.success("Orca Spoolman Presets is already open.")
            commands = queue.Queue()
            self._commands = commands
            try:
                if self._cancelled:
                    return orca.ExecutionResult.success("Closed.")
                self.window = orca.host.ui.create_window(
                    title="Orca Spoolman Presets", html=PAGE, width=820, height=640,
                    on_message=self._on_message, on_close=self._on_close)
                while True:
                    message = commands.get()
                    if self._cancelled or message.get("command") == "close":
                        return orca.ExecutionResult.success("Closed.")
                    try:
                        reply = self._run(message)
                    except Exception as error:
                        reply = {"error": str(error)}
                    if self.window is not None and self.window.is_open():
                        self.window.post(reply)
            except Exception as error:
                return orca.ExecutionResult.failure(orca.PluginResult.RecoverableError,
                                                    "Orca Spoolman Presets failed: %s" % error)
            finally:
                if self._commands is commands:
                    self._commands = None
                self.window = None

        def _run(self, message):
            command = message["command"]
            moonraker = Moonraker(message.get("url"))
            snap = message["snapshot"]
            if command == "sync":
                rows = sync_slots(moonraker, snap)
                changed = sum(r["status"].startswith("Selected") for r in rows)
                return {"sync": rows, "message": "%d slot(s) changed. Presets stay unsaved project choices." % changed}
            if command == "scan":
                filaments = moonraker.spoolman("/v1/filament") or []
                items = missing_presets(filaments, snap["candidates"], snap["systems"])
                return {"missing": items, "message": "%d preset(s) to create." % len(items)}
            if command == "create":
                created = create_presets(message.get("items") or [], snap)
                return {"missing": [], "message": "Created %d preset(s): %s" % (len(created), ", ".join(created))}
            raise ValueError("Unknown command")

        # UI thread: reads presets and config, and hands network work to the worker.
        def _on_message(self, message):
            message = dict(message) if isinstance(message, dict) else {}
            command = message.get("command")
            try:
                if command in ("init", "save_url"):
                    config = self._config()
                    if command == "save_url":
                        config["moonraker_url"] = (message.get("url") or "").strip().rstrip("/")
                        self.save_config(json.dumps(config))
                    self.window.post({"url": config["moonraker_url"],
                                      "message": "Saved." if command == "save_url" else ""})
                elif command in ("sync", "scan", "create"):
                    message["snapshot"] = preset_snapshot(orca.host.preset_bundle())
                    self._enqueue(message)
            except Exception as error:
                if self.window is not None and self.window.is_open():
                    self.window.post({"error": str(error)})

        def _on_close(self):
            self._enqueue({"command": "close"})

        def on_cancelled(self):
            self._cancelled = True
            self._enqueue({"command": "close"})

        def _enqueue(self, message):
            commands = self._commands
            if commands is not None:
                commands.put(message)

    @orca.plugin
    class SpoolmanPresetsPlugin(orca.base):
        def register_capabilities(self):
            orca.register_capability(SpoolmanPresets)
