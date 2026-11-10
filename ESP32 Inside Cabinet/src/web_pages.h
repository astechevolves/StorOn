#pragma once
// Static web UI served by the ESP32. Pages load their data from the JSON API
// (/api/status, /api/settings, /api/lane/dump, /api/lane/write,
// /api/spoolman/spool), so nothing here is templated.

static const char STYLE_CSS[] PROGMEM = R"CSS(
:root{--bg:#f4f5f7;--card:#fff;--text:#1b1f24;--muted:#69727d;--line:#dde1e6;
--accent:#d71920;--accent-ink:#fff;--ok:#1a7f37;--bad:#cf222e;--warn:#9a6700;--code:#f0f2f5}
@media (prefers-color-scheme:dark){:root{--bg:#0d0f12;--card:#171a1f;--text:#e7eaee;
--muted:#9aa4af;--line:#2a3038;--accent:#ff3b3f;--ok:#3fb950;--bad:#ff6b6b;--warn:#d29922;--code:#101318}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
header{background:#000;border-bottom:3px solid #d71920}
.bar{max-width:1040px;margin:0 auto;padding:10px 16px;display:flex;align-items:center;gap:20px;flex-wrap:wrap}
.bar img{height:40px;width:auto;display:block}
nav{display:flex;gap:4px}
nav a{color:#bbb;text-decoration:none;font-weight:600;padding:6px 12px;border-radius:6px}
nav a:hover{color:#fff}
nav a.on{color:#fff;background:#d71920}
main{max-width:1040px;margin:0 auto;padding:16px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px 16px;margin-bottom:14px}
.card h2{font-size:13px;margin:0 0 10px;color:var(--muted);text-transform:uppercase;letter-spacing:.06em}
h3{font-size:15px;margin:14px 0 8px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(200px,1fr));gap:8px 18px}
.kv span{display:block;color:var(--muted);font-size:12px}
.ok{color:var(--ok)}.bad{color:var(--bad)}.warn{color:var(--warn)}.muted{color:var(--muted)}
.banner{background:var(--warn);color:#fff;padding:10px 14px;border-radius:8px;margin-bottom:14px}
table{width:100%;border-collapse:collapse;font-size:14px}
th,td{text-align:left;padding:7px 8px;border-bottom:1px solid var(--line);vertical-align:top}
th{color:var(--muted);font-weight:500;font-size:12px}
tr.off td{color:var(--muted)}
.scroll{overflow-x:auto}
code,.mono{font-family:ui-monospace,SFMono-Regular,Consolas,monospace;font-size:13px}
pre{background:var(--code);padding:10px;border-radius:8px;overflow-x:auto;margin:8px 0 0}
button{background:var(--accent);color:var(--accent-ink);border:0;border-radius:7px;padding:7px 14px;font:inherit;font-weight:500;cursor:pointer}
button.sec{background:transparent;color:var(--accent);border:1px solid var(--accent)}
button.danger{background:var(--bad)}
button.sm{padding:4px 10px;font-size:13px}
button:disabled{opacity:.45;cursor:default}
label{display:block;margin-bottom:10px}
label span{display:block;font-size:13px;color:var(--muted);margin-bottom:3px}
label span b{color:var(--accent)}
input,select{width:100%;padding:7px 9px;border:1px solid var(--line);border-radius:7px;background:var(--bg);color:var(--text);font:inherit}
input[type=checkbox]{width:auto}
input[type=color]{width:42px;height:36px;padding:2px;flex:none}
.hint{font-size:12px;color:var(--muted);margin-top:3px}
.row{display:flex;gap:10px;flex-wrap:wrap;align-items:center}
.log div{padding:3px 0;border-bottom:1px solid var(--line)}
.log span{color:var(--muted);display:inline-block;min-width:70px}
.sw{display:inline-block;width:12px;height:12px;border-radius:3px;border:1px solid var(--line);vertical-align:-1px;margin-right:5px}
.actions{display:flex;gap:6px;flex-wrap:wrap}
#msg{margin-left:6px}
.checks td:first-child{width:28px;font-size:16px}
.modal{position:fixed;inset:0;background:rgba(0,0,0,.55);display:flex;align-items:flex-start;justify-content:center;padding:4vh 12px;z-index:10;overflow:auto}
.modal[hidden]{display:none}
.modal>.card{width:100%;max-width:760px;margin:0}
.tabs{display:flex;gap:4px;border-bottom:1px solid var(--line);margin:10px 0 12px;flex-wrap:wrap}
.tabs button{background:transparent;color:var(--muted);border-radius:7px 7px 0 0;border-bottom:2px solid transparent}
.tabs button.on{color:var(--accent);border-bottom-color:var(--accent)}
.pick{max-height:320px;overflow:auto;border:1px solid var(--line);border-radius:8px}
.pick>div{padding:7px 10px;border-bottom:1px solid var(--line);cursor:pointer}
.pick>div:hover{background:var(--code)}
.pick>div.sel{box-shadow:inset 3px 0 0 var(--accent);background:var(--code)}
.pick>.shead{cursor:default;font-size:11px;text-transform:uppercase;letter-spacing:.06em;color:var(--muted);background:var(--bg)}
.pick>.shead:hover{background:var(--bg)}
.frow{display:flex;align-items:center;gap:6px}
.frow .grow{flex:1}
.newfil{margin-top:8px;padding:9px 12px;border:1px dashed var(--line);border-radius:8px;cursor:pointer;color:var(--accent)}
.newfil.sel{border-style:solid;border-color:var(--accent)}
.badge{font-size:11px;padding:1px 7px;border-radius:9px;border:1px solid var(--line);color:var(--muted);margin-left:6px}
a{color:var(--accent)}
.toast{position:fixed;bottom:16px;left:50%;transform:translateX(-50%);background:#000;color:#fff;padding:9px 16px;border-radius:8px;z-index:11}
.verdict{padding:10px 12px;border-radius:8px;margin:4px 0 10px;border:1px solid var(--line);background:var(--code)}
.verdict b{display:block;margin-bottom:2px}
details summary{cursor:pointer;color:var(--muted);font-size:13px;margin-top:8px}
)CSS";

#define PAGE_HEAD(title) "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">" \
  "<title>" title "</title><link rel=\"icon\" type=\"image/png\" href=\"/favicon.png\"><link rel=\"apple-touch-icon\" href=\"/apple-touch-icon.png\">" \
  "<link rel=\"stylesheet\" href=\"/style.css\"></head><body>"
#define PAGE_HEADER(statusOn, readersOn, settingsOn) "<header><div class=\"bar\"><a href=\"https://github.com/astechevolves/storon\" target=\"_blank\" rel=\"noopener\" title=\"StorOn on GitHub\"><img src=\"/logo.png\" alt=\"StorOn on GitHub\"></a>" \
  "<nav><a href=\"/\"" statusOn ">Status</a><a href=\"/readers\"" readersOn ">Reader check</a><a href=\"/settings\"" settingsOn ">Settings</a></nav></div></header>"

static const char STATUS_HTML[] PROGMEM = PAGE_HEAD("StorOn") PAGE_HEADER(" class=\"on\"", "", "") R"HTML(
<main>
<div id="apBanner" class="banner" hidden></div>
<div class="card"><h2 id="devTitle">Device</h2><div class="grid" id="device"></div></div>
<div class="card"><h2>Sensors</h2><div class="grid" id="sensors"></div></div>
<div class="card"><h2>RFID lanes</h2><div class="scroll"><table>
<thead><tr><th>Lane</th><th>SS</th><th>Tool</th><th>Reader</th><th>Tag / spool</th><th>Type / SAK</th><th>Last seen</th><th>Hits</th><th></th></tr></thead>
<tbody id="lanes"></tbody></table></div>
<div id="dump"></div></div>
<div class="card" id="writer" hidden></div>
<div class="card"><h2>Event log</h2><div class="log mono" id="log"></div></div>
</main>
<div id="spoolDlg" class="modal" hidden></div><div id="toast" class="toast" hidden></div>
<script>
const $=id=>document.getElementById(id);
const esc=s=>String(s??'').replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const kv=(k,v,cls='')=>`<div class="kv"><span>${esc(k)}</span><b class="${cls}">${v}</b></div>`;
const dur=s=>s<60?s+'s':s<3600?Math.floor(s/60)+'m '+s%60+'s':Math.floor(s/3600)+'h '+Math.floor(s%3600/60)+'m';
const swatch=c=>c?`<span class="sw" style="background:${esc(String(c).slice(0,7))}"></span>`:'';
let busy=false,status=null,writerLane=0;

function spoolLabel(s){
 if(!s)return '';
 if(s.format=='bambu')return swatch(s.color)+esc(s.detailed_type||s.type);
 return swatch(s.color_1)+esc([s.manufacturer,s.material+(s.material_mod?' '+s.material_mod:''),s.color_name].filter(x=>x).join(' · '));
}
function spoolGrid(s){
 if(s.format=='bambu')return `<h3>Bambu spool data</h3><div class="grid">`+
  kv('Tray UUID',`<span class="mono">${esc(s.tray_uuid)}</span>`)+kv('Filament',esc(s.detailed_type)+' ('+esc(s.type)+')')+
  kv('Color',swatch(s.color)+`<span class="mono">${esc(s.color)}</span>`)+kv('Material / variant ID',esc(s.material_id)+' / '+esc(s.variant_id))+
  kv('Spool weight',s.spool_weight_g+' g')+kv('Diameter',s.diameter_mm+' mm')+kv('Length',s.length_m+' m')+
  kv('Hotend',s.hotend_min_c+'–'+s.hotend_max_c+' °C')+kv('Bed',s.bed_temp_c+' °C')+
  kv('Drying',s.drying_temp_c+' °C for '+s.drying_hours+' h')+kv('Produced',esc(s.production_date))+'</div>';
 const skip={format:1};let h='<h3>OpenTag3D data</h3><div class="grid">';
 for(const [k,v] of Object.entries(s)){if(skip[k])continue;
  h+=kv(k.replace(/_/g,' '),(/^color_/.test(k)?swatch(v):'')+`<span class="mono">${esc(v)}</span>`)}
 return h+'</div>';
}
function toolCell(l){
 if(l.tool<0)return '<span class="muted">-</span>';
 const st=l.tool_loaded==1?'<span class="ok">loaded</span>':l.tool_loaded==0?'<span class="muted">unloaded</span>':'';
 let h=`T${l.tool}`+(st?'<br>'+st:'');
 const p=status.printer||{};
 if(!p.connected)return h;
 const dis=p.busy?` disabled title="${esc('Not now: '+p.busy)}"`:'';
 const b=[];
 if(p.unload_command&&l.tool_loaded!=0)b.push(`<button class="sec sm" onclick="toolAction(${l.lane},'unload')"${dis}>Unload</button>`);
 if(p.load_command&&l.tool_loaded!=1)b.push(`<button class="sec sm" onclick="toolAction(${l.lane},'load')"${dis}>Load</button>`);
 if(b.length)h+=`<div class="actions" style="margin-top:4px">${b.join('')}</div>`;
 if(l.tool_mismatch){const want=l.tool_sensor?'loaded':'unloaded';
  h+=`<div class="warn" style="margin-top:4px">Sensor sees ${l.tool_sensor?'filament':'no filament'}</div>`+
   `<button class="sec sm" onclick="toolAction(${l.lane},'mark_${want}')"${dis}>Mark ${want}</button>`}
 return h;
}
async function toolAction(lane,action){
 const l=status.lanes.find(x=>x.lane==lane),T='T'+l.tool,at=l.nozzle_temp?` at ${l.nozzle_temp} °C`:'';
 const ask={load:`Load ${T} from lane ${lane}${at}?\n\nThe printer heats ${T} and feeds the filament.`,
  unload:`Unload ${T}${at}?\n\nThe printer heats ${T} and pulls the filament back. Lane ${lane} releases its spool when it's done.`,
  mark_loaded:`Mark ${T} as loaded?\n\nNothing moves: this only corrects the printer's record.`,
  mark_unloaded:`Mark ${T} as unloaded?\n\nNothing moves. Lane ${lane} releases its spool, the same as after an unload.`}[action];
 if(!confirm(ask))return;
 try{const d=await smFetch(`/api/lane/tool?lane=${lane}&action=${action}`,{method:'POST'});showToast(d.message)}
 catch(e){alert(e.message)}
 refresh();
}
function spoolmanCell(l){
 if(!status||!status.spoolman)return '';
 const tag=l.state&&l.state!='empty'&&l.state!='unknown';
 const btn=label=>tag&&l.spool_key?` <button class="sec sm" onclick="openSpool(${l.lane})">${label}</button>`:'';
 if(l.spoolman_id)return `<br><span class="ok">Spoolman #${l.spoolman_id}</span> ${esc(l.spoolman_name)}`+
  (l.spoolman_remaining_g!=null?` · ${l.spoolman_remaining_g} g left`:'')+btn('Spoolman…');
 if(l.spoolman_error)return `<br><span class="warn">${esc(l.spoolman_error)}</span>`+btn('Link / create…');
 if(l.spoolman_pending&&tag)return '<br><span class="muted">Spoolman: syncing…</span>';
 return '';
}
async function clearLane(lane){
 if(!confirm(`Release the spool held in lane ${lane}? Its Spoolman location is cleared too.`))return;
 await fetch('/api/lane/clear?lane='+lane,{method:'POST'});refresh();
}
async function refresh(){
 let d;try{d=await (await fetch('/api/status')).json()}catch(e){$('device').innerHTML=kv('Connection','lost','bad');return}
 status=d;
 $('devTitle').textContent=d.device.name;
 const ap=d.wifi.ap_active;$('apBanner').hidden=!ap;
 if(ap)$('apBanner').textContent=`Setup hotspot "${d.wifi.ap_ssid}" is on. Enter your WiFi in Settings.`;
 const m=d.mqtt;
 $('device').innerHTML=kv('Firmware',esc(d.device.fw))+kv('Uptime',dur(d.device.uptime_s))+
  kv('WiFi',d.wifi.connected?esc(d.wifi.ssid)+' ('+d.wifi.rssi+' dBm)':'not connected',d.wifi.connected?'ok':'bad')+
  kv('IP',esc(d.wifi.ip||'-'))+kv('MQTT',m.connected?'connected':(m.host?'disconnected (rc '+m.state+')':'not configured'),m.connected?'ok':'bad')+
  kv('Broker',esc(m.host?m.host+':'+m.port:'-'))+kv('Printer link',d.printer.configured?(d.printer.connected?'connected':esc(d.printer.error||'disconnected')):'not configured',d.printer.connected?'ok':(d.printer.configured?'bad':'muted'))+kv('Spoolman',d.spoolman?'configured':'not configured',d.spoolman?'ok':'muted')+
  kv('Free memory',Math.round(d.device.heap/1024)+' KB');
 const s=d.sensors,f=d.fan,t=x=>x.ok?x.temp.toFixed(1)+' °C / '+x.hum.toFixed(1)+' %':(x.enabled?'read failed':'disabled');
 $('sensors').innerHTML=kv('Filament high',t(s.high),s.high.ok?'':'bad')+kv('Filament low',t(s.low),s.low.ok?'':'bad')+
  kv('Circulation fan',f.enabled?(f.running?'running at '+f.pct+'%':'off'):'disabled',f.running?'ok':'');
  $('lanes').innerHTML=d.lanes.filter(l=>l.enabled).map(l=>
  `<tr><td>${l.lane}</td><td>GPIO ${l.ss_pin}</td><td>${toolCell(l)}</td>
  <td class="${l.present?'ok':'bad'}">${l.present?'found':'NOT FOUND'} <span class="mono">${esc(l.version)}</span></td>
  <td><span class="mono">${esc(l.state)}</span>${l.state!='empty'&&l.state!='unknown'?(l.tag_visible?' <span class="ok">in view</span>':' <span class="muted">held, tag not in view</span>'):''}${l.spool?'<br>'+spoolLabel(l.spool):''}${spoolmanCell(l)}</td>
  <td>${l.last_type?esc(l.last_type)+' <span class="mono">'+esc(l.last_sak)+'</span>':'-'}</td>
  <td>${l.last_seen_s!==undefined?dur(l.last_seen_s)+' ago':'never'}</td><td>${l.hits}/${l.reads}</td>
  <td><div class="actions"><button class="sec sm" onclick="dump(${l.lane})" ${l.present&&!busy?'':'disabled'}>Read tag</button>
  <button class="sec sm" onclick="openWriter(${l.lane})" ${l.present&&!busy?'':'disabled'}>Write tag</button>
  ${l.state!='empty'&&l.state!='unknown'?`<button class="sec sm" onclick="clearLane(${l.lane})">Clear</button>`:''}</div></td></tr>`).join('');
 $('log').innerHTML=d.log.map(e=>`<div><span>${dur(e.t)}</span>${esc(e.msg)}</div>`).join('');
}
async function dump(lane){
 busy=true;$('dump').innerHTML='<p>Reading lane '+lane+'…</p>';
 try{
  const d=await (await fetch('/api/lane/dump?lane='+lane)).json();
  if(d.error){$('dump').innerHTML=`<p class="bad">Lane ${lane}: ${esc(d.error)}</p>`;return}
  let h=`<h3>Lane ${lane} tag</h3><div class="grid">${kv('UID',`<span class="mono">${esc(d.uid)}</span>`)+kv('UID length',d.uid_bytes+' bytes')+kv('Type',esc(d.type))+kv('SAK',`<span class="mono">${esc(d.sak)}</span>`)}</div>`;
  if(d.spool)h+=spoolGrid(d.spool);
  if(d.note)h+=`<p class="warn">${esc(d.note)}</p>`;
  if(d.blocks&&d.blocks.length){
   const unit=d.block_size==4?'Page':'Block';
   h+=`<details><summary>Raw memory (${d.blocks.length} ${unit.toLowerCase()}s)</summary><div class="scroll"><table class="mono"><thead><tr><th>${unit}</th><th>Hex</th><th>ASCII</th></tr></thead><tbody>`+
    d.blocks.map(b=>b.locked?`<tr><td>${b.n}–${b.n+3}</td><td colspan="2" class="warn">sector ${b.sector} locked</td></tr>`
    :`<tr><td>${b.n}</td><td>${esc(b.hex)}</td><td>${esc(b.ascii)}</td></tr>`).join('')+'</tbody></table></div>';
   const text=d.blocks.filter(b=>!b.locked).map(b=>b.n+': '+b.hex).join('\n');
   h+=`<details><summary>Copy as text</summary><pre>UID ${esc(d.uid)}  ${esc(d.type)}  SAK ${esc(d.sak)}\n${esc(text)}</pre></details></details>`;
  }
  $('dump').innerHTML=h;
 }catch(e){$('dump').innerHTML='<p class="bad">Read failed: '+esc(e)+'</p>'}
 finally{busy=false;refresh()}
}

// ── OpenTag3D writer ──
// [id, label, kind, required, extra]; kinds: text (extra = max bytes), num (extra = step), color, date
const REQUIRED=[['material','Material','text',0,5],['manufacturer','Manufacturer','text',0,16],['color_1','Color','color',0],
 ['diameter','Diameter (mm)','num',0,0.01],['print_temp','Print temp (°C)','num',0,5],['bed_temp','Bed temp (°C)','num',0,5],
 ['chamber_temp','Chamber temp (°C, 0 = none)','num',0,5],['density','Density (g/cm³)','num',0,0.01],['weight','Filament weight (g)','num',0,1]];
const OPTIONAL=[['material_mod','Material modifier (e.g. CF, Matte)','text',0,5],['color_name','Color name','text',0,32],
 ['color_2','Color 2','color',0],['color_3','Color 3','color',0],['color_4','Color 4','color',0],
 ['min_print_temp','Min print temp (°C)','num',0,5],['max_print_temp','Max print temp (°C)','num',0,5],
 ['min_bed_temp','Min bed temp (°C)','num',0,5],['max_bed_temp','Max bed temp (°C)','num',0,5],
 ['empty_spool_weight','Empty spool weight (g)','num',0,1],['measured_length','Length (m)','num',0,1],
 ['max_dry_temp','Drying temp (°C)','num',0,5],['dry_time','Drying time (h)','num',0,1],
 ['nozzle_diameter','Min nozzle diameter (mm)','num',0,0.1],['sku','SKU','text',0,16],['serial','Serial / lot','text',0,32],
 ['mfg_date','Manufacture date','date',0],['data_url','Info URL (without https://)','text',0,32]];
function fieldHtml([id,label,kind,req,extra]){
 const lab=`<span>${esc(label)}${req?' <b>*</b>':''}</span>`;
 if(kind=='color')return `<label>${lab}<div class="row" style="gap:6px;flex-wrap:nowrap"><input type="color" id="pick_${id}" oninput="$('f_${id}').value=this.value.toUpperCase()">
  <input id="f_${id}" placeholder="#RRGGBB" maxlength="9" oninput="if(/^#[0-9a-f]{6}/i.test(this.value))$('pick_${id}').value=this.value.slice(0,7)"></div></label>`;
 if(kind=='num')return `<label>${lab}<input id="f_${id}" type="number" step="${extra}" min="0"></label>`;
 if(kind=='date')return `<label>${lab}<input id="f_${id}" type="date"></label>`;
 return `<label>${lab}<input id="f_${id}" maxlength="${extra}"></label>`;
}
function setField(id,v){const el=$('f_'+id);if(!el)return;el.value=v??'';
 const p=$('pick_'+id);if(p&&/^#[0-9a-f]{6}/i.test(el.value))p.value=el.value.slice(0,7)}
function openWriter(lane){
 writerLane=lane;const w=$('writer');w.hidden=false;
 const sm=status&&status.spoolman;
 w.innerHTML=`<h2>Write OpenTag3D tag · lane ${lane}</h2>
 <p class="hint">Needs an NTAG215 or NTAG216 sticker on the lane ${lane} reader. Bambu tags can't be written (they're signed). Fill in what you know and leave the rest blank: blank fields stay off the tag, and you can write it again after calibrating.</p>
 ${sm?`<div class="row" style="margin-bottom:10px"><label style="margin:0;max-width:180px"><span>Spoolman spool #</span><input id="smId" type="number" min="1"></label>
  <button class="sec" style="margin-top:18px" onclick="loadSpoolman()">Load from Spoolman</button><span id="smMsg" style="margin-top:18px"></span></div>`
  :'<p class="hint">Set a Spoolman URL in Settings to pre-fill this form from a spool.</p>'}
 <h3>Filament</h3><div class="grid">${REQUIRED.map(fieldHtml).join('')}</div>
 <details><summary>More fields</summary><div class="grid" style="margin-top:10px">${OPTIONAL.map(fieldHtml).join('')}</div></details>
 ${sm?`<label class="row" style="gap:6px;margin-top:10px"><input type="checkbox" id="smSave" disabled> Save the tag ID to the loaded Spoolman spool (field "tag")</label>`:''}
 <div class="row" style="margin-top:12px"><button id="wBtn" onclick="writeTag()">Write tag</button><button class="sec" onclick="$('writer').hidden=true">Close</button><span id="wMsg"></span></div>`;
 setField('diameter',1.75);
 // Start from what's on the tag already, then the lane's Spoolman spool
 // (Spoolman wins where it has a value: it's where calibrations get saved)
 const l=status&&status.lanes.find(x=>x.lane==lane);
 if(l&&l.spool&&l.spool.format=='opentag3d')for(const [k,v] of Object.entries(l.spool))setField(k,v);
 if(sm&&l&&l.spoolman_id){$('smId').value=l.spoolman_id;loadSpoolman()}
 w.scrollIntoView({behavior:'smooth'});
}
async function loadSpoolman(){
 const id=$('smId').value;if(!id)return;$('smMsg').className='';$('smMsg').textContent='Loading…';
 const d=await (await fetch('/api/spoolman/spool?id='+id)).json().catch(()=>({error:'Request failed'}));
 if(!d.ok){$('smMsg').className='bad';$('smMsg').textContent=d.error;return}
 for(const [k,v] of Object.entries(d.fields))setField(k,v);
 const s=d.spool;$('smMsg').className='ok';
 $('smMsg').textContent=`Loaded #${s.id}: ${[s.vendor,s.name,s.material].filter(x=>x).join(' · ')}`+(s.remaining_weight!=null?` (${Math.round(s.remaining_weight)} g left)`:'');
 $('smSave').disabled=false;$('smSave').checked=true;$('smSave').dataset.id=s.id;
}
async function writeTag(){
 const fields={};
 for(const [id,,kind] of REQUIRED.concat(OPTIONAL)){const v=$('f_'+id).value.trim();if(v==='')continue;
  fields[id]=kind=='num'?Number(v):v}
 const body={fields};const save=$('smSave');if(save&&save.checked)body.spoolman_id=Number(save.dataset.id);
 busy=true;$('wBtn').disabled=true;$('wMsg').className='';$('wMsg').textContent='Writing…';
 try{
  const r=await fetch('/api/lane/write?lane='+writerLane,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
  const d=await r.json().catch(()=>({error:'HTTP '+r.status}));
  if(d.ok){$('wMsg').className='ok';$('wMsg').textContent=`Written and verified to tag ${d.uid}.`+(d.spoolman?' '+d.spoolman+'.':'')+(d.spoolman_error?' Spoolman: '+d.spoolman_error:'')}
  else{$('wMsg').className='bad';$('wMsg').textContent=d.error||'Write failed'}
 }finally{busy=false;$('wBtn').disabled=false;refresh()}
}
// ── Spoolman dialog: link an existing spool, create one, or edit the linked one ──
let dlg=null;
async function smFetch(url,opt){
 const r=await fetch(url,opt);const d=await r.json().catch(()=>null);
 if(!r.ok||(d&&d.ok===false)){const m=d&&(d.error||d.message||(d.detail&&JSON.stringify(d.detail)));throw new Error(m||'HTTP '+r.status)}
 return d;
}
const smGet=p=>smFetch('/api/spoolman/relay?path='+encodeURIComponent(p));
const smSend=(method,p,body)=>smFetch(`/api/spoolman/relay?method=${method}&path=${encodeURIComponent(p)}`,
 {method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
const post=(url,body)=>smFetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body||{})});
const exStr=v=>{if(v==null)return '';try{const j=JSON.parse(v);return typeof j=='string'?j:String(j)}catch(e){return String(v)}};
const filLabel=f=>f?[f.vendor&&f.vendor.name,f.name,f.material&&'('+f.material+')'].filter(x=>x).join(' '):'';
const filColor=f=>f&&(f.color_hex?'#'+f.color_hex:f.multi_color_hexes?'#'+f.multi_color_hexes.split(',')[0]:'');
const spLabel=s=>`#${s.id} ${filLabel(s.filament)}`;
const g=v=>v==null||v===''?'':Math.round(v);
const smLink=p=>status.spoolman_url?`<a href="${esc(status.spoolman_url+p)}" target="_blank" rel="noopener">`:'<a>';

async function openSpool(lane,tab){
 const l=status.lanes.find(x=>x.lane==lane);
 dlg={lane,l,tab:tab||(l.spoolman_id?'edit':'existing'),spools:null,filaments:null,plan:null,q:'',fq:'',sel:0,err:'',msg:''};
 drawDlg();await loadDlg();
}
async function loadDlg(){
 dlg.err='';dlg.spools=null;drawDlg();
 try{
  const [sp,fi,ve]=await Promise.all([smGet('/api/v1/spool?allow_archived=false'),smGet('/api/v1/filament'),smGet('/api/v1/vendor')]);
  dlg.vendors=ve;
  dlg.spools=sp.sort((a,b)=>b.id-a.id);
  dlg.filaments=fi.sort((a,b)=>filLabel(a).localeCompare(filLabel(b)));
  const key=(dlg.l.spool_key||'').toLowerCase();
  dlg.cur=key&&dlg.spools.find(s=>exStr(s.extra&&s.extra.tag).toLowerCase()==key)||null;
  if(dlg.l.spool){try{dlg.plan=(await post(`/api/lane/create?lane=${dlg.lane}&dry_run=1`)).plan}catch(e){dlg.plan=null}}
  if(dlg.tab=='edit'&&!dlg.cur)dlg.tab='existing';
 }catch(e){dlg.err='Couldn\'t load from Spoolman: '+e.message}
 drawDlg();
}
function closeDlg(){dlg=null;$('spoolDlg').hidden=true;$('spoolDlg').innerHTML='';refresh()}
function setTab(t){dlg.tab=t;dlg.msg='';drawDlg()}
function drawDlg(){
 const box=$('spoolDlg');box.hidden=false;
 const l=dlg.l,cur=dlg.cur;
 const tabs=[['existing','Existing spool'],['new','New spool'],...(cur?[['edit','Edit linked spool']]:[])];
 let h=`<div class="card" onclick="event.stopPropagation()"><div class="row" style="justify-content:space-between"><h2 style="margin:0">Spoolman · lane ${dlg.lane}</h2><button class="sec sm" onclick="closeDlg()">Close</button></div>
 <p class="hint">Tag <span class="mono">${esc(l.spool_key||l.state)}</span>${l.spool?' · '+spoolLabel(l.spool):''}<br>`+
 (cur?`Linked to <b>${esc(spLabel(cur))}</b>${cur.remaining_weight!=null?' · '+g(cur.remaining_weight)+' g left':''} · ${smLink('/spool/show/'+cur.id)}open in Spoolman ↗</a>`:'Not linked to a Spoolman spool yet.')+`</p>`;
 if(dlg.err)return box.innerHTML=h+`<p class="bad">${esc(dlg.err)}</p><button onclick="loadDlg()">Retry</button></div>`;
 if(!dlg.spools)return box.innerHTML=h+'<p class="muted">Loading spools and filaments from Spoolman…</p></div>';
 h+=`<div class="tabs">${tabs.map(([k,t])=>`<button class="${dlg.tab==k?'on':''}" onclick="setTab('${k}')">${t}</button>`).join('')}</div>`;
 h+=dlg.tab=='existing'?tabExisting():dlg.tab=='new'?tabNew():tabEdit();
 h+=`<div class="row" style="margin-top:10px"><span id="dlgMsg">${dlg.msg}</span></div></div>`;
 box.innerHTML=h;
 if(dlg.tab=='existing')drawSpoolList();
 if(dlg.tab!='existing')drawFilList();
}
const moveNote=()=>dlg.cur?`<label class="row" style="gap:6px"><input type="checkbox" id="archOld" checked> Archive the old spool #${dlg.cur.id} (its roll is used up). The tag moves from #${dlg.cur.id} to the new spool either way.</label>`:'';

// Existing spool
function tabExisting(){
 return `<input id="spQ" placeholder="Search by number, vendor, name, material, color, location" value="${esc(dlg.q)}" oninput="dlg.q=this.value;drawSpoolList()">
 <div class="pick" id="spList" style="margin-top:8px"></div><div id="spConfirm" style="margin-top:10px"></div>
 <p class="hint">Spool not in Spoolman yet? Use <a href="#" onclick="setTab('new');return false">New spool</a>.</p>`;
}
function drawSpoolList(){
 const q=dlg.q.trim().toLowerCase();
 const list=dlg.spools.filter(s=>!dlg.cur||s.id!=dlg.cur.id).filter(s=>!q||(`#${s.id} ${filLabel(s.filament)} ${s.filament.color_hex||''} ${s.location||''} ${s.comment||''}`).toLowerCase().includes(q));
 $('spList').innerHTML=list.slice(0,150).map(s=>{
  const tag=exStr(s.extra&&s.extra.tag);
  return `<div class="${dlg.sel==s.id?'sel':''}" onclick="dlg.sel=${s.id};drawSpoolList()">${swatch(filColor(s.filament))}<b>#${s.id}</b> ${esc(filLabel(s.filament))}
  <span class="muted"> · ${s.remaining_weight!=null?g(s.remaining_weight)+' g':'? g'}${s.location?' · '+esc(s.location):''}</span>${tag?'<span class="badge">has a tag</span>':''}</div>`}).join('')
  ||'<div class="muted">No matching spools</div>';
 const s=dlg.spools.find(x=>x.id==dlg.sel);
 $('spConfirm').innerHTML=!s?'<span class="muted">Pick the spool that\'s in lane '+dlg.lane+'.</span>':
  `${exStr(s.extra&&s.extra.tag)?`<p class="warn">#${s.id} is linked to another tag (<span class="mono">${esc(exStr(s.extra.tag))}</span>). Linking replaces it with this lane's tag.</p>`:''}${moveNote()}
  <button onclick="doLink(${s.id})">Link spool #${s.id} to lane ${dlg.lane}</button>`;
}
async function doLink(id){
 dlgBusy('Linking…');
 try{await post(`/api/lane/link?lane=${dlg.lane}`,{spool_id:id,move:!!dlg.cur,archive_old:!!($('archOld')&&$('archOld').checked)});closeDlg()}
 catch(e){dlgFail(e)}
}

// SpoolmanDB, the catalog Spoolman pulls from: fetched through the relay
// (gzipped, ~150 KB) once per page load and searched in the browser
let catalog=null,catalogErr='',catalogLoading=false;
function loadCatalog(){
 if(catalog||catalogLoading)return;catalogLoading=true;catalogErr='';
 smGet('/api/v1/external/filament').then(d=>{catalog=d.sort((a,b)=>(a.diameter!=1.75)-(b.diameter!=1.75))})
  .catch(e=>{catalogErr=e.message}).finally(()=>{catalogLoading=false;if(dlg)drawFilList()});
}
const words=q=>q.trim().toLowerCase().split(/\s+/).filter(x=>x);
const hit=(text,ws)=>{text=text.toLowerCase();return ws.every(w=>text.includes(w))};
const wt=w=>w?(w>=1000?(w/1000).toFixed(w%1000?1:0)+' kg':Math.round(w)+' g'):'';
const extEntry=key=>catalog&&catalog.find(e=>'ext:'+e.id==key);
const extColor=e=>e.color_hex?'#'+e.color_hex:e.color_hexes&&e.color_hexes.length?'#'+e.color_hexes[0]:'';
const DENS=[['PETG',1.27],['PET',1.27],['PLA',1.24],['ABS',1.04],['ASA',1.07],['TPU',1.21],['PC',1.2],['PA',1.14],['PVA',1.23],['HIPS',1.04]];
const density=m=>{m=(m||'').toUpperCase();const d=DENS.find(([p])=>m.startsWith(p));return d?d[1]:1.24};

// Filament picker: New tab searches your filaments + SpoolmanDB; Edit tab only yours
function filPicker(selId){
 dlg.filSel=selId!=null?selId:(dlg.filSel??null);
 const nw=dlg.tab=='new';
 if(nw)loadCatalog();
 return `<label><span>Filament</span><input id="filQ" placeholder="${nw?'Search your filaments and SpoolmanDB, e.g. jayo matte':'Search your filaments'}" value="${esc(dlg.fq)}" oninput="dlg.fq=this.value;drawFilList()"></label>
 <div class="pick" id="filList" style="max-height:300px"></div>
 ${nw?`<div class="newfil" id="newFilBtn" onclick="pickFil('new')">＋ Create a new filament <span class="muted">not in your filaments or SpoolmanDB</span></div><div id="newFilForm"></div>`
  :`<div class="hint">Filament missing? Add it in ${smLink('/')}Spoolman ↗</a>, then <a href="#" onclick="loadDlg();return false">reload the lists</a>.</div>`}`;
}
function filRow(key,sw,title,sub,right,badge){
 const k=typeof key=='number'?key:`'${key}'`;
 return `<div class="frow ${dlg.filSel==key?'sel':''}" onclick="pickFil(${k})">${swatch(sw)}<span class="grow"><b>${esc(title)}</b> <span class="muted">${esc(sub)}</span></span><span class="muted">${right}</span>${badge?`<span class="badge">${badge}</span>`:''}</div>`;
}
function drawFilList(){
 if(!$('filList'))return;
 const ws=words(dlg.fq),nw=dlg.tab=='new';
 let h='';
 if(nw&&dlg.plan&&dlg.plan.new_filament){const nf=dlg.plan.new_filament;
  h+=filRow('tag',nf.color_hex?'#'+nf.color_hex:'','From the tag: '+nf.name,dlg.plan.vendor+' · '+nf.material,wt(nf.weight),'new')}
 const mine=dlg.filaments.filter(f=>!ws.length||hit(`#${f.id} ${filLabel(f)} ${f.color_hex||''} ${f.article_number||''}`,ws));
 if(nw)h+='<div class="shead">Your filaments</div>';
 h+=mine.slice(0,60).map(f=>filRow(f.id,filColor(f),f.name||'(no name)',[f.vendor&&f.vendor.name,f.material,'#'+f.id].filter(x=>x).join(' · '),wt(f.weight))).join('')
  ||'<div class="shead">No matches</div>';
 if(nw){
  h+='<div class="shead">SpoolmanDB</div>';
  if(!catalog)h+=`<div class="shead">${catalogErr?'SpoolmanDB unavailable: '+esc(catalogErr):'Loading SpoolmanDB…'}</div>`;
  else if(!ws.length)h+=`<div class="shead">Type to search ${catalog.length.toLocaleString()} filaments</div>`;
  else{
   const have=new Set(dlg.filaments.map(f=>f.external_id).filter(x=>x));
   const ext=catalog.filter(e=>!have.has(e.id)&&hit(`${e.manufacturer} ${e.name} ${e.material} ${e.finish||''} ${e.color_hex||''}`,ws));
   h+=ext.slice(0,60).map(e=>filRow('ext:'+e.id,extColor(e),e.name,e.manufacturer+' · '+e.material,
    wt(e.weight)+(e.diameter!=1.75?' · '+e.diameter+' mm':''),'SpoolmanDB')).join('')||'<div class="shead">No matches</div>';
   if(ext.length>60)h+=`<div class="shead">${ext.length-60} more: add words to narrow it down</div>`;
  }
 }
 $('filList').innerHTML=h;
}
// Weight defaults for a selection: {weight, spool, vendor}
function selDefaults(key){
 if(typeof key=='number'){const f=dlg.filaments.find(x=>x.id==key);return f?{weight:f.weight,spool:filSpoolWeight(f),vendor:f.vendor&&f.vendor.name}:{}}
 if(key=='tag'){const nf=dlg.plan.new_filament;return {weight:nf.weight,spool:nf.spool_weight,vendor:dlg.plan.vendor}}
 const e=extEntry(key);return e?{weight:e.weight,spool:e.spool_weight,vendor:e.manufacturer}:{};
}
function pickFil(key){
 dlg.filSel=key;drawFilList();
 if(dlg.tab!='new')return;
 drawNewForm();
 const d=selDefaults(key); // defaults follow the filament, unless set by hand
 if(!$('nInit').dataset.typed&&d.weight)$('nInit').value=g(d.weight);
 if(!dlg.cur&&!$('nSpool').dataset.typed&&d.spool)setSpoolWeight('n',d.spool,d.vendor);
}
function drawNewForm(){
 const box=$('newFilForm');if(!box)return;
 $('newFilBtn').classList.toggle('sel',dlg.filSel=='new');
 if(dlg.filSel!='new'){box.innerHTML='';return}
 if(box.innerHTML)return; // keep what was typed
 const mats=[...new Set([...dlg.filaments.map(f=>f.material),...(catalog||[]).map(e=>e.material)].filter(x=>x))].sort();
 box.innerHTML=`<div class="grid" style="margin-top:8px">
 <label><span>Filament vendor</span><input id="cfVendor" list="cfVendors" placeholder="e.g. JAYO"><datalist id="cfVendors">${dlg.vendors.map(v=>`<option value="${esc(v.name)}">`).join('')}</datalist></label>
 <label><span>Material</span><input id="cfMat" list="cfMats" placeholder="e.g. PLA, PETG, PLA-CF" oninput="$('cfDens').textContent=density(this.value)"><datalist id="cfMats">${mats.map(m=>`<option value="${esc(m)}">`).join('')}</datalist></label>
 <label><span>Color</span><div class="row" style="flex-wrap:nowrap;gap:6px"><input type="color" id="cfColor" value="#808080"><input id="cfName" placeholder="Color name, e.g. Teal"></div></label></div>
 <div class="hint">Saved with density <span id="cfDens">1.24</span> g/cm³ (typical for the material) and 1.75 mm; fine-tune in Spoolman if needed.</div>`;
}

// Empty spool weights known to Spoolman, per manufacturer: from vendors,
// filaments and existing spools. A refill on another brand's spool (Jayo on a
// Bambu spool) picks the spool's maker here.
const filSpoolWeight=f=>f?f.spool_weight??(f.vendor&&f.vendor.empty_spool_weight):null;
function spoolWeights(){
 const m=new Map();
 const add=(v,w)=>{if(!v||!(w>0))return;w=Math.round(w);const k=v+'|'+w;const o=m.get(k)||{vendor:v,w,n:0};o.n++;m.set(k,o)};
 for(const f of dlg.filaments){const v=f.vendor&&f.vendor.name;add(v,f.vendor&&f.vendor.empty_spool_weight);add(v,f.spool_weight)}
 for(const s of dlg.spools)add(s.filament.vendor&&s.filament.vendor.name,s.spool_weight);
 return [...m.values()].sort((a,b)=>a.vendor.localeCompare(b.vendor)||b.n-a.n);
}
function spoolField(pre,w,vendor){
 const opts=spoolWeights();
 return `<label><span>Empty spool weight (g)</span><div class="row" style="flex-wrap:nowrap;gap:6px">
 <select id="${pre}SpoolSel" onchange="if(this.value){$('${pre}Spool').value=this.value.split('|')[1];$('${pre}Spool').dataset.typed=1}">
 <option value="">Other / custom</option>${opts.map(o=>`<option value="${esc(o.vendor+'|'+o.w)}">${esc(o.vendor)} spool · ${o.w} g</option>`).join('')}</select>
 <input id="${pre}Spool" type="number" min="0" step="any" style="max-width:90px" oninput="this.dataset.typed=1;$('${pre}SpoolSel').value=''"></div>
 <div class="hint">Pick whose empty spool it's on, or type a weight.</div></label>`;
}
// Show weight w, selecting the matching manufacturer entry (vendor's first)
function setSpoolWeight(pre,w,vendor){
 const sel=$(pre+'SpoolSel'),inp=$(pre+'Spool');if(!sel)return;
 inp.value=w>0?Math.round(w):'';
 const vals=[...sel.options].map(o=>o.value);
 sel.value=w>0?(vals.find(v=>v==vendor+'|'+Math.round(w))||vals.find(v=>v.endsWith('|'+Math.round(w)))||''):'';
}
const num=(id,label,v,hint)=>`<label><span>${label}</span><input id="${id}" type="number" min="0" step="any" value="${esc(v)}" oninput="this.dataset.typed=1">${hint?`<div class="hint">${hint}</div>`:''}</label>`;
const txt=(id,label,v)=>`<label><span>${label}</span><input id="${id}" value="${esc(v)}"></label>`;
const optNum=id=>{const v=$(id).value.trim();return v===''?undefined:Number(v)};

// New spool
function tabNew(){
 const cur=dlg.cur,p=dlg.plan;
 const pre=dlg.filSel??(p&&p.filament_id)??(p&&p.new_filament?'tag':null)??(cur&&cur.filament.id)??null;
 const d=pre!=null?selDefaults(pre):{};
 setTimeout(()=>{setSpoolWeight('n',cur?cur.spool_weight??cur.filament.spool_weight:d.spool,cur?'':d.vendor);drawNewForm()});
 const chips=[250,500,750,1000,2000].map(w=>`<button type="button" class="sec sm" onclick="$('nInit').value=${w};$('nInit').dataset.typed=1">${wt(w)}</button>`).join(' ');
 return `${cur?`<p class="hint">New roll on a reusable spool? Pick its filament here: the new spool gets this lane's tag, and the empty spool weight carries over from #${cur.id}.</p>`:''}
 ${filPicker(pre)}
 <div class="grid" style="margin-top:12px"><label><span>Filament weight (g)</span><input id="nInit" type="number" min="0" step="any" value="${g(d.weight)||1000}" oninput="this.dataset.typed=1">
 <div class="row" style="gap:4px;margin-top:5px">${chips}</div></label>
 ${spoolField('n')}${txt('nComment','Comment (optional)','')}</div>
 <p class="hint">Price, lot number and the rest can be added in Spoolman later.</p>
 ${moveNote()}
 <button onclick="doCreate()">Create spool and link to lane ${dlg.lane}</button>`;
}
const clean=o=>Object.fromEntries(Object.entries(o).filter(([,v])=>v!=null&&v!==''));
async function vendorId(name,extId){
 name=(name||'').trim();if(!name)return undefined;
 const v=dlg.vendors.find(x=>x.name.toLowerCase()==name.toLowerCase());if(v)return v.id;
 const c=await smSend('POST','/api/v1/vendor',clean({name,external_id:extId}));dlg.vendors.push(c);return c.id;
}
// Add a SpoolmanDB filament to your Spoolman, the way Spoolman's own "Add
// spools" does (linked by external_id, so it's never added twice)
async function filamentFromCatalog(e){
 if(!e)throw new Error('SpoolmanDB entry not loaded');
 const have=dlg.filaments.find(f=>f.external_id==e.id);if(have)return have.id;
 const multi=e.color_hexes&&e.color_hexes.length>1;
 const f=await smSend('POST','/api/v1/filament',clean({name:e.name,vendor_id:await vendorId(e.manufacturer,e.manufacturer),material:e.material,
  density:e.density,diameter:e.diameter,weight:e.weight,spool_weight:e.spool_weight,
  color_hex:multi?null:extColor(e).slice(1),multi_color_hexes:multi?e.color_hexes.join(','):null,
  multi_color_direction:multi?(e.multi_color_direction||'coaxial'):null,
  settings_extruder_temp:e.extruder_temp,settings_bed_temp:e.bed_temp,external_id:e.id}));
 dlg.filaments.push(f);return f.id;
}
async function filamentFromForm(){
 const mat=$('cfMat').value.trim(),name=$('cfName').value.trim();
 if(!mat)throw new Error('Enter the material for the new filament');
 const f=await smSend('POST','/api/v1/filament',clean({name:name||mat,vendor_id:await vendorId($('cfVendor').value),material:mat,
  density:density(mat),diameter:1.75,weight:optNum('nInit'),color_hex:$('cfColor').value.slice(1).toUpperCase()}));
 dlg.filaments.push(f);return f.id;
}
async function doCreate(){
 const sel=dlg.filSel;
 if(sel==null){dlgFail(new Error('Pick a filament'));return}
 const body={initial_weight:optNum('nInit'),spool_weight:optNum('nSpool'),comment:$('nComment').value.trim(),
  move:!!dlg.cur,archive_old:!!($('archOld')&&$('archOld').checked)};
 dlgBusy('Creating…');
 try{
  if(typeof sel=='number')body.filament_id=sel;
  else if(sel=='new')body.filament_id=await filamentFromForm();
  else if(sel!='tag')body.filament_id=await filamentFromCatalog(extEntry(sel));
  const d=await post(`/api/lane/create?lane=${dlg.lane}`,body);
  closeDlg();if(d.spool_id)showToast(`Created Spoolman spool #${d.spool_id}`);
 }catch(e){dlgFail(e)}
}

// Edit linked spool
function tabEdit(){
 const s=dlg.cur;
 setTimeout(()=>setSpoolWeight('e',s.spool_weight??s.filament.spool_weight,''));
 return `${filPicker(s.filament.id)}
 <div class="grid" style="margin-top:10px">${num('eRem','Remaining filament (g)',g(s.remaining_weight))}${num('eInit','Filament weight when new (g)',g(s.initial_weight??s.filament.weight))}
 ${spoolField('e')}${txt('eComment','Comment',s.comment||'')}</div>
 <div class="row"><button onclick="doEdit()">Save to Spoolman</button>
 <button class="sec" onclick="newRoll()">New roll on this spool…</button>${smLink('/spool/show/'+s.id)}All fields in Spoolman ↗</a></div>`;
}
function newRoll(){dlg.filSel=dlg.cur.filament.id;setTab('new')}
async function doEdit(){
 const s=dlg.cur,init=optNum('eInit'),rem=optNum('eRem');
 const body={filament_id:dlg.filSel,initial_weight:init??null,spool_weight:optNum('eSpool')??null,comment:$('eComment').value.trim()||null};
 const base=init??s.filament.weight;
 if(rem!=null&&base)body.used_weight=Math.max(0,base-rem);
 dlgBusy('Saving…');
 try{await smSend('PATCH','/api/v1/spool/'+s.id,body);await post('/api/spoolman/refresh');closeDlg();showToast(`Saved spool #${s.id}`)}
 catch(e){dlgFail(e)}
}
function dlgBusy(m){$('dlgMsg').className='muted';$('dlgMsg').textContent=m;document.querySelectorAll('#spoolDlg button').forEach(b=>b.disabled=true)}
function dlgFail(e){$('dlgMsg').className='bad';$('dlgMsg').textContent=e.message;document.querySelectorAll('#spoolDlg button').forEach(b=>b.disabled=false)}
function showToast(m){const t=$('toast');t.textContent=m;t.hidden=false;clearTimeout(t._h);t._h=setTimeout(()=>t.hidden=true,4000)}
refresh();setInterval(()=>{if(!busy)refresh()},2000);
</script></body></html>)HTML";

static const char SETTINGS_HTML[] PROGMEM = PAGE_HEAD("StorOn Settings") PAGE_HEADER("", "", " class=\"on\"") R"HTML(
<main><form id="f">
<div class="card"><h2>Device</h2><div class="grid">
<label><span>Name</span><input name="device_name" maxlength="31"><div class="hint">Shown in Home Assistant</div></label>
<label><span>Device ID</span><input name="device_id" maxlength="31" pattern="[a-z0-9_]+"><div class="hint">MQTT topic prefix. a-z, 0-9, _ only. Changing it creates new entities in Home Assistant.</div></label>
</div></div>
<div class="card"><h2>WiFi</h2><div class="grid">
<label><span>Network name (SSID)</span><input name="wifi_ssid" maxlength="32"></label>
<label><span>Password</span><input name="wifi_pass" type="password" maxlength="64" autocomplete="new-password" data-secret></label>
</div></div>
<div class="card"><h2>MQTT broker</h2><div class="grid">
<label><span>Host or IP</span><input name="mqtt_host" maxlength="63"></label>
<label><span>Port</span><input name="mqtt_port" type="number" min="1" max="65535"></label>
<label><span>Username</span><input name="mqtt_user" maxlength="32" autocomplete="off"></label>
<label><span>Password</span><input name="mqtt_pass" type="password" maxlength="64" autocomplete="new-password" data-secret></label>
</div></div>
<div class="card"><h2>Spoolman</h2>
<label><span>Spoolman URL</span><input name="spoolman_url" maxlength="95" placeholder="http://192.168.1.10:7912">
<div class="hint">Optional. With it, each lane finds its spool in Spoolman (by the spool's "tag" field) and the tag writer can pre-fill from a spool.</div></label>
<label><span>Location prefix</span><input name="spoolman_location" maxlength="31" placeholder="StorOn">
<div class="hint">A spool in lane 3 gets the Spoolman location "&lt;prefix&gt; Lane 3". Leave empty to not touch locations.</div></label>
<label><span>Location after leaving a lane</span><input name="spoolman_away_location" maxlength="31" placeholder="StorOn Shelves">
<div class="hint">Where a spool goes when it's unloaded, cleared or replaced in its lane. Empty clears the location. Only applied if the spool still shows its lane.</div></label>
</div>
<div class="card"><h2>Printer (Moonraker)</h2>
<label><span>Moonraker URL</span><input name="moonraker_url" maxlength="95" placeholder="http://192.168.1.20">
<div class="hint">Optional. With storon.cfg on the printer, a tool's unload releases its lane's spool. Set which tool each lane feeds below.</div></label>
<label><span>Load command</span><input name="load_command" maxlength="95" placeholder="LOAD_FILAMENT TOOL={tool} TEMP={temp}">
<div class="hint">Optional: adds a Load button to each lane. {tool}, {lane}, {temp} and {material} are filled in; temp and material come from the lane's Spoolman filament or its tag, and a word whose value isn't known is left out. Buttons are disabled while the printer is printing or busy.</div></label>
<label><span>Unload command</span><input name="unload_command" maxlength="95" placeholder="UNLOAD_FILAMENT TOOL={tool} TEMP={temp}">
<div class="hint">Optional: adds an Unload button to each lane.</div></label>
</div>
<div class="card"><h2>RFID lanes</h2><p class="hint">One RC522 reader per lane. Disable lanes without a reader; lane numbers stay the same either way.</p>
<div class="scroll"><table><thead><tr><th>Lane</th><th>Enabled</th><th>SS pin (board "SDA")</th><th>Feeds tool (T#, -1 = none)</th></tr></thead><tbody id="laneRows"></tbody></table></div>
<div class="grid" style="margin-top:12px">
<label><span>Poll every lane once per (ms)</span><input name="rfid_cycle_ms" type="number" min="200" max="60000">
<div class="hint">500 catches tags passing by as a spool feeds.</div></label>
<label><span>Release a lane after its tag is unseen for (min)</span><input name="lane_release_min" type="number" min="0" max="10080">
<div class="hint">0 = never. Tags often sit out of view, so lanes normally hold their spool until a different tag is read, you click Clear, or the printer unloads it.</div></label>
</div>
</div>
<div class="card"><h2>Pins</h2><p class="hint" id="allowed"></p><div class="grid">
<label><span>DHT22 high</span><input name="pin_dht_high" type="number"></label>
<label><span>DHT22 low</span><input name="pin_dht_low" type="number"></label>
<label><span>Fan (PWM)</span><input name="pin_fan" type="number"></label>
<label><span>Status LED (WS2812)</span><input name="pin_led" type="number"></label>
<label><span>SPI SCK</span><input name="pin_sck" type="number"></label>
<label><span>SPI MISO</span><input name="pin_miso" type="number"></label>
<label><span>SPI MOSI</span><input name="pin_mosi" type="number"></label>
</div><p class="hint">Use -1 for anything not connected (SPI pins are required).</p></div>
<div class="card"><h2>Timing</h2><div class="grid">
<label><span>Read DHT22 sensors every (s)</span><input name="sensor_interval_s" type="number" min="5" max="3600"></label>
<label><span>Start a fan run every (min)</span><input name="fan_cycle_min" type="number" min="1" max="1440"></label>
<label><span>Each fan run lasts (min)</span><input name="fan_on_min" type="number" min="0" max="1440"></label>
<label><span>Status LED brightness (0-255)</span><input name="led_brightness" type="number" min="0" max="255"></label>
</div></div>
<div class="card"><h2>Web UI password</h2>
<label><span>New password for user "admin"</span><input name="admin_pass" type="password" maxlength="32" autocomplete="new-password" data-secret></label>
<label class="row" style="gap:6px"><input type="checkbox" name="clear_admin_pass"> Remove the password</label>
<p class="hint" id="adminHint"></p></div>
<div class="row"><button type="submit">Save and reboot</button><span id="msg"></span></div>
</form>
<div class="card" style="margin-top:14px"><h2>Maintenance</h2>
<label><span>Firmware update (.bin from <code>.pio/build/…/firmware.bin</code>)</span><input type="file" id="fw" accept=".bin"></label>
<div class="row"><button id="up" class="sec">Upload firmware</button><span id="upMsg"></span></div>
<div class="row" style="margin-top:14px"><button id="reboot" class="sec">Reboot</button><button id="reset" class="danger">Factory reset</button></div>
<p class="hint">Factory reset erases all settings, including WiFi. The board then starts its setup hotspot. Holding the BOOT button for 10 seconds does the same.</p>
</div></main>
<script>
const $=id=>document.getElementById(id),f=$('f');
const nums=['lane_release_min','mqtt_port','pin_dht_high','pin_dht_low','pin_fan','pin_led','pin_sck','pin_miso','pin_mosi','rfid_cycle_ms','sensor_interval_s','fan_cycle_min','fan_on_min','led_brightness'];
const isSecret=el=>'secret' in el.dataset;
async function load(){
 const r=await fetch('/api/settings');if(r.status==401){$('msg').textContent='Password required';return}
 const s=await r.json();
 for(const el of f.elements){if(!el.name||el.type=='checkbox')continue;
  if(isSecret(el)){el.value='';el.placeholder=s[el.name+'_set']?'(saved, leave blank to keep)':'(not set)';continue}
  if(s[el.name]!==undefined)el.value=s[el.name]}
 $('laneRows').innerHTML=s.lanes.map((l,i)=>`<tr><td>${i+1}</td>
  <td><input type="checkbox" id="lane_en_${i}" ${l.enabled?'checked':''}></td>
  <td><input type="number" id="lane_ss_${i}" value="${l.ss}" style="max-width:110px"></td>
  <td><input type="number" id="lane_tool_${i}" value="${l.tool}" min="-1" style="max-width:110px"></td></tr>`).join('');
 $('allowed').textContent='Usable GPIOs on this board: '+s.allowed_pins.join(', ');
 $('adminHint').textContent=s.admin_pass_set?'A password is set.':'No password is set: anyone on your network can change these settings.';
}
f.onsubmit=async e=>{e.preventDefault();const body={};
 for(const el of f.elements){if(!el.name)continue;
  if(el.type=='checkbox'){if(el.checked)body[el.name]=true;continue}
  if(isSecret(el)){if(el.value)body[el.name]=el.value;continue}
  body[el.name]=nums.includes(el.name)?Number(el.value):el.value}
 body.lanes=[];for(let i=0;$('lane_en_'+i);i++)body.lanes.push({enabled:$('lane_en_'+i).checked,ss:Number($('lane_ss_'+i).value),tool:Number($('lane_tool_'+i).value)});
 $('msg').className='';$('msg').textContent='Saving…';
 const r=await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
 const d=await r.json().catch(()=>({error:'HTTP '+r.status}));
 if(d.ok){$('msg').className='ok';$('msg').textContent='Saved. Rebooting… '+(d.hint||'');setTimeout(()=>location.href='/',8000)}
 else{$('msg').className='bad';$('msg').textContent=d.error||'Save failed'}};
$('reboot').onclick=async()=>{if(!confirm('Reboot now?'))return;await fetch('/api/reboot',{method:'POST'});$('msg').textContent='Rebooting…';setTimeout(()=>location.href='/',8000)};
$('reset').onclick=async()=>{if(!confirm('Erase ALL settings, including WiFi?'))return;await fetch('/api/factory-reset',{method:'POST'});alert('Settings erased. Connect to the board\'s setup hotspot to configure it again.')};
$('up').onclick=()=>{const file=$('fw').files[0];if(!file){$('upMsg').textContent='Choose a .bin file first';return}
 const x=new XMLHttpRequest(),fd=new FormData();fd.append('firmware',file);
 x.upload.onprogress=e=>$('upMsg').textContent='Uploading '+Math.round(e.loaded/e.total*100)+'%';
 x.onload=()=>{let d={};try{d=JSON.parse(x.responseText)}catch(e){}
  $('upMsg').textContent=d.ok?'Done. Rebooting…':'Update failed: '+(d.error||x.status);if(d.ok)setTimeout(()=>location.href='/',10000)};
 x.open('POST','/update');x.send(fd)};
load();
</script></body></html>)HTML";


static const char READERS_HTML[] PROGMEM = PAGE_HEAD("StorOn Reader Check") PAGE_HEADER("", " class=\"on\"", "") R"HTML(
<main>
<div class="card"><h2>Reader check</h2>
<p class="muted" style="margin-top:0">Tests one RC522 reader at a time and says, in plain terms, whether a problem is in the wiring between the ESP32 and the reader, or in the reader itself. Each lane takes about a second and briefly pauses tag reading on it. Hold a tag on the reader to test the radio side too.</p>
<div class="row">
<span id="laneBtns"></span>
<button class="sec" id="allBtn" onclick="runAll()">Check all lanes</button>
<label style="margin:0;min-width:260px"><span>SPI wire drive strength (ESP32 pins, not the antenna)</span>
<select id="drive"><option value="-1">leave as is</option><option value="0">0 (weakest, ~5 mA)</option><option value="1">1 (~10 mA)</option><option value="2">2 (~20 mA)</option><option value="3">3 (strongest, ~40 mA)</option></select></label>
</div>
<div class="hint">How hard the ESP32 drives the SPI wires to the readers (SCK, MOSI and the select lines). It's a wiring experiment: weaker drive gives slower signal edges, which ring less on long cables. A change applies to every lane until the next reboot. The antenna isn't adjustable here: receive gain is always set to the maximum and transmit power is fixed by the chip.</div>
</div>
<div id="results"></div>
</main>
<script>
const $=id=>document.getElementById(id);
const esc=s=>String(s??'').replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const hx=v=>'0x'+(v>>>0).toString(16).toUpperCase().padStart(2,'0');
const VERSIONS={0x91:'MFRC522 v1.0',0x92:'MFRC522 v2.0',0x88:'FM17522 clone',0xB2:'FM17522 clone',0x12:'counterfeit chip (often works)'};
const ERRBITS=[[0x80,'write error'],[0x40,'chip overheating (antenna switched off)'],[0x10,'buffer overflow'],[0x08,'collision (several tags)'],[0x04,'CRC error'],[0x02,'parity error'],[0x01,'protocol error']];
const errText=e=>{const b=ERRBITS.filter(x=>e&x[0]).map(x=>x[1]);return b.length?b.join(', '):'none'};
const isTimeout=q=>q.status.indexOf('Timeout')>=0;
let lanes=[],busy=false;

async function load(){
 const s=await (await fetch('/api/status')).json();
 lanes=s.lanes.filter(l=>l.enabled);
 if(s.spi_pin_drive!=null)showDrive(s.spi_pin_drive);
 $('laneBtns').innerHTML=lanes.map(l=>`<button class="sm" onclick="run(${l.lane})">Lane ${l.lane}</button> `).join('')||'<span class="muted">No lanes enabled (see Settings)</span>';
}
// The "leave as is" option names the current strength
function showDrive(v){$('drive').options[0].textContent=`leave as is (currently ${v})`}
function setBusy(b){busy=b;document.querySelectorAll('button').forEach(x=>x.disabled=b)}
async function check(n){return (await fetch(`/api/lane/diag?lane=${n}&drive=${$('drive').value}`)).json()}
async function runInto(el,n){el.innerHTML=card({lane:n,_pending:1});
 try{const d=await check(n);if(d.pin_drive!=null)showDrive(d.pin_drive);el.innerHTML=card(d)}catch(e){el.innerHTML=card({lane:n,error:'Request failed: '+e})}}
async function run(n){if(busy)return;setBusy(true);$('results').innerHTML='<div></div>';await runInto($('results').firstChild,n);setBusy(false)}
async function runAll(){if(busy)return;setBusy(true);$('results').innerHTML=lanes.map(l=>`<div id="res${l.lane}"></div>`).join('');
 for(const l of lanes)await runInto($('res'+l.lane),l.lane);setBusy(false)}

function analyse(d){
 const g=d.registers,rows=[],v=g.version;
 const answers=v!=0&&v!=0xFF;
 rows.push([answers,'Chip answers',answers?`version ${hx(v)}: ${VERSIONS[v]||'unknown chip type'}`:`nothing answered (${hx(v)}): the reader isn't connected, powered, or selected`]);
 const readsOk=answers&&d.read_ok==d.read_total;
 rows.push([readsOk,'Reads are reliable',`${d.read_ok} of ${d.read_total} version reads matched`]);
 const writesOk=answers&&d.write_ok==d.write_total;
 const bad=d.write_patterns.filter(f=>f.ok<f.of);
 const fixedFail=bad.length>0&&d.write_patterns.every(f=>f.ok==0||f.ok==f.of);
 rows.push([writesOk,'Writes are reliable',`${d.write_ok} of ${d.write_total} test patterns read back correctly`+
  (bad.length?'<br><span class="mono">'+bad.map(f=>`wrote ${hx(f.wrote)}, read ${hx(f.read)} (${f.of-f.ok}/${f.of} failed)`).join('<br>')+'</span>'+
   (fixedFail?'<br><span class="warn">The same patterns fail every time and the others never do.</span>':''):'')]);
 const set=[['receive gain',(g.rf_gain&0x70)==0x70,g.rf_gain,'0x70'],['TX modulation',g.tx_ask==0x40,g.tx_ask,'0x40'],['mode',g.mode==0x3D,g.mode,'0x3D']];
 const setOk=set.every(x=>x[1]);
 rows.push([setOk,'Setup sticks after init',set.map(x=>`${x[0]} ${hx(x[2])}${x[1]?'':` <span class="bad">(should be ${x[3]})</span>`}`).join(' · ')]);
 const antOk=(d.tx_control_antenna_on&3)==3;
 const gain=[18,23,18,23,33,38,43,48][(g.rf_gain>>4)&7];
 rows.push([antOk,'Antenna turns on',(antOk?'Both antenna drivers on (full transmit power, fixed by the chip)':'The antenna drivers did not switch on')+
  ` · receive gain ${gain} dB${gain==48?' (maximum)':''} <span class="muted">· TX control ${hx(d.tx_control_antenna_on)}</span>`]);
 const seen=d.requests.filter(q=>q.ok).reduce((a,q)=>a+q.count,0);
 const hot=d.requests.some(q=>q.error_reg&0x40);
 const radioErr=d.requests.some(q=>!q.ok&&!isTimeout(q));
 rows.push([seen>0?true:radioErr?false:null,'Tag wake-ups (20 tries)',d.requests.map(q=>`${q.count}× `+
  (q.ok?'<span class="ok">tag answered</span>':isTimeout(q)?'no tag answered':esc(q.status.replace(/\.$/,'')))+
  (q.error_reg?` <span class="muted">(chip flags: ${errText(q.error_reg)})</span>`:'')).join('<br>')]);
 rows.push([d.self_test_passed?true:null,'Built-in self-test',d.self_test_passed?'passed':'failed <span class="muted">(many working clone chips fail this; only meaningful compared with a reader that works)</span>']);

 let verdict,cls='bad';
 if(!answers)verdict=['No reply from the reader',`Check its SS wire (GPIO ${d.ss_pin}), MISO, 3.3 V and GND.`];
 else if(readsOk&&fixedFail)verdict=['Consistent write fault',"Reads are perfect, but specific values written to the chip never arrive correctly, every time. A loose wire or electrical noise gives random errors that change with drive strength; a fault this exact usually means the reader chip. To be sure: re-run at another drive strength. If the result doesn't change at all, it's the board. If it changes, it's the wiring."];
 else if(!readsOk||!writesOk)verdict=['Wiring / signal problem',(!readsOk?'Reads come back wrong (MISO, SCK or SS). ':'')+(!writesOk?'Writes arrive wrong (MOSI, SCK or SS). ':'')+'Check those conductors and terminals, try a lower drive strength, or shorten the run. Another reader on the bus answering out of turn causes this too.'];
 else if(hot)verdict=['Reader reports overheating','The wires test clean, but the chip flags overheating and switches its antenna off. Usually a short at the antenna drivers or the parts between chip and antenna: feel the chip, look for solder bridges, or replace the board.'];
 else if(!setOk||!antOk)verdict=["Reader isn't holding its setup","Reads and writes over the wires test clean, but the chip doesn't keep the settings it's given. That points at the reader board, not the wiring."];
 else if(seen>0){verdict=['Working','The reader is healthy and a tag answered.'];cls='ok'}
 else if(radioErr){verdict=['Radio errors','Wiring and setup are fine, but tag replies come back garbled. Lay the tag flat and centred; if a tag that works on another lane fails here, suspect this antenna.'];cls='warn'}
 else{verdict=['Healthy, no tag seen','Wiring and setup check out. Hold a tag flat on the reader and run again to test the radio side.'];cls='ok'}
 return {rows,verdict,cls};
}
function card(d){
 const head=`<h2>Lane ${d.lane}${d.ss_pin!=null?` · SS GPIO ${d.ss_pin}`:''}</h2>`;
 if(d._pending)return `<div class="card">${head}<span class="muted">Testing…</span></div>`;
 if(d.error)return `<div class="card">${head}<span class="bad">${esc(d.error)}</span></div>`;
 const a=analyse(d);
 const icon=x=>x===true?'<span class="ok">✔</span>':x===false?'<span class="bad">✘</span>':'<span class="muted">–</span>';
 return `<div class="card">${head}<div class="verdict"><b class="${a.cls}">${a.verdict[0]}</b>${a.verdict[1]}</div>`+
  `<table class="checks"><tbody>${a.rows.map(r=>`<tr><td>${icon(r[0])}</td><td><b>${r[1]}</b><br>${r[2]}</td></tr>`).join('')}</tbody></table>`+
  `<div class="hint">SPI clock ${(d.spi_clock_hz/1e6).toFixed(1)} MHz · SPI wire drive strength ${d.pin_drive} of 0–3</div>`+
  `<details><summary>Raw result</summary><pre>${esc(JSON.stringify(d,null,1))}</pre></details></div>`;
}
load();
</script></body></html>)HTML";
