#include "web_ui.h"
#include "../core/nvs_manager.h"
#include "../core/device_id.h"
#include "../config/defaults.h"
#include "../config/pins.h"
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <WiFi.h>

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

static bool configSaved = false;
static bool previewRequested = false;
static char pin[5] = "0000";
static int failedAttempts = 0;
static unsigned long lockoutUntil = 0;
static bool authenticated = false;

static const char* getResetReasonString() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON: return "Cold Boot (Power-on)";
        case ESP_RST_EXT: return "External Reset Pin";
        case ESP_RST_SW: return "Software Reset";
        case ESP_RST_PANIC: return "Software Crash (Panic)";
        case ESP_RST_INT_WDT: return "Interrupt Watchdog";
        case ESP_RST_TASK_WDT: return "Task Watchdog";
        case ESP_RST_WDT: return "Other Watchdog";
        case ESP_RST_DEEPSLEEP: return "Deep Sleep Wakeup";
        case ESP_RST_BROWNOUT: return "Brownout Reset (Voltage Drop)";
        case ESP_RST_SDIO: return "SDIO Reset";
        default: return "Unknown";
    }
}

// HTML Templates

static const char HTML_LOGIN[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>IoTDrainage - Login</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Segoe UI',sans-serif;background:#0f172a;color:#e2e8f0;
display:flex;justify-content:center;align-items:center;min-height:100vh}
.card{background:#1e293b;border-radius:16px;padding:32px;max-width:380px;
width:90%;box-shadow:0 8px 32px rgba(0,0,0,0.4)}
h1{font-size:1.4em;text-align:center;margin-bottom:8px;color:#38bdf8}
.sub{text-align:center;color:#94a3b8;margin-bottom:24px;font-size:0.9em}
input[type=password]{width:100%;padding:14px;border-radius:8px;border:1px solid #334155;
background:#0f172a;color:#fff;font-size:1.2em;text-align:center;letter-spacing:8px;
margin-bottom:16px}
button{width:100%;padding:14px;border:none;border-radius:8px;background:#2563eb;
color:#fff;font-size:1em;cursor:pointer;font-weight:600}
button:hover{background:#1d4ed8}
.err{color:#f87171;text-align:center;margin-bottom:12px;font-size:0.9em;display:none}
</style>
</head>
<body>
<div class="card">
<h1>🔐 IoTDrainage Authentication</h1>
<p class="sub">Masukkan PIN 4 digit untuk akses</p>
<div class="err" id="err">PIN salah! Sisa percobaan: <span id="rem">3</span></div>
<form action="/auth" method="POST">
<input type="password" name="pin" maxlength="4" pattern="\d{4}" required
       placeholder="••••" autofocus>
<button type="submit">Masuk</button>
</form>
</div>
</body>
</html>
)rawliteral";

static const char HTML_LOCKOUT[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>IoTDrainage - Lockout</title>
<style>
body{font-family:'Segoe UI',sans-serif;background:#0f172a;color:#e2e8f0;
display:flex;justify-content:center;align-items:center;min-height:100vh}
.card{background:#1e293b;border-radius:16px;padding:32px;max-width:380px;width:90%;
text-align:center;box-shadow:0 8px 32px rgba(0,0,0,0.4)}
h1{color:#f87171;margin-bottom:16px}
p{color:#94a3b8}
</style></head>
<body><div class="card">
<h1>⛔ Terkunci</h1>
<p>Terlalu banyak percobaan salah.<br>Coba lagi dalam 5 menit.</p>
</div></body></html>
)rawliteral";

static const char HTML_CONFIG[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>IoTDrainage - Konfigurasi</title>
<link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;700&display=swap" rel="stylesheet">
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Outfit',sans-serif;background:#090d16;color:#f1f5f9;padding:24px 16px 100px 16px;min-height:100vh}
.container{max-width:480px;margin:0 auto}
h1{text-align:center;font-weight:700;font-size:1.6em;background:linear-gradient(135deg,#38bdf8,#7c3aed);-webkit-background-clip:text;-webkit-text-fill-color:transparent;margin-bottom:4px}
.sub{text-align:center;color:#64748b;margin-bottom:24px;font-size:0.9em;font-weight:400}

/* Navigation Bar */
.nav-bar {
  display: flex;
  justify-content: space-around;
  background: rgba(15, 23, 42, 0.6);
  backdrop-filter: blur(12px);
  -webkit-backdrop-filter: blur(12px);
  border: 1px solid rgba(255, 255, 255, 0.05);
  border-radius: 14px;
  padding: 6px;
  position: sticky;
  top: 12px;
  z-index: 100;
  margin-bottom: 24px;
  box-shadow: 0 8px 32px rgba(0,0,0,0.5);
}
.nav-btn {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 4px;
  background: transparent;
  border: none;
  color: #64748b;
  font-family: inherit;
  font-size: 0.72em;
  font-weight: 600;
  cursor: pointer;
  padding: 8px 6px;
  border-radius: 10px;
  transition: all 0.2s ease;
  flex: 1;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}
.nav-btn svg {
  width: 20px;
  height: 20px;
  fill: currentColor;
  transition: transform 0.2s ease;
}
.nav-btn:hover {
  color: #38bdf8;
}
.nav-btn:hover svg {
  transform: translateY(-2px);
}
.nav-btn.active {
  color: #38bdf8;
  background: rgba(56, 189, 248, 0.08);
  box-shadow: inset 0 0 0 1px rgba(56, 189, 248, 0.15);
}

/* Tabs Content */
.tab-content {
  display: none;
  animation: fadeIn 0.25s ease-out;
}
.tab-content.active {
  display: block;
}
@keyframes fadeIn {
  from { opacity: 0; transform: translateY(6px); }
  to { opacity: 1; transform: translateY(0); }
}

/* Floating Save Bar */
.save-bar {
  position: fixed;
  bottom: 0;
  left: 0;
  width: 100%;
  background: rgba(9, 13, 22, 0.9);
  backdrop-filter: blur(16px);
  -webkit-backdrop-filter: blur(16px);
  border-top: 1px solid rgba(255, 255, 255, 0.05);
  padding: 16px;
  box-shadow: 0 -8px 32px rgba(0, 0, 0, 0.6);
  z-index: 90;
  transform: translateY(100%);
  transition: transform 0.3s cubic-bezier(0.16, 1, 0.3, 1);
}
.save-bar.visible {
  transform: translateY(0);
}
.save-bar .container {
  max-width: 480px;
  margin: 0 auto;
}
.save-bar .submit {
  margin-top: 0;
}

.card{background:rgba(30,41,59,0.3);backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px);border:1px solid rgba(255,255,255,0.05);border-radius:16px;padding:24px;margin-bottom:20px;box-shadow:0 8px 32px rgba(0,0,0,0.3)}
.card h2{font-size:1.1em;font-weight:600;color:#38bdf8;margin-bottom:16px;display:flex;align-items:center;gap:10px}
label{display:block;color:#94a3b8;margin-bottom:6px;font-size:0.85em;font-weight:600;text-transform:uppercase;letter-spacing:0.5px}
input[type=text],input[type=password],input[type=number],select{width:100%;padding:12px 16px;border-radius:10px;border:1px solid #1e293b;background:#0f172a;color:#fff;margin-bottom:16px;font-size:0.95em;transition:border-color 0.2s,box-shadow 0.2s}
input[type=text]:focus,input[type=password]:focus,input[type=number]:focus,select:focus{outline:none;border-color:#38bdf8;box-shadow:0 0 0 3px rgba(56,189,248,0.15)}
.diag-grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:16px}
.diag-item{background:#060a13;border-radius:10px;padding:12px;border:1px solid rgba(255,255,255,0.02)}
.diag-item .lbl{color:#64748b;font-size:0.75em;text-transform:uppercase;margin-bottom:4px}
.diag-item .val{font-size:0.95em;font-weight:600;color:#f1f5f9}
.live{background:#060a13;border-radius:12px;padding:16px;margin-bottom:16px;border:1px solid rgba(56,189,248,0.1);position:relative;overflow:hidden}
.live::before{content:'';position:absolute;top:0;left:0;width:4px;height:100%;background:#38bdf8}
.live.wl::before{background:#7c3aed}
.live .val{font-size:2.2em;font-weight:700;color:#38bdf8;line-height:1.2}
.live .lbl{color:#94a3b8;font-size:0.85em;margin-bottom:4px}
.bar{height:10px;background:#1e293b;border-radius:5px;overflow:hidden;margin-top:12px}
.bar-fill{height:100%;background:linear-gradient(90deg,#38bdf8,#7c3aed);border-radius:5px;transition:width 0.4s ease}
.btn-cal{width:100%;padding:12px;border:2px dashed #1e293b;border-radius:10px;background:transparent;color:#38bdf8;cursor:pointer;margin-bottom:16px;font-size:0.95em;font-weight:600;transition:all 0.2s}
.btn-cal:hover{border-color:#38bdf8;background:rgba(56,189,248,0.05);color:#fff}
.submit{width:100%;padding:16px;border:none;border-radius:12px;background:linear-gradient(135deg,#38bdf8,#7c3aed);color:#fff;font-size:1.05em;cursor:pointer;font-weight:600;transition:transform 0.1s,opacity 0.2s;box-shadow:0 4px 20px rgba(124,58,237,0.3)}
.submit:hover{opacity:0.95;transform:translateY(-1px)}
.submit:active{transform:translateY(1px)}
.rain{display:inline-block;padding:6px 16px;border-radius:20px;font-size:0.85em;font-weight:600}
.rain.yes{background:rgba(124,58,237,0.15);color:#a78bfa;border:1px solid rgba(124,58,237,0.3)}
.rain.no{background:rgba(56,189,248,0.15);color:#38bdf8;border:1px solid rgba(56,189,248,0.3)}
.status{text-align:center;padding:12px;border-radius:8px;margin-top:16px;font-weight:600;font-size:0.9em}
#countdown{text-align:center;color:#ef4444;font-weight:600;margin-bottom:16px;font-size:1.1em;background:rgba(239,68,68,0.1);padding:8px;border-radius:8px;border:1px solid rgba(239,68,68,0.2)}
.segment-control {
  display: flex;
  background: #0f172a;
  border: 1px solid #1e293b;
  border-radius: 10px;
  padding: 4px;
  margin-bottom: 20px;
}
.segment-btn {
  flex: 1;
  padding: 10px;
  border: none;
  background: transparent;
  color: #64748b;
  font-family: inherit;
  font-size: 0.85em;
  font-weight: 600;
  cursor: pointer;
  border-radius: 8px;
  transition: all 0.2s;
  text-align: center;
}
.segment-btn:hover {
  color: #38bdf8;
}
.segment-btn.active {
  background: rgba(56, 189, 248, 0.1);
  color: #38bdf8;
  box-shadow: inset 0 0 0 1px rgba(56, 189, 248, 0.2);
}
.sub-tab-content {
  display: none;
  animation: fadeIn 0.2s ease-out;
}
.sub-tab-content.active {
  display: block;
}
</style>
</head>
<body>
<div class="container">
<h1>🔧 IoTDrainage Konfigurasi</h1>
<p class="sub">Silakan atur parameter operasional alat</p>

<!-- Navigation Bar -->
<div class="nav-bar">
  <button id="btn-tab-dashboard" class="nav-btn active" onclick="switchTab('dashboard')">
    <svg viewBox="0 0 24 24"><path d="M5 9.2h3V19H5zM10.5 5h3v14h-3zM16 12.2h3V19h-3z"/></svg>
    Monitor
  </button>
  <button id="btn-tab-koneksi" class="nav-btn" onclick="switchTab('koneksi')">
    <svg viewBox="0 0 24 24"><path d="M12 21a2 2 0 110-4 2 2 0 010 4zm4.25-6.15a6 6 0 00-8.5 0l1.42 1.42a4 4 0 015.66 0l1.42-1.42zm3.53-3.53a11 11 0 00-15.56 0l1.42 1.42a9 9 0 0112.72 0l1.42-1.42z"/></svg>
    Koneksi
  </button>
  <button id="btn-tab-sensor" class="nav-btn" onclick="switchTab('sensor')">
    <svg viewBox="0 0 24 24"><path d="M17 2H7c-1.1 0-2 .9-2 2v16c0 1.1.9 2 2 2h10c1.1 0 2-.9 2-2V4c0-1.1-.9-2-2-2zm0 16h-4v-2h4v-2h-2v-2h2V9h-4V7h4V4H7v16h10v-2z"/></svg>
    Sensor
  </button>
  <button id="btn-tab-kamera" class="nav-btn" onclick="switchTab('kamera')">
    <svg viewBox="0 0 24 24"><path d="M9 2L7.17 4H4c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V6c0-1.1-.9-2-2-2h-3.17L15 2H9zm3 15c-2.76 0-5-2.24-5-5s2.24-5 5-5 5 2.24 5 5-2.24 5-5 5z"/></svg>
    Kamera
  </button>
</div>

<!-- ==================== TAB: MONITOR ==================== -->
<div id="tab-dashboard" class="tab-content active">
  <div class="card">
    <h2>📊 Status & Diagnostik</h2>
    <div class="diag-grid">
      <div class="diag-item"><div class="lbl">ID Perangkat</div><div class="val" id="devId">Loading...</div></div>
      <div class="diag-item"><div class="lbl">IP Address</div><div class="val" id="ipAddress">--.---.---.---</div></div>
      <div class="diag-item"><div class="lbl">WiFi SSID</div><div class="val" id="wifiSsid">Terputus</div></div>
      <div class="diag-item"><div class="lbl">Sinyal (RSSI)</div><div class="val" id="wifiRssi">-- dBm</div></div>
      <div class="diag-item"><div class="lbl">Boot Terakhir</div><div class="val" id="bootReason">Loading...</div></div>
      <div class="diag-item"><div class="lbl">Uptime</div><div class="val" id="uptime">--:--:--</div></div>
      <div class="diag-item" style="grid-column:span 2"><div class="lbl">RAM Bebas</div><div class="val" id="freeHeap">-- KB</div></div>
    </div>
  </div>

  <div id="countdown">⏳ Sisa Waktu Setup: 10:00</div>

  <div class="card">
    <h2>📡 Live Sensor</h2>
    <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:16px">
      <div class="live"><div class="lbl">Jarak Terukur</div><div class="val" id="dist">--.- cm</div></div>
      <div class="live wl"><div class="lbl">Water Level</div><div class="val" id="wl">--.- cm</div><div class="bar"><div class="bar-fill" id="bar" style="width:0%"></div></div></div>
    </div>
    <div style="text-align:center">
      <span class="rain no" id="rain">☀️ Tidak Hujan</span>
    </div>
  </div>
</div>

<!-- ==================== TAB: KONEKSI ==================== -->
<div id="tab-koneksi" class="tab-content">
  <div class="card">
    <h2>📍 Lokasi & Jaringan</h2>
    <label>Lokasi Pemasangan</label>
    <input type="text" id="location" placeholder="Contoh: Jl. Drainase No.1" maxlength="63">
    <label>WiFi SSID</label>
    <input type="text" id="ssid" placeholder="Nama jaringan WiFi">
    <label>WiFi Password</label>
    <input type="password" id="wifiPass" placeholder="Kata sandi WiFi (Masked)">
    <label>Backend URL / Host</label>
    <input type="text" id="backendUrl" placeholder="Contoh: 192.168.1.100:3000 atau api.com">
  </div>
</div>

<!-- ==================== TAB: SENSOR ==================== -->
<div id="tab-sensor" class="tab-content">
  <!-- Live telemetry helper for calibration -->
  <div class="card" style="border-color: rgba(56,189,248,0.2)">
    <h2>📡 Pantauan Sensor Real-time</h2>
    <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px">
      <div class="diag-item" style="background:#060a13"><div class="lbl">Jarak Terukur</div><div class="val" id="distSensor">--.- cm</div></div>
      <div class="diag-item" style="background:#060a13"><div class="lbl">Water Level</div><div class="val" id="wlSensor" style="color:#7c3aed">--.- cm</div></div>
    </div>
  </div>

  <!-- Segmented Control for Sub-menus -->
  <div class="segment-control">
    <button id="btn-sub-cal" class="segment-btn" onclick="switchSubTab('cal')">📏 Kalibrasi Tinggi</button>
    <button id="btn-sub-thresh" class="segment-btn active" onclick="switchSubTab('thresh')">⚠️ Ambang Batas</button>
  </div>

  <!-- Sub-tab: Kalibrasi -->
  <div id="sub-cal" class="sub-tab-content">
    <div class="card">
      <h2>📏 Kalibrasi Sensor</h2>
      <button class="btn-cal" id="btnCal" onclick="setHeight()">✅ Pakai Jarak Saat Ini sebagai Tinggi Sensor</button>
      <label>Tinggi Sensor (cm)</label>
      <input type="number" id="heightSensor" step="0.1" value="150">
      <label>Offset Kalibrasi (cm)</label>
      <input type="number" id="offset" step="0.1" value="0">
    </div>
  </div>

  <!-- Sub-tab: Ambang Batas -->
  <div id="sub-thresh" class="sub-tab-content active">
    <div class="card">
      <h2>⚠️ Ambang Batas (Threshold)</h2>
      <label>Threshold Normal (cm)</label>
      <input type="number" id="thNormal" min="10" max="200" value="40">
      <label>Threshold Bahaya (cm)</label>
      <input type="number" id="thBahaya" min="10" max="200" value="80">
      <div style="background:rgba(56,189,248,0.05);padding:12px;border-radius:10px;border:1px dashed rgba(56,189,248,0.2);margin-top:12px">
        <p style="color:#38bdf8;font-size:0.8em;font-weight:600;margin-bottom:6px;text-transform:uppercase;letter-spacing:0.5px">💡 Cara Kerja Status</p>
        <p style="color:#94a3b8;font-size:0.8em;line-height:1.4">
          Sistem menggunakan 2 nilai batas (input di atas) untuk membagi tinggi air menjadi 3 status:<br>
          • Level &lt; <strong>Normal</strong> &rarr; <strong style="color:#22c55e">NORMAL</strong><br>
          • <strong>Normal</strong> &le; Level &lt; <strong>Bahaya</strong> &rarr; <strong style="color:#eab308">WASPADA</strong><br>
          • Level &ge; <strong>Bahaya</strong> &rarr; <strong style="color:#ef4444">BAHAYA</strong>
        </p>
      </div>
    </div>
  </div>
</div>

<!-- ==================== TAB: KAMERA ==================== -->
<div id="tab-kamera" class="tab-content">
  <div class="card">
    <h2>📷 Preview Camera & Flash</h2>
    <label>Mode Flash Kamera</label>
    <select id="flashMode">
      <option value="2">📸 AUTO (Flash hanya saat gelap)</option>
      <option value="1">⚡ Always ON (Selalu flash)</option>
      <option value="0">🚫 Always OFF (Matikan flash)</option>
    </select>
    <img id="camStream" src="" alt="Preview" style="width:100%;border-radius:12px;background:#060a12;min-height:150px;display:none;object-fit:cover;margin-bottom:12px;border:1px solid rgba(255,255,255,0.05)">
    <div id="camPlaceholder" style="width:100%;border-radius:12px;background:#060a12;min-height:150px;color:#64748b;display:flex;align-items:center;justify-content:center;border:1px dashed #1e293b;margin-bottom:12px;font-size:0.9em">Klik tombol untuk melihat gambar</div>
    <button id="btnPreview" class="btn-cal" type="button" onclick="requestPreview()">📸 Ambil Foto Preview</button>
  </div>
</div>

<!-- Persistent Floating Save Bar -->
<div id="floating-save" class="save-bar">
  <div class="container">
    <form id="configForm">
      <button type="submit" class="submit">💾 Simpan & Mulai Monitor</button>
      <div class="status" id="status" style="display:none"></div>
    </form>
  </div>
</div>

</div>

<script>
var timeLeft = 600;
setInterval(function(){
  if(timeLeft > 0) timeLeft--;
  var m = Math.floor(timeLeft/60);
  var s = timeLeft%60;
  var cd = document.getElementById('countdown');
  if(cd) cd.textContent = '⏳ Sisa Waktu Setup: ' + m + ':' + (s<10?'0':'') + s;
}, 1000);

// Tab switching logic
function switchTab(tabId) {
  document.querySelectorAll('.tab-content').forEach(function(el) {
    el.classList.remove('active');
  });
  document.querySelectorAll('.nav-btn').forEach(function(el) {
    el.classList.remove('active');
  });
  
  document.getElementById('tab-' + tabId).classList.add('active');
  document.getElementById('btn-tab-' + tabId).classList.add('active');
  
  var saveBar = document.getElementById('floating-save');
  if (tabId === 'dashboard') {
    saveBar.classList.remove('visible');
  } else {
    saveBar.classList.add('visible');
  }
}

// Sub-tab switching logic
function switchSubTab(subId) {
  document.querySelectorAll('.sub-tab-content').forEach(function(el) {
    el.classList.remove('active');
  });
  document.querySelectorAll('.segment-btn').forEach(function(el) {
    el.classList.remove('active');
  });
  
  document.getElementById('sub-' + subId).classList.add('active');
  document.getElementById('btn-sub-' + subId).classList.add('active');
}

var ws;
function requestPreview(){
  document.getElementById('btnPreview').textContent = '⏳ Mengambil gambar...';
  document.getElementById('btnPreview').disabled = true;
  ws.send(JSON.stringify({cmd:'request_preview'}));
}

function handleMessage(msgData) {
  try {
    var d = JSON.parse(msgData);
    if(d.type==='telemetry'){
      document.getElementById('dist').textContent=d.dist_cm.toFixed(1)+' cm';
      document.getElementById('distSensor').textContent=d.dist_cm.toFixed(1)+' cm';
      document.getElementById('wl').textContent=d.water_level_cm.toFixed(1)+' cm';
      document.getElementById('wlSensor').textContent=d.water_level_cm.toFixed(1)+' cm';
      var hs=parseFloat(document.getElementById('heightSensor').value)||150;
      var pct=Math.min(100,Math.max(0,(d.water_level_cm/hs)*100));
      document.getElementById('bar').style.width=pct+'%';
      var r=document.getElementById('rain');
      if(d.rain){r.className='rain yes';r.textContent='🌧️ Hujan Terdeteksi';}
      else{r.className='rain no';r.textContent='☀️ Tidak Hujan';}
      if (d.rssi_dbm) {
        document.getElementById('wifiRssi').textContent = d.rssi_dbm + ' dBm';
      }
    }else if(d.type==='device_info'){
      document.getElementById('devId').textContent = d.device_id || 'Unknown';
      document.getElementById('ipAddress').textContent = d.ip_address || '--.---.---.---';
      document.getElementById('wifiSsid').textContent = d.ssid || 'Terputus';
      document.getElementById('wifiRssi').textContent = (d.rssi_dbm || 0) + ' dBm';
      document.getElementById('bootReason').textContent = d.reset_reason || 'Unknown';
      
      let hrs = Math.floor(d.uptime_sec / 3600);
      let mins = Math.floor((d.uptime_sec % 3600) / 60);
      let secs = d.uptime_sec % 60;
      document.getElementById('uptime').textContent = 
        (hrs<10?'0':'') + hrs + ':' + (mins<10?'0':'') + mins + ':' + (secs<10?'0':'') + secs;
        
      document.getElementById('freeHeap').textContent = (d.free_heap / 1024).toFixed(1) + ' KB';

      if (d.location) document.getElementById('location').value = d.location;
      if (d.ssid) document.getElementById('ssid').value = d.ssid;
      if (d.has_password) {
        document.getElementById('wifiPass').value = '••••••••';
      }
      
      let host = d.backend_host || '';
      let port = d.backend_port || 3000;
      if (host) {
        document.getElementById('backendUrl').value = host + ':' + port;
      }
      
      if (d.height_sensor !== undefined) {
        document.getElementById('heightSensor').value = d.height_sensor.toFixed(1);
      }
      if (d.offset !== undefined) {
        document.getElementById('offset').value = d.offset.toFixed(1);
      }
      if (d.threshold_normal !== undefined) {
        document.getElementById('thNormal').value = d.threshold_normal;
      }
      if (d.threshold_bahaya !== undefined) {
        document.getElementById('thBahaya').value = d.threshold_bahaya;
      }
      if (d.flash_mode !== undefined) {
        document.getElementById('flashMode').value = d.flash_mode.toString();
      }
    }
  } catch(ex) {}
}

function initWS(){
  ws=new WebSocket('ws://'+location.hostname+'/ws');
  ws.onmessage=function(e){
    if(e.data instanceof Blob){
      var url = URL.createObjectURL(e.data);
      var img = document.getElementById('camStream');
      if(img.src.startsWith('blob:')) URL.revokeObjectURL(img.src);
      img.src = url;
      img.style.display = 'block';
      document.getElementById('camPlaceholder').style.display = 'none';
      document.getElementById('btnPreview').textContent = '📸 Ambil Ulang Foto';
      document.getElementById('btnPreview').disabled = false;
      return;
    }
    handleMessage(e.data);
  };
  ws.onclose=function(){setTimeout(initWS,2000)};
}
initWS();

function setHeight(){
  var v=document.getElementById('distSensor');
  var t=v.textContent.replace(' cm','');
  if(t!=='--.-'){document.getElementById('heightSensor').value=parseFloat(t).toFixed(1);}
}

document.getElementById('configForm').onsubmit=function(e){
  e.preventDefault();
  
  let rawBackend = document.getElementById('backendUrl').value.trim();
  let host = rawBackend.replace(/^(http|https):\/\//, '');
  host = host.split('/')[0];
  let port = 3000;
  if (host.includes(':')) {
    let parts = host.split(':');
    host = parts[0];
    port = parseInt(parts[1]) || 3000;
  }

  var cfg={
    cmd:'save_config',
    ssid:document.getElementById('ssid').value,
    password:document.getElementById('wifiPass').value,
    backend_host:host,
    backend_port:port,
    height_sensor:parseFloat(document.getElementById('heightSensor').value),
    offset:parseFloat(document.getElementById('offset').value),
    threshold_normal:parseInt(document.getElementById('thNormal').value),
    threshold_bahaya:parseInt(document.getElementById('thBahaya').value),
    location:document.getElementById('location').value,
    flash_mode:parseInt(document.getElementById('flashMode').value)
  };
  if(!cfg.ssid){alert('SSID wajib diisi!');return;}
  ws.send(JSON.stringify(cfg));
  
  var st=document.getElementById('status');
  st.style.display='block';
  st.style.background='rgba(56,189,248,0.1)';
  st.style.color='#38bdf8';
  st.style.border='1px solid rgba(56,189,248,0.2)';
  st.textContent='💾 Menyimpan konfigurasi... Perangkat akan reboot.';
};
</script>
</body>
</html>
)rawliteral";

// PIN Management

static void generatePIN() {
    AuthConfig auth;
    if (NVSManager::loadAuthConfig(auth) && auth.valid && strlen(auth.pin) == PIN_LENGTH) {
        strncpy(pin, auth.pin, sizeof(pin));
    } else {
        // Generate random 4-digit PIN
        randomSeed(esp_random());
        snprintf(pin, sizeof(pin), "%04d", (int)random(0, 10000));

        // Save to NVS
        AuthConfig newAuth;
        memset(&newAuth, 0, sizeof(newAuth));
        strncpy(newAuth.pin, pin, sizeof(newAuth.pin));
        newAuth.fail_count = 0;
        newAuth.lockout_until = 0;
        newAuth.valid = true;
        NVSManager::saveAuthConfig(newAuth);
    }
    Serial.printf("\n========================================\n");
    Serial.printf("  🔐 Web UI PIN: %s\n", pin);
    Serial.printf("========================================\n\n");
}

// WebSocket Event Handler

static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                       AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("[WS] Client #%u connected\n", client->id());
        
        LocationConfig lCfg;
        if (!NVSManager::loadLocationConfig(lCfg) || !lCfg.valid) {
            lCfg.location[0] = '\0';
        }

        WiFiConfig wifi;
        if (!NVSManager::loadWiFiConfig(wifi) || !wifi.valid) {
            wifi.ssid[0] = '\0';
            wifi.password[0] = '\0';
        }

        BackendConfig backend;
        if (!NVSManager::loadBackendConfig(backend) || !backend.valid) {
            backend.host[0] = '\0';
            backend.port = DEFAULT_BACKEND_PORT;
        }

        SensorConfig sensor;
        if (!NVSManager::loadSensorConfig(sensor) || !sensor.valid) {
            sensor.height_sensor_cm = DEFAULT_HEIGHT_SENSOR;
            sensor.offset_cm = DEFAULT_OFFSET;
        }

        ThresholdConfig thresh;
        if (!NVSManager::loadThresholdConfig(thresh) || !thresh.valid) {
            thresh.threshold_normal_cm = DEFAULT_THRESHOLD_NORMAL;
            thresh.threshold_bahaya_cm = DEFAULT_THRESHOLD_BAHAYA;
        }

        CameraConfig camera;
        if (!NVSManager::loadCameraConfig(camera) || !camera.valid) {
            camera.flash_mode = 2; // Default AUTO
        }

        JsonDocument doc;
        doc["type"] = "device_info";
        doc["device_id"] = DeviceID::get();
        doc["location"] = lCfg.location;
        doc["ssid"] = wifi.ssid;
        doc["has_password"] = (strlen(wifi.password) > 0);
        doc["backend_host"] = backend.host;
        doc["backend_port"] = backend.port;
        doc["height_sensor"] = sensor.height_sensor_cm;
        doc["offset"] = sensor.offset_cm;
        doc["threshold_normal"] = thresh.threshold_normal_cm;
        doc["threshold_bahaya"] = thresh.threshold_bahaya_cm;
        doc["flash_mode"] = camera.flash_mode;

        // Diagnostics
        doc["ip_address"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
        doc["rssi_dbm"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
        doc["reset_reason"] = getResetReasonString();
        doc["uptime_sec"] = millis() / 1000;
        doc["free_heap"] = ESP.getFreeHeap();

        char buf[768];
        serializeJson(doc, buf);
        client->text(buf);
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("[WS] Client #%u disconnected\n", client->id());
    } else if (type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo *)arg;
        if (info->opcode == WS_TEXT) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len)) return;

            const char *cmd = doc["cmd"] | "";
            if (strcmp(cmd, "request_preview") == 0) {
                previewRequested = true;
                Serial.println("[WebUI] Camera preview requested");
            } else if (strcmp(cmd, "save_config") == 0) {
                Serial.println("[WebUI] Saving configuration...");

                // Save location config
                LocationConfig lCfg;
                memset(&lCfg, 0, sizeof(lCfg));
                strncpy(lCfg.location, doc["location"] | "Unknown", sizeof(lCfg.location) - 1);
                lCfg.location[sizeof(lCfg.location) - 1] = '\0';
                lCfg.valid = true;
                NVSManager::saveLocationConfig(lCfg);

                // Save WiFi config
                WiFiConfig wifi;
                memset(&wifi, 0, sizeof(wifi));
                strncpy(wifi.ssid, doc["ssid"] | "", sizeof(wifi.ssid) - 1);
                
                // If password is dummy "••••••••" or empty, load old password from NVS
                const char* newPass = doc["password"] | "";
                if (strcmp(newPass, "••••••••") == 0 || strlen(newPass) == 0) {
                    WiFiConfig oldWifi;
                    if (NVSManager::loadWiFiConfig(oldWifi) && oldWifi.valid) {
                        strncpy(wifi.password, oldWifi.password, sizeof(wifi.password) - 1);
                    } else {
                        wifi.password[0] = '\0';
                    }
                } else {
                    strncpy(wifi.password, newPass, sizeof(wifi.password) - 1);
                }
                wifi.valid = true;
                NVSManager::saveWiFiConfig(wifi);

                // Save backend config
                BackendConfig backend;
                memset(&backend, 0, sizeof(backend));
                strncpy(backend.host, doc["backend_host"] | "", sizeof(backend.host) - 1);
                backend.port = doc["backend_port"] | DEFAULT_BACKEND_PORT;
                backend.valid = true;
                NVSManager::saveBackendConfig(backend);

                // Save sensor config
                SensorConfig sensor;
                sensor.height_sensor_cm = doc["height_sensor"] | DEFAULT_HEIGHT_SENSOR;
                sensor.offset_cm = doc["offset"] | DEFAULT_OFFSET;
                sensor.valid = true;
                NVSManager::saveSensorConfig(sensor);

                // Save threshold config
                ThresholdConfig thresh;
                thresh.threshold_normal_cm = doc["threshold_normal"] | DEFAULT_THRESHOLD_NORMAL;
                thresh.threshold_bahaya_cm = doc["threshold_bahaya"] | DEFAULT_THRESHOLD_BAHAYA;
                thresh.valid = true;
                NVSManager::saveThresholdConfig(thresh);

                // Save camera configuration (flash mode)
                CameraConfig camera;
                camera.flash_mode = doc["flash_mode"] | 2; // Default AUTO
                camera.valid = true;
                NVSManager::saveCameraConfig(camera);

                configSaved = true;
                Serial.println("[WebUI] Configuration saved! Rebooting...");
            }
        }
    }
}

// Public API

void WebUI::start() {
    generatePIN();
    authenticated = false;
    configSaved = false;
    failedAttempts = 0;
    lockoutUntil = 0;

    // WebSocket setup
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    // Login page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (authenticated) {
            req->send(200, "text/html", HTML_CONFIG);
        } else {
            if (millis() < lockoutUntil) {
                req->send(200, "text/html", HTML_LOCKOUT);
            } else {
                req->send(200, "text/html", HTML_LOGIN);
            }
        }
    });

    // Auth handler
    server.on("/auth", HTTP_POST, [](AsyncWebServerRequest *req) {
        if (millis() < lockoutUntil) {
            req->send(200, "text/html", HTML_LOCKOUT);
            return;
        }

        if (!req->hasParam("pin", true)) {
            req->send(400, "text/plain", "Bad Request: Missing pin parameter");
            return;
        }

        String inputPin = req->getParam("pin", true)->value();
        if (inputPin.equals(pin)) {
            authenticated = true;
            failedAttempts = 0;
            Serial.println("[WebUI] PIN correct — authenticated");
            req->send(200, "text/html", HTML_CONFIG);
        } else {
            failedAttempts++;
            Serial.printf("[WebUI] PIN wrong! Attempt %d/%d\n",
                          failedAttempts, PIN_MAX_ATTEMPTS);

            if (failedAttempts >= PIN_MAX_ATTEMPTS) {
                lockoutUntil = millis() + PIN_LOCKOUT_MS;
                Serial.println("[WebUI] LOCKOUT — 5 minutes");
                req->send(200, "text/html", HTML_LOCKOUT);
            } else {
                req->send(200, "text/html", HTML_LOGIN);
            }
        }
    });

    // Captive portal redirects
    server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->redirect("/");
    });
    server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->redirect("/");
    });
    server.onNotFound([](AsyncWebServerRequest *req) {
        req->redirect("/");
    });

    server.begin();
    Serial.println("[WebUI] Server started at http://192.168.4.1");
}

void WebUI::stop() {
    ws.closeAll();
    server.end();
    Serial.println("[WebUI] Server stopped");
}

void WebUI::loop() {
    ws.cleanupClients();
}

void WebUI::sendTelemetry(float distCm, float waterLevelCm, int rssi, bool rain) {
    if (ws.count() == 0) return;

    JsonDocument doc;
    doc["type"] = "telemetry";
    doc["dist_cm"] = distCm;
    doc["water_level_cm"] = waterLevelCm;
    doc["rssi_dbm"] = rssi;
    doc["rain"] = rain;

    char buf[192];
    serializeJson(doc, buf);
    ws.textAll(buf);
}

void WebUI::sendCameraFrame(const uint8_t *data, size_t len) {
    if (ws.count() == 0) return;
    ws.binaryAll((uint8_t *)data, len);
}

bool WebUI::hasClients() {
    return ws.count() > 0;
}

bool WebUI::isConfigSaved() {
    return configSaved;
}

const char* WebUI::getPIN() {
    return pin;
}

bool WebUI::isPreviewRequested() {
    return previewRequested;
}

void WebUI::clearPreviewRequest() {
    previewRequested = false;
}
