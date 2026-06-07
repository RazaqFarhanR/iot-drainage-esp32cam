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
static bool localCommissioning = false;

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

int StateMachine::detectBootClicks() {
    pinMode(PIN_FACTORY_RESET, INPUT_PULLUP);
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, HIGH); // Off (active low)

    Serial.println("[SM] Monitoring button clicks for 2.5s...");
    
    int clicks = 0;
    bool initialReading = digitalRead(PIN_FACTORY_RESET);
    bool stableState = initialReading;
    bool lastReading = initialReading;
    uint32_t startTime = millis();
    uint32_t lastDebounceTime = 0;
    const uint32_t debounceDelay = 50; // 50ms debounce for responsiveness

    while (millis() - startTime < 2500) {
        // Blink LED rapidly to indicate we are in the boot window
        if ((millis() - startTime) % 200 < 100) {
            digitalWrite(PIN_STATUS_LED, LOW);
        } else {
            digitalWrite(PIN_STATUS_LED, HIGH);
        }

        bool reading = digitalRead(PIN_FACTORY_RESET);
        if (reading != lastReading) {
            lastDebounceTime = millis();
            lastReading = reading;
        }

        if ((millis() - lastDebounceTime) > debounceDelay) {
            if (reading != stableState) {
                stableState = reading;
                if (stableState == LOW) {
                    clicks++;
                    Serial.printf("[SM] Click %d detected\n", clicks);
                    // Turn LED solid ON for a moment to acknowledge click
                    digitalWrite(PIN_STATUS_LED, LOW);
                    delay(100); 
                }
            }
        }
        delay(10);
    }
    
    digitalWrite(PIN_STATUS_LED, HIGH); // Turn off LED after window
    return clicks;
}

bool StateMachine::isLocalCommissioning() {
    return localCommissioning;
}

void StateMachine::setLocalCommissioning(bool state) {
    localCommissioning = state;
}

void StateMachine::init() {
    validateRTCData();

    // Check reset reason
    esp_reset_reason_t reason = esp_reset_reason();
    esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();

    Serial.printf("[SM] DIAGNOSTICS: Reset Reason = %d, Wakeup Cause = %d\n", (int)reason, (int)wakeup);

    // Temp pin check to diagnose instant wakeup
    pinMode(PIN_FACTORY_RESET, INPUT_PULLUP);
    pinMode(PIN_RAIN_SENSOR, INPUT_PULLUP);
    delay(50); // allow pin states to settle
    int resetBtnVal = digitalRead(PIN_FACTORY_RESET);
    int rainSensorVal = digitalRead(PIN_RAIN_SENSOR);
    Serial.printf("[SM] DIAGNOSTICS: Reset Button (GPIO %d) = %d (%s)\n", 
                  PIN_FACTORY_RESET, resetBtnVal, resetBtnVal == LOW ? "LOW" : "HIGH");
    Serial.printf("[SM] DIAGNOSTICS: Rain Sensor (GPIO %d) = %d (%s)\n", 
                  (int)PIN_RAIN_SENSOR, rainSensorVal, rainSensorVal == LOW ? "LOW" : "HIGH");

    if (reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT || reason == ESP_RST_INT_WDT) {
        bufferLog("ERROR", "Watchdog reset detected (System Hang)");
    } else if (reason == ESP_RST_BROWNOUT) {
        bufferLog("ERROR", "Perangkat ter-restart paksa karena tegangan drop (Brownout)");
    } else if (reason == ESP_RST_POWERON) {
        bufferLog("INFO", "Perangkat dihidupkan ulang secara fisik (Cold Boot)");
    }

    // Check for physical hardware button clicks (on cold boot or EXT1 deep sleep wake)
    if (wakeup == ESP_SLEEP_WAKEUP_UNDEFINED || wakeup == ESP_SLEEP_WAKEUP_EXT1) {
        int clicks = detectBootClicks();
        int totalClicks = clicks;
        
        // If it woke up from deep sleep due to button press, that counts as 1 click already
        if (wakeup == ESP_SLEEP_WAKEUP_EXT1) {
            totalClicks += 1;
            Serial.println("[SM] Boot reason: EXT1 (Button press woke device up)");
        }

        if (totalClicks == 2) {
            Serial.println("[SM] 2 Clicks detected -> Local Commissioning (STA mode)");
            setLocalCommissioning(true);
            currentMode = SystemMode::COMMISSIONING;
            return;
        } else if (totalClicks >= 3) {
            Serial.println("[SM] 3+ Clicks detected -> AP Commissioning & Factory Reset");
            NVSManager::factoryReset();
            setLocalCommissioning(false);
            currentMode = SystemMode::COMMISSIONING;
            return;
        }
        // If 1 click or 0 clicks, just continue normal boot
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

    // Enable EXT1 Wakeup for Factory Reset Button (GPIO 13)
    esp_sleep_enable_ext1_wakeup(1ULL << PIN_FACTORY_RESET, ESP_EXT1_WAKEUP_ALL_LOW);

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

    // Secure factory reset button pin (GPIO 13) to prevent floating and spurious wakeups
    rtc_gpio_pullup_en((gpio_num_t)PIN_FACTORY_RESET);
    rtc_gpio_pulldown_dis((gpio_num_t)PIN_FACTORY_RESET);

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
