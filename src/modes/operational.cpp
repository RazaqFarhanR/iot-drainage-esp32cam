#include "operational.h"
#include "../core/state_machine.h"
#include "../core/watchdog.h"
#include "../core/device_id.h"
#include "../core/nvs_manager.h"
#include "../config/defaults.h"
#include "../config/pins.h"
#include "../sensor/ultrasonic.h"
#include "../sensor/rain_sensor.h"
#include "../sensor/self_check.h"
#include "../camera/camera_handler.h"
#include "../connectivity/wifi_manager.h"
#include "../connectivity/mqtt_handler.h"
#include "../connectivity/http_client.h"
#include "../connectivity/ntp_sync.h"
#include <ArduinoJson.h>
#include <Arduino.h>

/**
 * @file operational.cpp
 * @brief Operational Mode — 8-step lifecycle per SRS §4.2.
 *
 * Steps:
 *  1. Wake-up (from deep sleep)
 *  2. Connect WiFi (fast connect)
 *  3. Self-Check (validate sensor)
 *  4. Sync Config (HTTP GET thresholds)
 *  5. Measure (15x sampling + median + smoothing)
 *  6. Process (determine NORMAL/WASPADA/BAHAYA)
 *  7. Transmit (MQTT telemetry; if BAHAYA → HTTP POST photo)
 *  8. Sleep (deep sleep based on status)
 */

// ============================================
// LED Indicators (§4.2)
// ============================================

static void ledSuccess() {
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, LOW);   // ON
    delay(LED_SUCCESS_BLINK_MS);
    digitalWrite(PIN_STATUS_LED, HIGH);  // OFF
}

static void ledFail() {
    pinMode(PIN_STATUS_LED, OUTPUT);
    for (int i = 0; i < LED_FAIL_BLINK_COUNT; i++) {
        digitalWrite(PIN_STATUS_LED, LOW);
        delay(LED_FAIL_BLINK_MS);
        digitalWrite(PIN_STATUS_LED, HIGH);
        delay(LED_FAIL_BLINK_MS);
    }
}

// ============================================
// MQTT Command Handler
// ============================================

static void onMQTTCommand(const char *cmd, JsonDocument &doc) {
    RTCData &rtc = StateMachine::getRTCData();

    if (strcmp(cmd, "enter_maintenance") == 0) {
        Serial.println("[OP] MQTT: Maintenance requested");
        rtc.maintenanceRequested = true;
    } else if (strcmp(cmd, "reboot_setup") == 0) {
        Serial.println("[OP] MQTT: Reboot to commissioning requested");
        NVSManager::factoryReset();
        ESP.restart();
    } else if (strcmp(cmd, "snapshot") == 0 || strcmp(cmd, "diagnostic") == 0) {
        Serial.println("[OP] MQTT: Diagnostic/Snapshot — entering maintenance next cycle");
        rtc.maintenanceRequested = true;
    }
}

// ============================================
// Main Operational Cycle
// ============================================

