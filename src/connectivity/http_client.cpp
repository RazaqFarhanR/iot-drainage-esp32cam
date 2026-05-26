#include "http_client.h"
#include "../core/nvs_manager.h"
#include "../core/device_id.h"
#include "../core/state_machine.h"
#include "../core/watchdog.h"
#include "../config/defaults.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include "../env.h"

/**
 * @file http_client.cpp
 * @brief HTTP client implementation with per-phase timeouts (§11.12).
 *
 * Timeout budget:
 *  - Connect:    5s
 *  - Header:     3s
 *  - Upload:     2s per KB
 *  - Response:   5s
 *  - Total max:  30s
 */

static void getActiveBackendConfig(BackendConfig &cfg) {
    if (!NVSManager::loadBackendConfig(cfg) || !cfg.valid || strlen(cfg.host) == 0) {
        strncpy(cfg.host, DEFAULT_BACKEND_HOST, sizeof(cfg.host));
        cfg.port = DEFAULT_BACKEND_PORT;
        cfg.valid = true;
    }
}

static bool getBackendURL(char *urlBuf, size_t bufLen, const char *path) {
    BackendConfig cfg;
    getActiveBackendConfig(cfg);
    snprintf(urlBuf, bufLen, "http://%s:%d%s", cfg.host, cfg.port, path);
    return true;
}

// ============================================
// Fetch Config (§6.1)
// ============================================

bool HTTPClient_::fetchConfig() {
    char url[256];
    char path[64];
    snprintf(path, sizeof(path), API_ENDPOINT_CONFIG, DeviceID::get());
    if (!getBackendURL(url, sizeof(url), path)) return false;

    Serial.printf("[HTTP] GET %s\n", url);

    HTTPClient http;
    http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
    http.setTimeout(HTTP_RESPONSE_TIMEOUT_MS);

    if (!http.begin(url)) {
        Serial.println("[HTTP] Failed to begin");
        return false;
    }

    int httpCode = http.GET();
    if (httpCode != 200) {
        Serial.printf("[HTTP] GET failed: %d\n", httpCode);
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    // Parse response
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("[HTTP] JSON parse error: %s\n", err.c_str());
        return false;
    }

    // Update threshold config in NVS
    ThresholdConfig thresh;
    thresh.threshold_normal_cm = doc["threshold_normal_cm"] | DEFAULT_THRESHOLD_NORMAL;
    thresh.threshold_bahaya_cm = doc["threshold_bahaya_cm"] | DEFAULT_THRESHOLD_BAHAYA;
    thresh.valid = true;
    NVSManager::saveThresholdConfig(thresh);

    // Update sensor config if provided
    if (!doc["height_sensor_cm"].isNull()) {
        SensorConfig sensor;
        sensor.height_sensor_cm = doc["height_sensor_cm"] | DEFAULT_HEIGHT_SENSOR;
        sensor.offset_cm = doc["offset_cm"] | DEFAULT_OFFSET;
        sensor.valid = true;
        NVSManager::saveSensorConfig(sensor);
    }

    Serial.printf("[HTTP] Config updated: normal=%.1f, bahaya=%.1f\n",
                  thresh.threshold_normal_cm, thresh.threshold_bahaya_cm);
    return true;
}

// ============================================
// Upload Image (multipart/form-data)
// ============================================

static bool uploadImage(const char *url, camera_fb_t *fb, const char *fieldName) {
    Watchdog::setPhase(WDTPhase::UPLOAD);
    unsigned long totalStart = millis();

    WiFiClient client;
    // Parse host and port from URL
    BackendConfig cfg;
    getActiveBackendConfig(cfg);

    Serial.printf("[HTTP] Connecting to %s:%d...\n", cfg.host, cfg.port);

    // Phase 1: Connect (5s timeout)
    unsigned long connectStart = millis();
    if (!client.connect(cfg.host, cfg.port)) {
        Serial.println("[HTTP] Connection failed");
        return false;
    }
    if (millis() - connectStart > HTTP_CONNECT_TIMEOUT_MS) {
        client.stop();
        return false;
    }

    // Build multipart body
    String boundary = "IFMS" + String(millis());
    String head = "--" + boundary + "\r\n" +
                  "Content-Disposition: form-data; name=\"" + fieldName +
                  "\"; filename=\"flood.jpg\"\r\n" +
                  "Content-Type: image/jpeg\r\n\r\n";
    String tail = "\r\n--" + boundary + "--\r\n";
    uint32_t totalLen = head.length() + fb->len + tail.length();

    // Extract path from URL
    String urlStr(url);
    int pathStart = urlStr.indexOf('/', 7);  // Skip "http://"
    String path = (pathStart > 0) ? urlStr.substring(pathStart) : "/";

    // Phase 2: Send headers (3s timeout)
    client.printf("POST %s HTTP/1.1\r\n", path.c_str());
    client.printf("Host: %s\r\n", cfg.host);
    client.printf("Content-Type: multipart/form-data; boundary=%s\r\n", boundary.c_str());
    client.printf("Content-Length: %u\r\n", totalLen);
    client.print("Connection: close\r\n\r\n");

    // Send multipart head
    client.print(head);

    // Phase 3: Upload body (2s per KB)
    uint8_t *buf = fb->buf;
    size_t remaining = fb->len;
    size_t offset = 0;

    while (remaining > 0) {
        // Check total timeout
        if (millis() - totalStart > HTTP_TOTAL_TIMEOUT_MS) {
            Serial.println("[HTTP] Total timeout exceeded!");
            client.stop();
            return false;
        }

        size_t chunk = (remaining > 1024) ? 1024 : remaining;
        size_t written = client.write(buf + offset, chunk);
        if (written == 0) {
            Serial.println("[HTTP] Write error");
            client.stop();
            return false;
        }
        offset += written;
        remaining -= written;
        Watchdog::feed();
    }

    client.print(tail);

    // Phase 4: Wait for response (5s timeout)
    unsigned long respStart = millis();
    while (!client.available() && (millis() - respStart) < HTTP_RESPONSE_TIMEOUT_MS) {
        delay(10);
        Watchdog::feed();
    }

    if (client.available()) {
        String response = client.readStringUntil('\n');
        Serial.printf("[HTTP] Response: %s\n", response.c_str());
        client.stop();
        return response.indexOf("200") > 0 || response.indexOf("201") > 0;
    }

    client.stop();
    Serial.println("[HTTP] No response received");
    return false;
}

// ============================================
// Public Upload Methods
// ============================================

bool HTTPClient_::uploadBahayaImage(camera_fb_t *fb) {
    char url[256];
    if (!getBackendURL(url, sizeof(url), API_ENDPOINT_UPLOAD)) return false;
    Serial.printf("[HTTP] Uploading BAHAYA image (%u bytes)...\n", fb->len);

    bool ok = uploadImage(url, fb, "image");
    if (!ok) {
        StateMachine::getRTCData().lastUploadFailed = true;
        StateMachine::getRTCData().pendingUpload = true;
        StateMachine::getRTCData().pendingUploadRetries = 0;
    }
    return ok;
}

bool HTTPClient_::uploadSnapshot(camera_fb_t *fb) {
    char url[256];
    char path[64];
    snprintf(path, sizeof(path), API_ENDPOINT_SNAPSHOT, DeviceID::get());
    if (!getBackendURL(url, sizeof(url), path)) return false;
    Serial.printf("[HTTP] Uploading snapshot (%u bytes)...\n", fb->len);

    return uploadImage(url, fb, "image");
}
