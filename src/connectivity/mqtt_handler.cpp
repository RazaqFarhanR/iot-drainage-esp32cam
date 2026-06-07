#include "mqtt_handler.h"
#include "../core/device_id.h"
#include "../core/nvs_manager.h"
#include "../core/watchdog.h"
#include "../config/defaults.h"
#include <PubSubClient.h>
#include <WiFi.h>
#include <Arduino.h>
#include <mbedtls/md.h>
#include "../core/state_machine.h"
#include <esp_sleep.h>
#include <esp_system.h>

static WiFiClient mqttWiFiClient;
static PubSubClient mqttClient(mqttWiFiClient);

// Topic buffers
static char topicTelemetry[64];
static char topicLog[64];

// Helpers for advanced logging
static const char* getWakeReasonString() {
    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_EXT0: return "EXT0_RAIN";
        case ESP_SLEEP_WAKEUP_TIMER: return "TIMER";
        case ESP_SLEEP_WAKEUP_TOUCHPAD: return "TOUCH";
        case ESP_SLEEP_WAKEUP_ULP: return "ULP";
        case ESP_SLEEP_WAKEUP_UNDEFINED: return "POWER_ON";
        default: return "OTHER";
    }
}

static const char* getResetReasonString() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON: return "POWERON_RESET";
        case ESP_RST_EXT: return "EXTERNAL_RESET";
        case ESP_RST_SW: return "SW_RESET";
        case ESP_RST_PANIC: return "PANIC";
        case ESP_RST_INT_WDT: return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT: return "OTHER_WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP_RESET";
        case ESP_RST_BROWNOUT: return "BROWNOUT_RESET";
        case ESP_RST_SDIO: return "SDIO_RESET";
        default: return "UNKNOWN";
    }
}

// Public API

bool MQTTHandler::connect() {
    // Build topics
    const char *deviceId = DeviceID::get();
    snprintf(topicTelemetry, sizeof(topicTelemetry), "%s", MQTT_TOPIC_TELEMETRY);
    snprintf(topicLog, sizeof(topicLog), "%s", MQTT_TOPIC_LOG);

    // Get broker config
    BackendConfig backend;
    const char *broker = MQTT_BROKER_DEFAULT;
    int port = MQTT_PORT_DEFAULT;
    if (NVSManager::loadBackendConfig(backend) && backend.valid) {
        // Use backend host as MQTT broker in dev
        // In production, this would be a separate config
    }

    mqttClient.setServer(broker, port);
    mqttClient.setBufferSize(768);

    int attempts = 0;
    while (!mqttClient.connected() && attempts < 3) {
        Watchdog::feed();
        Serial.printf("[MQTT] Connecting to %s:%d...\n", broker, port);

        if (mqttClient.connect(deviceId, MQTT_USER, MQTT_PASS)) {
            Serial.println("[MQTT] Connected!");
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

bool MQTTHandler::publishTelemetry(float rawDistance, float waterLevel, const char *status, bool rainDetected, const char *sensorFlag, uint64_t nextWakeupSec) {
    if (!mqttClient.connected()) return false;

    // Fetch location from NVS
    LocationConfig lCfg;
    const char* locStr = "Unknown";
    if (NVSManager::loadLocationConfig(lCfg) && lCfg.valid && strlen(lCfg.location) > 0) {
        locStr = lCfg.location;
    }

    JsonDocument doc;
    doc["device_id"] = DeviceID::get();
    doc["location"] = locStr;
    doc["water_distance"] = rawDistance;
    doc["water_level_cm"] = waterLevel;
    doc["status"] = status;
    doc["rain_detected"] = rainDetected;
    doc["sensor_flag"] = sensorFlag;
    doc["timestamp"] = (uint32_t)time(nullptr);
    doc["next_wakeup_sec"] = nextWakeupSec;

    char buffer[384];
    serializeJson(doc, buffer, sizeof(buffer));

    bool ok = mqttClient.publish(topicTelemetry, buffer);
    Serial.printf("[MQTT] Telemetry %s\n", ok ? "sent" : "FAILED");
    return ok;
}



bool MQTTHandler::publishLog(const char *level, const char *message) {
    if (!mqttClient.connected()) return false;

    JsonDocument doc;
    doc["device_id"] = DeviceID::get();
    doc["timestamp"] = (uint32_t)time(nullptr);
    doc["level"] = level;
    doc["message"] = message;
    
    // Diagnostic Metadata (Ultimate RCA)
    doc["wake_reason"] = getWakeReasonString();
    doc["reset_reason"] = getResetReasonString();
    doc["active_time_ms"] = millis();
    doc["wifi_rssi_dbm"] = WiFi.RSSI();
    doc["network_failures"] = StateMachine::getRTCData().wifiFailCount;
    doc["free_heap_bytes"] = ESP.getFreeHeap();

    char buffer[512];
    serializeJson(doc, buffer, sizeof(buffer));

    // Ensure MQTT client can handle this payload size
    mqttClient.setBufferSize(768);

    // Using QoS 0 for publish since PubSubClient doesn't easily support QoS 1
    bool ok = mqttClient.publish(topicLog, buffer);
    Serial.printf("[MQTT] Log [%s]: %s (%s)\n", level, message, ok ? "sent" : "FAILED");
    return ok;
}


void MQTTHandler::disconnect() {
    if (mqttClient.connected()) {
        mqttClient.disconnect();
    }
}
