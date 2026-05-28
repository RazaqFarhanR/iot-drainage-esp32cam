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
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, LOW);
    delay(LED_SUCCESS_BLINK_MS);
    digitalWrite(PIN_STATUS_LED, HIGH);
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

bool connectNetwork() {
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
        MQTTHandler::setCommandCallback(onMQTTCommand);
        if (!check.shouldSkipData) {
            MQTTHandler::publishTelemetry(meas.waterLevel, meas.smoothed, status, check.flagStr,
                                          rainDetected, WiFiMgr::getRSSI(), NTPSync::isSynced(), rtc.lastUploadFailed);
        } else {
            MQTTHandler::publishTelemetry(0, 0, status, check.flagStr, rainDetected,
                                          WiFiMgr::getRSSI(), NTPSync::isSynced(), rtc.lastUploadFailed);
        }

        unsigned long mqttStart = millis();
        while (millis() - mqttStart < 2000) {
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
        rtc.ext0WakeCount = 0;
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

    RainSensor::init();
    RainSensor::updateWakeCounter();
    bool rainDetected = RainSensor::wasRainWake() || RainSensor::isRaining();
    if (rainDetected) Serial.println("[OP] Rain detected!");

    if (!connectNetwork()) return;

    NTPSync::sync();
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
