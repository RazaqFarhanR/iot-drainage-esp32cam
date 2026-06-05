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

namespace OperationalMode {

static void ledSuccess() {
    pinMode(PIN_EXT_LED, OUTPUT);
    digitalWrite(PIN_EXT_LED, HIGH); // Active-High: ON
    delay(LED_SUCCESS_BLINK_MS);
    digitalWrite(PIN_EXT_LED, LOW);  // Active-High: OFF
}

static void ledFail() {
    pinMode(PIN_EXT_LED, OUTPUT);
    for (int i = 0; i < LED_FAIL_BLINK_COUNT; i++) {
        digitalWrite(PIN_EXT_LED, HIGH); // Active-High: ON
        delay(LED_FAIL_BLINK_MS);
        digitalWrite(PIN_EXT_LED, LOW);  // Active-High: OFF
        delay(LED_FAIL_BLINK_MS);
    }
}

static void onMQTTCommand(const char *cmd, JsonDocument &doc) {
    RTCData &rtc = StateMachine::getRTCData();
    const char *msg_id = doc["msg_id"] | "";

    if (strcmp(cmd, "enter_maintenance") == 0) {
        Serial.println("[OP] MQTT: Maintenance requested");
        rtc.maintenanceRequested = true;
        MQTTHandler::publishLog("INFO", "Sistem berpindah ke Mode Maintenance");
        MQTTHandler::publishResponse(cmd, msg_id, "SUCCESS", 200, "Entering maintenance mode");
    } else if (strcmp(cmd, "reboot_setup") == 0) {
        Serial.println("[OP] MQTT: Reboot to commissioning requested");
        NVSManager::factoryReset();
        MQTTHandler::publishLog("INFO", "Sistem berpindah ke Mode Commissioning");
        MQTTHandler::publishResponse(cmd, msg_id, "SUCCESS", 200, "Factory reset and rebooting");
        delay(500);
        ESP.restart();
    } else if (strcmp(cmd, "snapshot") == 0 || strcmp(cmd, "diagnostic") == 0) {
        Serial.println("[OP] MQTT: Diagnostic/Snapshot — entering maintenance next cycle");
        rtc.maintenanceRequested = true;
        MQTTHandler::publishResponse(cmd, msg_id, "SUCCESS", 200, "Will handle on next cycle");
    } else if (strcmp(cmd, "update_wifi") == 0) {
        const char *ssid = doc["ssid"] | "";
        const char *pass = doc["pass"] | "";
        if (strlen(ssid) > 0) {
            WiFiConfig wifiCfg;
            NVSManager::loadWiFiConfig(wifiCfg);
            strncpy(wifiCfg.ssid, ssid, sizeof(wifiCfg.ssid) - 1);
            strncpy(wifiCfg.password, pass, sizeof(wifiCfg.password) - 1);
            wifiCfg.valid = true;
            NVSManager::saveWiFiConfig(wifiCfg);
            MQTTHandler::publishResponse(cmd, msg_id, "SUCCESS", 200, "WiFi updated, rebooting");
            delay(1000);
            ESP.restart();
        } else {
            MQTTHandler::publishResponse(cmd, msg_id, "FAILED", 400, "Missing ssid");
        }
    } else if (strcmp(cmd, "set_thresholds") == 0) {
        float normal = doc["normal_max_cm"] | -1.0f;
        float bahaya = doc["waspada_max_cm"] | -1.0f;
        float baseline = doc["baseline_height_cm"] | -1.0f;
        
        if (normal >= 0 && bahaya >= 0 && baseline >= 0) {
            ThresholdConfig tCfg;
            tCfg.threshold_normal_cm = normal;
            tCfg.threshold_bahaya_cm = bahaya;
            tCfg.valid = true;
            NVSManager::saveThresholdConfig(tCfg);

            SensorConfig sCfg;
            NVSManager::loadSensorConfig(sCfg);
            sCfg.height_sensor_cm = baseline;
            sCfg.valid = true;
            NVSManager::saveSensorConfig(sCfg);

            MQTTHandler::publishResponse(cmd, msg_id, "SUCCESS", 200, "Thresholds updated");
        } else {
            MQTTHandler::publishResponse(cmd, msg_id, "FAILED", 400, "Invalid parameters");
        }
    } else if (strcmp(cmd, "force_snapshot") == 0) {
        if (Camera::init()) {
            camera_fb_t *fb = Camera::captureBestPhoto();
            if (fb) {
                if (HTTPClient_::uploadSnapshot(fb)) {
                    MQTTHandler::publishResponse(cmd, msg_id, "SUCCESS", 200, "Snapshot uploaded");
                } else {
                    MQTTHandler::publishResponse(cmd, msg_id, "FAILED", 500, "Upload failed");
                }
                Camera::returnFrame(fb);
            } else {
                MQTTHandler::publishResponse(cmd, msg_id, "FAILED", 500, "Capture failed");
            }
            Camera::deinit();
        } else {
            MQTTHandler::publishResponse(cmd, msg_id, "FAILED", 500, "Camera init failed");
        }
    } else if (strcmp(cmd, "set_flash_mode") == 0) {
        const char *mode = doc["mode"] | "";
        CameraConfig cCfg;
        cCfg.valid = true;
        if (strcmp(mode, "OFF") == 0) {
            cCfg.flash_mode = 0;
            NVSManager::saveCameraConfig(cCfg);
            MQTTHandler::publishResponse(cmd, msg_id, "SUCCESS", 200, "Flash mode set to OFF");
        } else if (strcmp(mode, "ON") == 0) {
            cCfg.flash_mode = 1;
            NVSManager::saveCameraConfig(cCfg);
            MQTTHandler::publishResponse(cmd, msg_id, "SUCCESS", 200, "Flash mode set to ON");
        } else if (strcmp(mode, "AUTO") == 0) {
            cCfg.flash_mode = 2;
            NVSManager::saveCameraConfig(cCfg);
            MQTTHandler::publishResponse(cmd, msg_id, "SUCCESS", 200, "Flash mode set to AUTO");
        } else {
            MQTTHandler::publishResponse(cmd, msg_id, "FAILED", 400, "Invalid mode");
        }
    } else {
        MQTTHandler::publishResponse(cmd, msg_id, "FAILED", 400, "Unknown command");
    }
}

bool connectNetwork() {
    Watchdog::setPhase(WDTPhase::WIFI);
    if (!WiFiMgr::connect()) {
        Serial.println("[OP] WiFi failed!");
        ledFail();
        bool goOffline = WiFiMgr::handleConnectFailure();
        if (goOffline) {
            RTCData &rtc = StateMachine::getRTCData();
            if (rtc.pendingLog[0] == '\0') {
                strncpy(rtc.pendingLog, "WiFi gagal terhubung, masuk ke Mode OFFLINE (Backoff)", sizeof(rtc.pendingLog) - 1);
                strncpy(rtc.pendingLogLevel, "WARNING", sizeof(rtc.pendingLogLevel) - 1);
            }
            StateMachine::requestMode(SystemMode::OFFLINE);
        } else {
            StateMachine::requestMode(SystemMode::COMMISSIONING);
        }
        return false;
    }
    return true;
}

MeasurementResult takeMeasurements(SelfCheckResult &checkResult) {
    Watchdog::setPhase(WDTPhase::SENSOR);
    Ultrasonic::init();
    
    MeasurementResult measurement = Ultrasonic::measure(OPERATIONAL_SAMPLE_COUNT, true);
    checkResult = SelfCheck::check(measurement);
    Serial.printf("[OP] Self-Check: %s\n", checkResult.flagStr);

    if (checkResult.shouldSkipSmoothing && measurement.valid) {
        Serial.println("[OP] Spike detected — using raw median");
        measurement.smoothed = measurement.median;
        measurement.waterLevel = Ultrasonic::calculateWaterLevel(measurement.median);
    }
    return measurement;
}

const char* determineStatus(const MeasurementResult &meas, const SelfCheckResult &check, uint64_t &sleepSec) {
    ThresholdConfig thresh;
    if (!NVSManager::loadThresholdConfig(thresh) || !thresh.valid) {
        thresh.threshold_normal_cm = DEFAULT_THRESHOLD_NORMAL;
        thresh.threshold_bahaya_cm = DEFAULT_THRESHOLD_BAHAYA;
    }

    const char *status = "NORMAL";
    sleepSec = SLEEP_NORMAL_SEC;

    if (check.forceBahaya) {
        status = "BAHAYA";
        sleepSec = SLEEP_BAHAYA_SEC;
    } else if (meas.valid) {
        if (meas.waterLevel >= thresh.threshold_bahaya_cm) {
            status = "BAHAYA";
            sleepSec = SLEEP_BAHAYA_SEC;
        } else if (meas.waterLevel >= thresh.threshold_normal_cm) {
            status = "WASPADA";
            sleepSec = SLEEP_WASPADA_SEC;
        }
    }

    if (check.overrideSleepSec > 0) {
        sleepSec = check.overrideSleepSec;
    }

    Serial.printf("[OP] Status: %s | WaterLevel: %.1f cm | Sleep: %llus\n", status, meas.waterLevel, sleepSec);
    
    if (meas.valid && !check.forceBahaya) {
        SelfCheck::updateBaseline(meas.waterLevel);
    }
    return status;
}

void transmitData(const MeasurementResult &meas, const SelfCheckResult &check, const char *status, bool rainDetected) {
    RTCData &rtc = StateMachine::getRTCData();
    bool mqttOk = MQTTHandler::connect();
    
    if (mqttOk) {
        if (rtc.pendingLog[0] != '\0') {
            MQTTHandler::publishLog(rtc.pendingLogLevel, rtc.pendingLog);
            memset(rtc.pendingLog, 0, sizeof(rtc.pendingLog));
        }
        MQTTHandler::setCommandCallback(onMQTTCommand);
        if (!check.shouldSkipData) {
            MQTTHandler::publishTelemetry(meas.waterLevel, meas.smoothed, status, check.flagStr,
                                          rainDetected, WiFiMgr::getRSSI(), NTPSync::isSynced(), rtc.lastUploadFailed);
        } else {
            MQTTHandler::publishTelemetry(0, 0, status, check.flagStr, rainDetected,
                                          WiFiMgr::getRSSI(), NTPSync::isSynced(), rtc.lastUploadFailed);
        }

        unsigned long mqttStart = millis();
        while (millis() - mqttStart < 3000) {
            MQTTHandler::loop();
            Watchdog::feed();
            delay(100);
        }
        ledSuccess();
    } else {
        ledFail();
    }
}

void handleCameraUploads(const char *status, bool shouldSkipData) {
    RTCData &rtc = StateMachine::getRTCData();

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
        if (MQTTHandler::isConnected()) {
            MQTTHandler::publishLog("ERROR", "Gagal mengunggah foto BAHAYA setelah batas maksimal percobaan");
        }
        rtc.pendingUpload = false;
        rtc.pendingUploadRetries = 0;
    }

