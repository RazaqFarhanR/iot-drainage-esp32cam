#include "web_ui.h"
#include "../core/nvs_manager.h"
#include "../core/device_id.h"
#include "../config/defaults.h"
#include "../config/pins.h"
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include <Arduino.h>

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

static bool configSaved = false;
static bool previewRequested = false;
static char pin[5] = "0000";
static int failedAttempts = 0;
static unsigned long lockoutUntil = 0;
static bool authenticated = false;

// HTML Templates

static const char HTML_LOGIN[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>IFMS - Login</title>
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
<h1>🔐 IFMS Authentication</h1>
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
<title>IFMS - Lockout</title>
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
<title>IFMS - Konfigurasi</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Segoe UI',sans-serif;background:#0f172a;color:#e2e8f0;padding:16px}
.container{max-width:480px;margin:0 auto}
h1{text-align:center;color:#38bdf8;margin-bottom:4px;font-size:1.3em}
.sub{text-align:center;color:#94a3b8;margin-bottom:20px;font-size:0.85em}
.card{background:#1e293b;border-radius:12px;padding:20px;margin-bottom:16px;
box-shadow:0 4px 16px rgba(0,0,0,0.3)}
.card h2{font-size:1em;color:#38bdf8;margin-bottom:12px;display:flex;align-items:center;gap:8px}
label{display:block;color:#94a3b8;margin-bottom:4px;font-size:0.85em}
input[type=text],input[type=password],input[type=number]{width:100%;padding:10px;
border-radius:6px;border:1px solid #334155;background:#0f172a;color:#fff;
margin-bottom:12px;font-size:0.95em}
.live{background:#0f172a;border-radius:8px;padding:16px;margin-bottom:12px;text-align:center}
.live .val{font-size:2em;font-weight:700;color:#22c55e}
.live .lbl{color:#94a3b8;font-size:0.85em}
.bar{height:20px;background:#334155;border-radius:10px;overflow:hidden;margin:8px 0}
.bar-fill{height:100%;background:linear-gradient(90deg,#22c55e,#eab308,#ef4444);
border-radius:10px;transition:width 0.5s}
.btn-cal{width:100%;padding:10px;border:2px dashed #334155;border-radius:8px;
background:transparent;color:#38bdf8;cursor:pointer;margin-bottom:12px;font-size:0.9em}
.btn-cal:hover{border-color:#38bdf8;background:#1e3a5f}
.slider-group{margin-bottom:12px}
.slider-group input[type=range]{width:100%;accent-color:#38bdf8}
.slider-val{display:flex;justify-content:space-between;font-size:0.85em;color:#94a3b8}
.submit{width:100%;padding:14px;border:none;border-radius:8px;
background:linear-gradient(135deg,#2563eb,#7c3aed);color:#fff;font-size:1em;
cursor:pointer;font-weight:600;margin-top:8px}
.submit:hover{opacity:0.9}
.rain{display:inline-block;padding:4px 12px;border-radius:20px;font-size:0.85em;
font-weight:600}
.rain.yes{background:#1e3a5f;color:#38bdf8}
.rain.no{background:#1a2332;color:#64748b}
.status{text-align:center;padding:8px;border-radius:6px;margin-top:8px;font-weight:600}
</style>
</head>
<body>
<div class="container">
<h1>🔧 IFMS Konfigurasi</h1>
<p class="sub" id="devId">Device: Loading...</p>

<!-- Live Telemetry -->
<div class="card">
<h2>📡 Live Sensor</h2>
<div class="live">
<div class="lbl">Jarak Terukur</div>
<div class="val" id="dist">--.- cm</div>
</div>
<div class="live">
<div class="lbl">Water Level</div>
<div class="val" id="wl" style="color:#38bdf8">--.- cm</div>
<div class="bar"><div class="bar-fill" id="bar" style="width:0%"></div></div>
</div>
<div style="text-align:center">
<span class="rain no" id="rain">☀️ Tidak Hujan</span>
</div>
</div>

<div id="countdown" style="text-align:center;color:#f87171;font-weight:bold;margin-bottom:16px;font-size:1.1em;">⏳ Sisa Waktu Setup: 10:00</div>

<!-- Camera Stream -->
<div class="card">
<h2>📷 Preview Camera</h2>
<img id="camStream" src="" alt="Preview" style="width:100%;border-radius:8px;background:#0f172a;min-height:150px;display:none;object-fit:cover;margin-bottom:12px;">
<div id="camPlaceholder" style="width:100%;border-radius:8px;background:#0f172a;min-height:150px;color:#94a3b8;display:flex;align-items:center;justify-content:center;border:1px dashed #334155;margin-bottom:12px;">Klik tombol untuk melihat gambar</div>
<button id="btnPreview" class="btn-cal" type="button" onclick="requestPreview()">📸 Ambil Foto Preview</button>
</div>

<!-- Calibration Wizard -->
<div class="card">
<h2>📏 Kalibrasi Sensor</h2>
<p style="color:#94a3b8;font-size:0.85em;margin-bottom:12px">
Letakkan benda padat di dasar saluran tepat di bawah sensor,
lalu tekan tombol di bawah untuk set tinggi sensor.</p>
<button class="btn-cal" id="btnCal" onclick="setHeight()">
✅ Pakai Jarak Saat Ini sebagai Height_sensor
</button>
<label>Tinggi Sensor (cm)</label>
<input type="number" id="heightSensor" step="0.1" value="150">
<label>Offset Kalibrasi (cm)</label>
<input type="number" id="offset" step="0.1" value="0">
</div>

<!-- Threshold -->
<div class="card">
<h2>⚠️ Threshold</h2>
<div class="slider-group">
<label>Threshold Normal (cm)</label>
<input type="range" id="thNormal" min="10" max="200" value="40"
       oninput="document.getElementById('thNormalVal').textContent=this.value">
<div class="slider-val"><span>10</span><span id="thNormalVal">40</span><span>200</span></div>
</div>
<div class="slider-group">
<label>Threshold Bahaya (cm)</label>
<input type="range" id="thBahaya" min="10" max="200" value="80"
       oninput="document.getElementById('thBahayaVal').textContent=this.value">
<div class="slider-val"><span>10</span><span id="thBahayaVal">80</span><span>200</span></div>
</div>
</div>

<!-- WiFi Config -->
<div class="card">
<h2>📶 Koneksi WiFi</h2>
<label>WiFi SSID</label>
<input type="text" id="ssid" placeholder="Nama jaringan WiFi">
<label>WiFi Password</label>
<input type="password" id="wifiPass" placeholder="Kata sandi WiFi">
</div>

<!-- Backend Config -->
<div class="card">
<h2>🖥️ Server Backend</h2>
<label>Backend Host</label>
<input type="text" id="backendHost" placeholder="192.168.1.100">
<label>Backend Port</label>
<input type="number" id="backendPort" value="3000">
</div>

<!-- Save Button -->
<form id="configForm">
<button type="submit" class="submit">💾 Simpan & Mulai Monitor</button>
<div class="status" id="status" style="display:none"></div>
</form>
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

var ws;
function requestPreview(){
  document.getElementById('btnPreview').textContent = '⏳ Mengambil gambar...';
  document.getElementById('btnPreview').disabled = true;
  ws.send(JSON.stringify({cmd:'request_preview'}));
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
    try{
      var d=JSON.parse(e.data);
      if(d.type==='telemetry'){
        document.getElementById('dist').textContent=d.dist_cm.toFixed(1)+' cm';
        document.getElementById('wl').textContent=d.water_level_cm.toFixed(1)+' cm';
        var hs=parseFloat(document.getElementById('heightSensor').value)||150;
        var pct=Math.min(100,Math.max(0,(d.water_level_cm/hs)*100));
        document.getElementById('bar').style.width=pct+'%';
        var r=document.getElementById('rain');
        if(d.rain){r.className='rain yes';r.textContent='🌧️ Hujan Terdeteksi';}
        else{r.className='rain no';r.textContent='☀️ Tidak Hujan';}
      }else if(d.type==='device_info'){
        document.getElementById('devId').textContent='Device: '+d.device_id;
      }
    }catch(ex){}
  };
  ws.onclose=function(){setTimeout(initWS,2000)};
}
initWS();

function setHeight(){
  var v=document.getElementById('dist');
  var t=v.textContent.replace(' cm','');
  if(t!=='--.-'){document.getElementById('heightSensor').value=parseFloat(t).toFixed(1);}
}

document.getElementById('configForm').onsubmit=function(e){
  e.preventDefault();
  var cfg={
    cmd:'save_config',
    ssid:document.getElementById('ssid').value,
    password:document.getElementById('wifiPass').value,
    backend_host:document.getElementById('backendHost').value,
    backend_port:parseInt(document.getElementById('backendPort').value),
    height_sensor:parseFloat(document.getElementById('heightSensor').value),
    offset:parseFloat(document.getElementById('offset').value),
    threshold_normal:parseInt(document.getElementById('thNormal').value),
    threshold_bahaya:parseInt(document.getElementById('thBahaya').value)
  };
  if(!cfg.ssid){alert('SSID wajib diisi!');return;}
  ws.send(JSON.stringify(cfg));
  var st=document.getElementById('status');
  st.style.display='block';
  st.style.background='#1e3a5f';
  st.style.color='#38bdf8';
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
        // Send device info
        JsonDocument doc;
        doc["type"] = "device_info";
        doc["device_id"] = DeviceID::get();
        char buf[128];
        serializeJson(doc, buf);
        client->text(buf);
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("[WS] Client #%u disconnected\n", client->id());
    } else if (type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo *)arg;
        if (info->opcode == WS_TEXT) {
            data[len] = 0;  // Null terminate
            JsonDocument doc;
            if (deserializeJson(doc, (char *)data)) return;

            const char *cmd = doc["cmd"] | "";
            if (strcmp(cmd, "request_preview") == 0) {
                previewRequested = true;
                Serial.println("[WebUI] Camera preview requested");
            } else if (strcmp(cmd, "save_config") == 0) {
                Serial.println("[WebUI] Saving configuration...");

                // Save WiFi config
                WiFiConfig wifi;
                memset(&wifi, 0, sizeof(wifi));
                strncpy(wifi.ssid, doc["ssid"] | "", sizeof(wifi.ssid) - 1);
                strncpy(wifi.password, doc["password"] | "", sizeof(wifi.password) - 1);
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
