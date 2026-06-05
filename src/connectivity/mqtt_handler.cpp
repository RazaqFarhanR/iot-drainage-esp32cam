#include "mqtt_handler.h"
#include "../core/device_id.h"
#include "../core/nvs_manager.h"
#include "../core/watchdog.h"
#include "../config/defaults.h"
#include <PubSubClient.h>
#include <WiFi.h>
#include <Arduino.h>
#include <mbedtls/md.h>

static WiFiClient mqttWiFiClient;
static PubSubClient mqttClient(mqttWiFiClient);
static MQTTCommandCallback cmdCallback = nullptr;

// Topic buffers
static char topicTelemetry[64];
static char topicDiagnostic[64];
static char topicCmd[64];
static char topicRes[64];
static char topicLog[64];

// HMAC-SHA256 Token Verification

static bool verifyToken(const char *token, uint32_t timestamp) {
    DeviceConfig cfg;
    bool hasSecret = NVSManager::loadDeviceConfig(cfg) && cfg.valid && strlen(cfg.device_secret) > 0;

    if (!hasSecret) {
        Serial.println("[MQTT] WARNING: No device secret — accepting in dev mode");
        return true;
    }

    if (!token || strlen(token) == 0) {
        return false;
    }

    // Verify HMAC-SHA256(device_secret, timestamp_string)
    char tsStr[16];
    snprintf(tsStr, sizeof(tsStr), "%u", timestamp);

    uint8_t hmacResult[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, (const uint8_t *)cfg.device_secret, strlen(cfg.device_secret));
    mbedtls_md_hmac_update(&ctx, (const uint8_t *)tsStr, strlen(tsStr));
    mbedtls_md_hmac_finish(&ctx, hmacResult);
    mbedtls_md_free(&ctx);

    // Convert to hex string
    char computed[65];
    for (int i = 0; i < 32; i++) {
        snprintf(computed + (i * 2), 3, "%02x", hmacResult[i]);
    }

    return (strcmp(token, computed) == 0);
}

// MQTT Callback

static void mqttCallback(char *topic, byte *payload, unsigned int length) {
    char message[512];
    size_t copyLen = (length < sizeof(message) - 1) ? length : sizeof(message) - 1;
    memcpy(message, payload, copyLen);
    message[copyLen] = '\0';

    Serial.printf("[MQTT] Received on %s: %s\n", topic, message);

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, message);
    if (err) {
        Serial.printf("[MQTT] JSON parse error: %s\n", err.c_str());
        return;
    }

    // Token verification
    const char *token = doc["token"] | "";
    uint32_t ts = doc["ts"] | 0;
    if (!verifyToken(token, ts)) {
        Serial.println("[MQTT] Token verification FAILED — ignoring command");
        return;
    }

    const char *cmd = doc["cmd"] | "";
    if (cmdCallback && strlen(cmd) > 0) {
        cmdCallback(cmd, doc);
    }
}

// Public API

bool MQTTHandler::connect() {
    // Build topics
    const char *deviceId = DeviceID::get();
    snprintf(topicTelemetry, sizeof(topicTelemetry), "%s", MQTT_TOPIC_TELEMETRY);
    snprintf(topicDiagnostic, sizeof(topicDiagnostic), "device/%s/diagnostic", deviceId);
    snprintf(topicCmd, sizeof(topicCmd), "device/%s/cmd", deviceId);
    snprintf(topicRes, sizeof(topicRes), "device/%s/res", deviceId);
    snprintf(topicLog, sizeof(topicLog), "device/%s/log", deviceId);

    // Get broker config
    BackendConfig backend;
    const char *broker = MQTT_BROKER_DEFAULT;
    int port = MQTT_PORT_DEFAULT;
    if (NVSManager::loadBackendConfig(backend) && backend.valid) {
        // Use backend host as MQTT broker in dev
        // In production, this would be a separate config
    }

    mqttClient.setServer(broker, port);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(512);

    int attempts = 0;
    while (!mqttClient.connected() && attempts < 3) {
        Watchdog::feed();
        Serial.printf("[MQTT] Connecting to %s:%d...\n", broker, port);

        if (mqttClient.connect(deviceId, MQTT_USER, MQTT_PASS)) {
            Serial.println("[MQTT] Connected!");
            // Subscribe to command topic
            mqttClient.subscribe(topicCmd, MQTT_QOS);
            Serial.printf("[MQTT] Subscribed to: %s\n", topicCmd);
            return true;
        }

        Serial.printf("[MQTT] Failed (rc=%d), retry %d/3\n",
                      mqttClient.state(), attempts + 1);
        attempts++;
        delay(1000);
    }

    return false;
}

