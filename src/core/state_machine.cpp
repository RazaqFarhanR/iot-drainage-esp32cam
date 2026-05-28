#include "state_machine.h"
#include "nvs_manager.h"
#include "../config/defaults.h"
#include "../config/pins.h"
#include <Arduino.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>

// RTC memory — survives deep sleep, lost on power cycle
RTC_DATA_ATTR static RTCData rtcData;

static SystemMode currentMode = SystemMode::COMMISSIONING;

// RTC Data Management

void StateMachine::validateRTCData() {
    if (rtcData.magic != 0x1F452026) {
        Serial.println("[SM] RTC data invalid — initializing...");
        memset(&rtcData, 0, sizeof(rtcData));
        rtcData.magic = 0x1F452026;
        rtcData.hasLastDistance = false;
        rtcData.offlineRetryCount = 0;
        rtcData.wifiFailCount = 0;
        rtcData.faultCounter = 0;
        rtcData.stuckCounter = 0;
        rtcData.ext0WakeCount = 0;
        rtcData.pendingUpload = false;
        rtcData.pendingUploadRetries = 0;
        rtcData.ntpSynced = false;
        rtcData.driftCounter = 0;
        rtcData.lastSnapshotDay = 0;
        rtcData.maintenanceRequested = false;
        rtcData.lastUploadFailed = false;
        rtcData.resetCount = 0;
    }
}

RTCData& StateMachine::getRTCData() {
    return rtcData;
}

// Boot Logic

bool StateMachine::checkDoubleReset() {
    uint32_t now = millis();
    if (rtcData.resetCount > 0 &&
        (now - rtcData.lastResetTime) < DOUBLE_RESET_WINDOW_MS) {
        rtcData.resetCount = 0;
        Serial.println("[SM] *** Double Reset detected ***");
        return true;
    }
    rtcData.lastResetTime = now;
    rtcData.resetCount = 1;

    // Wait briefly for potential second press
    delay(500);
    return false;
}

bool StateMachine::checkFactoryReset() {
    // Factory reset is triggered externally — we check if NVS
    // has a flag set by a long-press handler or if user requests via Serial.
    // For hardware detection, we rely on the boot count in RTC memory.
    // Long press > 10s would be handled by a GPIO ISR in a real implementation.
    // Here we expose it as API for the Commissioning mode web UI.
    return false;
}

void StateMachine::init() {
    validateRTCData();

    // Check wake cause
    esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();

    // Check if maintenance was requested via MQTT (set in RTC before sleep)
    if (rtcData.maintenanceRequested) {
        rtcData.maintenanceRequested = false;
        currentMode = SystemMode::MAINTENANCE;
        Serial.println("[SM] Boot → MAINTENANCE (MQTT request)");
        return;
    }

    // Check for double reset → Commissioning
    if (wakeup == ESP_SLEEP_WAKEUP_UNDEFINED) {
        // Fresh power-on or reset (not from deep sleep)
        if (checkDoubleReset()) {
            NVSManager::factoryReset();
            currentMode = SystemMode::COMMISSIONING;
            Serial.println("[SM] Boot → COMMISSIONING (double reset)");
            return;
        }
    }

    // Conditional boot logic: check NVS wifi_cfg
    if (NVSManager::hasValidWiFiConfig()) {
        // Has valid config — go operational
        if (rtcData.offlineRetryCount > 0) {
            currentMode = SystemMode::OFFLINE;
            Serial.println("[SM] Boot → OFFLINE (retry in progress)");
        } else {
            currentMode = SystemMode::OPERATIONAL;
            Serial.println("[SM] Boot → OPERATIONAL (NVS valid)");
        }
    } else {
        // No valid config — commissioning
        currentMode = SystemMode::COMMISSIONING;
        Serial.println("[SM] Boot → COMMISSIONING (NVS empty)");
    }
}

SystemMode StateMachine::getCurrentMode() {
    return currentMode;
}

void StateMachine::requestMode(SystemMode mode) {
    currentMode = mode;
    Serial.printf("[SM] Mode changed → %d\n", (int)mode);
}

// Deep Sleep (,)

uint64_t StateMachine::getOfflineBackoffSeconds() {
    uint16_t retry = rtcData.offlineRetryCount;
    if (retry <= 3) return BACKOFF_TIER1_SEC;
    if (retry <= 6) return BACKOFF_TIER2_SEC;
    if (retry <= 10) return BACKOFF_TIER3_SEC;
    return BACKOFF_TIER4_SEC;
}

void StateMachine::enterDeepSleep(uint64_t sleepSeconds) {
    Serial.printf("[SM] Entering deep sleep for %llu seconds...\n", sleepSeconds);

    // Release sensor pins
    rtc_gpio_hold_dis((gpio_num_t)PIN_ULTRASONIC_TRIG);
    rtc_gpio_hold_dis((gpio_num_t)PIN_ULTRASONIC_ECHO);

    // Configure timer wakeup
    esp_sleep_enable_timer_wakeup(sleepSeconds * 1000000ULL);

    // Configure EXT0 rain sensor wake ( — with cooldown)
    if (rtcData.ext0WakeCount < EXT0_COOLDOWN_THRESHOLD) {
        esp_sleep_enable_ext0_wakeup(PIN_RAIN_SENSOR, 0);  // Wake on LOW
    } else {
        Serial.println("[SM] EXT0 cooldown active — rain wake disabled");
    }

    // Isolate floating pins
    rtc_gpio_isolate(GPIO_NUM_12);
    rtc_gpio_isolate(GPIO_NUM_13);

    Serial.flush();
    esp_deep_sleep_start();
}

void StateMachine::logState() {
    const char* modeStr[] = {"COMMISSIONING", "OPERATIONAL", "MAINTENANCE", "OFFLINE"};
    Serial.printf("[SM] Current Mode: %s | WiFi Fails: %d | Offline Retries: %d | Faults: %d\n",
                  modeStr[(int)currentMode],
                  rtcData.wifiFailCount,
                  rtcData.offlineRetryCount,
                  rtcData.faultCounter);
}