    if (strcmp(status, "BAHAYA") == 0 && !shouldSkipData) {
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
}

void goToSleep(uint64_t sleepSec, const SelfCheckResult &check) {
    RTCData &rtc = StateMachine::getRTCData();
    MQTTHandler::disconnect();
    WiFiMgr::disconnect();

    if (RainSensor::isEXT0Cooled()) {
        sleepSec = EXT0_COOLDOWN_SLEEP_SEC;
        if (rtc.pendingLog[0] == '\0') {
            strncpy(rtc.pendingLog, "Hujan terus-menerus terdeteksi. Mengaktifkan jeda (Cooldown) 5 menit", sizeof(rtc.pendingLog) - 1);
            strncpy(rtc.pendingLogLevel, "WARNING", sizeof(rtc.pendingLogLevel) - 1);
        }
    }

    if (check.enterMaintenance) {
        rtc.maintenanceRequested = true;
    }

    Watchdog::setPhase(WDTPhase::SLEEP);
    StateMachine::enterDeepSleep(sleepSec);
}

void run() {
    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║    OPERATIONAL MODE                  ║");
    Serial.println("╚══════════════════════════════════════╝\n");

    // Force internal LED OFF (Active-Low)
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, HIGH);

    RainSensor::init();
    RainSensor::updateWakeCounter();
    bool rainDetected = RainSensor::wasRainWake() || RainSensor::isRaining();
    if (rainDetected) Serial.println("[OP] Rain detected!");