void MQTTHandler::loop() {
    if (mqttClient.connected()) {
        mqttClient.loop();
    }
}

bool MQTTHandler::isConnected() {
    return mqttClient.connected();
}

bool MQTTHandler::publishTelemetry(float waterLevel, float rawDistance,
                                    const char *status, const char *sensorFlag,
                                    bool rainDetected, int rssi, bool timeSynced,
                                    bool lastUploadFailed) {
    if (!mqttClient.connected()) return false;

    JsonDocument doc;
    doc["device_id"] = DeviceID::get();
    doc["water_level_cm"] = waterLevel;
    doc["water_distance"] = rawDistance;
    doc["status"] = status;
    doc["sensor_flag"] = sensorFlag;
    doc["rain_detected"] = rainDetected;
    doc["rssi_dbm"] = rssi;
    doc["time_synced"] = timeSynced;
    doc["timestamp"] = (uint32_t)time(nullptr);

    if (lastUploadFailed) {
        doc["last_upload_failed"] = true;
    }

    char buffer[384];
    serializeJson(doc, buffer, sizeof(buffer));

    bool ok = mqttClient.publish(topicTelemetry, buffer);
    Serial.printf("[MQTT] Telemetry %s: %s\n", ok ? "sent" : "FAILED", status);
    return ok;
}

bool MQTTHandler::publishDiagnostic(int sampleCount, float median,
                                     float variance, float minVal, float maxVal,
                                     const char *sensorStatus) {
    if (!mqttClient.connected()) return false;

    JsonDocument doc;
    doc["type"] = "sensor_diagnostic";
    doc["device_id"] = DeviceID::get();
    doc["sample_count"] = sampleCount;
    doc["median_cm"] = median;
    doc["variance"] = variance;
    doc["min_cm"] = minVal;
    doc["max_cm"] = maxVal;
    doc["sensor_status"] = sensorStatus;

    char buffer[256];
    serializeJson(doc, buffer, sizeof(buffer));

    bool ok = mqttClient.publish(topicDiagnostic, buffer);
    Serial.printf("[MQTT] Diagnostic %s: %s\n", ok ? "sent" : "FAILED", sensorStatus);
    return ok;
}

bool MQTTHandler::publishResponse(const char *cmd, const char *msg_id, const char *status, int code, const char *message) {
    if (!mqttClient.connected()) return false;

    JsonDocument doc;
    doc["cmd"] = cmd;
    doc["msg_id"] = msg_id;
    doc["status"] = status;
    doc["code"] = code;
    doc["message"] = message;
    doc["timestamp"] = (uint32_t)time(nullptr);

    char buffer[256];
    serializeJson(doc, buffer, sizeof(buffer));

    bool ok = mqttClient.publish(topicRes, buffer);
    Serial.printf("[MQTT] Response to %s: %s\n", cmd, ok ? "sent" : "FAILED");
    return ok;
}

bool MQTTHandler::publishLog(const char *level, const char *message) {
    if (!mqttClient.connected()) return false;

    JsonDocument doc;
    doc["device_id"] = DeviceID::get();
    doc["level"] = level;
    doc["message"] = message;
    doc["timestamp"] = (uint32_t)time(nullptr);

    char buffer[384];
    serializeJson(doc, buffer, sizeof(buffer));

    // Using QoS 0 for publish since PubSubClient doesn't easily support QoS 1
    bool ok = mqttClient.publish(topicLog, buffer);
    Serial.printf("[MQTT] Log [%s]: %s (%s)\n", level, message, ok ? "sent" : "FAILED");
    return ok;
}

void MQTTHandler::setCommandCallback(MQTTCommandCallback callback) {
    cmdCallback = callback;
}

void MQTTHandler::disconnect() {
    if (mqttClient.connected()) {
        mqttClient.disconnect();
    }
}