void OperationalMode::run() {
    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║    OPERATIONAL MODE                  ║");
    Serial.println("╚══════════════════════════════════════╝\n");

    RTCData &rtc = StateMachine::getRTCData();

    // --- Step 1: Wake-up ---
    Serial.println("[OP] Step 1: Wake-up");
    RainSensor::init();
    RainSensor::updateWakeCounter();
    bool rainDetected = RainSensor::wasRainWake() || RainSensor::isRaining();

    if (rainDetected) {
        Serial.println("[OP] Rain detected!");
    }

    // --- Step 2: Connect WiFi ---
    Serial.println("[OP] Step 2: Connect WiFi");
    Watchdog::setPhase(WDTPhase::WIFI);

    if (!WiFiMgr::connect()) {
        Serial.println("[OP] WiFi failed!");
        ledFail();

        bool goOffline = WiFiMgr::handleConnectFailure();
        if (goOffline) {
            StateMachine::requestMode(SystemMode::OFFLINE);
        } else {
            StateMachine::requestMode(SystemMode::COMMISSIONING);
        }
        return;  // Let main.cpp handle mode transition
    }

    // --- Step 3: Self-Check ---
    Serial.println("[OP] Step 3: Self-Check");
    Watchdog::setPhase(WDTPhase::SENSOR);
    Ultrasonic::init();

    // --- Step 4: Sync Config ---
    Serial.println("[OP] Step 4: Sync Config (HTTP GET)");
    NTPSync::sync();
    HTTPClient_::fetchConfig();  // Best effort — falls back to NVS

    // --- Step 5: Measure ---
    Serial.println("[OP] Step 5: Measure (15 samples)");
    MeasurementResult measurement = Ultrasonic::measure(OPERATIONAL_SAMPLE_COUNT, true);

    // Run self-check on measurement
    SelfCheckResult check = SelfCheck::check(measurement);
    Serial.printf("[OP] Self-Check: %s\n", check.flagStr);

    // If spike detected, re-measure without smoothing
    if (check.shouldSkipSmoothing && measurement.valid) {
        Serial.println("[OP] Spike detected — using raw median");
        measurement.smoothed = measurement.median;
        measurement.waterLevel = Ultrasonic::calculateWaterLevel(measurement.median);
    }

    // --- Step 6: Process ---
    Serial.println("[OP] Step 6: Determine status");

    // Load thresholds
    ThresholdConfig thresh;
    if (!NVSManager::loadThresholdConfig(thresh) || !thresh.valid) {
        thresh.threshold_normal_cm = DEFAULT_THRESHOLD_NORMAL;
        thresh.threshold_bahaya_cm = DEFAULT_THRESHOLD_BAHAYA;
    }

    // Determine status (§6.1)
    const char *status = "NORMAL";
    uint64_t sleepSeconds = SLEEP_NORMAL_SEC;

    if (check.forceBahaya) {
        status = "BAHAYA";
        sleepSeconds = SLEEP_BAHAYA_SEC;
    } else if (measurement.valid) {
        if (measurement.waterLevel >= thresh.threshold_bahaya_cm) {
            status = "BAHAYA";
            sleepSeconds = SLEEP_BAHAYA_SEC;
        } else if (measurement.waterLevel >= thresh.threshold_normal_cm) {
            status = "WASPADA";
            sleepSeconds = SLEEP_WASPADA_SEC;
        }
    }

    // Override sleep from self-check
    if (check.overrideSleepSec > 0) {
        sleepSeconds = check.overrideSleepSec;
    }

    Serial.printf("[OP] Status: %s | WaterLevel: %.1f cm | Sleep: %llus\n",
                  status, measurement.waterLevel, sleepSeconds);

    // Update baseline (§11.9)
    if (measurement.valid && !check.forceBahaya) {
        SelfCheck::updateBaseline(measurement.waterLevel);
    }

    // --- Step 7: Transmit ---
    Serial.println("[OP] Step 7: Transmit");

    // Connect MQTT
    bool mqttOk = MQTTHandler::connect();
    if (mqttOk) {
        MQTTHandler::setCommandCallback(onMQTTCommand);

        // Publish telemetry
        if (!check.shouldSkipData) {
            MQTTHandler::publishTelemetry(
                measurement.waterLevel,
                measurement.smoothed,
                status,
                check.flagStr,
                rainDetected,
                WiFiMgr::getRSSI(),
                NTPSync::isSynced(),
                rtc.lastUploadFailed
            );
        } else {
            // Still send alert for fault conditions
            MQTTHandler::publishTelemetry(
                0, 0, status, check.flagStr,
                rainDetected, WiFiMgr::getRSSI(),
                NTPSync::isSynced(), rtc.lastUploadFailed
            );
        }

        // Brief loop to receive any pending MQTT commands
        unsigned long mqttStart = millis();
        while (millis() - mqttStart < 2000) {
            MQTTHandler::loop();
            Watchdog::feed();
            delay(100);
        }
    }

    // Handle pending upload retry (§11.5)
    if (rtc.pendingUpload && rtc.pendingUploadRetries < MAX_UPLOAD_RETRY_CYCLES) {
        Serial.println("[OP] Retrying pending photo upload...");
        Watchdog::setPhase(WDTPhase::UPLOAD);
        if (Camera::init()) {
            camera_fb_t *fb = Camera::captureBestPhoto();
            if (fb) {
                if (HTTPClient_::uploadBahayaImage(fb)) {
                    rtc.pendingUpload = false;
                    rtc.pendingUploadRetries = 0;
                    Serial.println("[OP] Pending upload succeeded!");
                } else {
                    rtc.pendingUploadRetries++;
                }
                Camera::returnFrame(fb);
            }
            Camera::deinit();
        }
    } else if (rtc.pendingUpload) {
        Serial.println("[OP] Max upload retries reached — clearing pending");
        rtc.pendingUpload = false;
        rtc.pendingUploadRetries = 0;
    }

    // BAHAYA: Capture and upload photo (§6.1)
    if (strcmp(status, "BAHAYA") == 0 && !check.shouldSkipData) {
        Serial.println("[OP] BAHAYA — capturing photo...");
        Watchdog::setPhase(WDTPhase::UPLOAD);
        if (Camera::init()) {
            camera_fb_t *fb = Camera::captureBestPhoto();
            if (fb) {
                if (!HTTPClient_::uploadBahayaImage(fb)) {
                    Serial.println("[OP] Photo upload failed — flagging for retry");
                    rtc.lastUploadFailed = true;
                } else {
                    rtc.lastUploadFailed = false;
                }
                Camera::returnFrame(fb);
            }
            Camera::deinit();
        }
    }

    // Daily snapshot check (§3.4)
    if (NTPSync::isDailySnapshotDue(DAILY_SNAPSHOT_HOUR, DAILY_SNAPSHOT_MINUTE)) {
        Serial.println("[OP] Daily snapshot time!");
        Watchdog::setPhase(WDTPhase::UPLOAD);
        if (Camera::init()) {
            camera_fb_t *fb = Camera::captureBestPhoto();
            if (fb) {
                HTTPClient_::uploadSnapshot(fb);
                Camera::returnFrame(fb);
            }
            Camera::deinit();
        }
    }

    // Check if maintenance requested via MQTT
    if (rtc.maintenanceRequested) {
        StateMachine::requestMode(SystemMode::MAINTENANCE);
        MQTTHandler::disconnect();
        WiFiMgr::disconnect();
        return;  // Don't sleep — let main handle maintenance
    }

    // Transmit result LED
    if (mqttOk) {
        ledSuccess();
    } else {
        ledFail();
    }

    // --- Step 8: Sleep ---
    Serial.println("[OP] Step 8: Sleep");
    MQTTHandler::disconnect();
    WiFiMgr::disconnect();

    // Handle EXT0 cooldown (§11.4)
    if (RainSensor::isEXT0Cooled()) {
        sleepSeconds = EXT0_COOLDOWN_SLEEP_SEC;
        StateMachine::getRTCData().ext0WakeCount = 0;
    }

    // Enter maintenance if stuck sensor detected
    if (check.enterMaintenance) {
        rtc.maintenanceRequested = true;
        // Will enter maintenance on next wake
    }

    Watchdog::setPhase(WDTPhase::SLEEP);
    StateMachine::enterDeepSleep(sleepSeconds);
}