    RTCData &rtc = StateMachine::getRTCData();
    if (rtc.pendingLog[0] == '\0') {
        if (RainSensor::wasRainWake()) {
            strncpy(rtc.pendingLog, "Perangkat menyala karena Hujan (EXT0)", sizeof(rtc.pendingLog) - 1);
        } else {
            strncpy(rtc.pendingLog, "Perangkat menyala karena Timer (Rutin)", sizeof(rtc.pendingLog) - 1);
        }
        strncpy(rtc.pendingLogLevel, "INFO", sizeof(rtc.pendingLogLevel) - 1);
    }

    if (!connectNetwork()) return;

    NTPSync::sync();
    if (!NTPSync::isSynced() && MQTTHandler::isConnected()) {
        MQTTHandler::publishLog("WARNING", "Gagal sinkronisasi waktu NTP. Menggunakan estimasi RTC");
    }
    // HTTPClient_::fetchConfig();  // Disabled: Backend does not support dynamic config yet

    SelfCheckResult checkResult;
    MeasurementResult measurement = takeMeasurements(checkResult);

    uint64_t sleepSeconds = 0;
    const char *status = determineStatus(measurement, checkResult, sleepSeconds);

    transmitData(measurement, checkResult, status, rainDetected);
    handleCameraUploads(status, checkResult.shouldSkipData);

    RTCData &rtc = StateMachine::getRTCData();
    if (rtc.maintenanceRequested) {
        StateMachine::requestMode(SystemMode::MAINTENANCE);
        MQTTHandler::disconnect();
        WiFiMgr::disconnect();
        return;
    }

    goToSleep(sleepSeconds, checkResult);
}

} // namespace OperationalMode
