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
    if (rtcData.magic != RTC_DATA_MAGIC) {
        Serial.println("[SM] RTC data invalid — initializing...");
        memset(&rtcData, 0, sizeof(rtcData));
        rtcData.magic = RTC_DATA_MAGIC;
        rtcData.hasLastDistance = false;
        rtcData.baselineAvg = 0.0f;
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

        rtcData.lastUploadFailed = false;
        rtcData.maintenanceRequested = false;
        rtcData.resetCount = 0;
        memset(rtcData.pendingLog, 0, sizeof(rtcData.pendingLog));
        memset(rtcData.pendingLogLevel, 0, sizeof(rtcData.pendingLogLevel));
    }
}

RTCData& StateMachine::getRTCData() {
    return rtcData;
}

// Priority values: ERROR(3) > WARNING(2) > INFO(1)
static int getLogLevelPriority(const char* level) {
    if (strcmp(level, "ERROR") == 0) return 3;
    if (strcmp(level, "WARNING") == 0) return 2;
    if (strcmp(level, "INFO") == 0) return 1;
    return 0;
}

void StateMachine::bufferLog(const char* level, const char* message) {
    if (rtcData.pendingLog[0] == '\0') {
        // Buffer is empty, just write
        strncpy(rtcData.pendingLogLevel, level, sizeof(rtcData.pendingLogLevel) - 1);
        strncpy(rtcData.pendingLog, message, sizeof(rtcData.pendingLog) - 1);
    } else {
        // Buffer is not empty, check priority
        int currentPrio = getLogLevelPriority(rtcData.pendingLogLevel);
        int newPrio = getLogLevelPriority(level);
        if (newPrio >= currentPrio) {
            strncpy(rtcData.pendingLogLevel, level, sizeof(rtcData.pendingLogLevel) - 1);
            strncpy(rtcData.pendingLog, message, sizeof(rtcData.pendingLog) - 1);
            Serial.printf("[SM] Overwriting log with higher/equal priority: %s\n", level);
        } else {
            Serial.printf("[SM] Skipping log (%s), current buffer has higher priority (%s)\n", level, rtcData.pendingLogLevel);
        }
    }
}

// Boot Logic

bool StateMachine::checkDoubleReset() {
    if (rtcData.resetCount > 0) {
        rtcData.resetCount = 0;
        Serial.println("[SM] *** Double Reset detected ***");
        return true;
    }
    rtcData.resetCount = 1;
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

    // Check reset reason
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT || reason == ESP_RST_INT_WDT) {
        bufferLog("ERROR", "Watchdog reset detected (System Hang)");
    } else if (reason == ESP_RST_BROWNOUT) {
        bufferLog("ERROR", "Perangkat ter-restart paksa karena tegangan drop (Brownout)");
    } else if (reason == ESP_RST_POWERON) {
        bufferLog("INFO", "Perangkat dihidupkan ulang secara fisik (Cold Boot)");
    }

    esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();



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
    rtcData.resetCount = 0;

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
        rtcData.ext0WakeCount = 0; // Reset for the next timer cycle
    }

    // Isolate floating pins
    rtc_gpio_isolate((gpio_num_t)PIN_ULTRASONIC_TRIG);
    rtc_gpio_isolate((gpio_num_t)PIN_ULTRASONIC_ECHO);

    // Secure rain sensor pin
    rtc_gpio_pullup_en((gpio_num_t)PIN_RAIN_SENSOR);

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
