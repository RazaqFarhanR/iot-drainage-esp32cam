#include "watchdog.h"
#include "../config/defaults.h"
#include <esp_task_wdt.h>
#include <Arduino.h>

/**
 * @file watchdog.cpp
 * @brief Adaptive WDT implementation — reconfigure per phase (§11.13).
 */

static bool wdtInitialized = false;

static int getTimeoutForPhase(WDTPhase phase) {
    switch (phase) {
        case WDTPhase::BOOT:   return WDT_BOOT_SEC;
        case WDTPhase::WIFI:   return WDT_WIFI_SEC;
        case WDTPhase::SENSOR: return WDT_SENSOR_SEC;
        case WDTPhase::UPLOAD: return WDT_UPLOAD_SEC;
        case WDTPhase::SLEEP:  return WDT_SLEEP_SEC;
        default:               return WDT_BOOT_SEC;
    }
}

void Watchdog::init() {
    esp_task_wdt_init(WDT_BOOT_SEC, true);
    esp_task_wdt_add(NULL);
    wdtInitialized = true;
    Serial.printf("[WDT] Initialized (BOOT phase: %ds)\n", WDT_BOOT_SEC);
}

void Watchdog::setPhase(WDTPhase phase) {
    if (!wdtInitialized) return;
    int timeout = getTimeoutForPhase(phase);
    esp_task_wdt_init(timeout, true);
    feed();
    Serial.printf("[WDT] Phase changed → %ds\n", timeout);
}

void Watchdog::feed() {
    if (wdtInitialized) {
        esp_task_wdt_reset();
    }
}

void Watchdog::disable() {
    if (wdtInitialized) {
        esp_task_wdt_delete(NULL);
        wdtInitialized = false;
        Serial.println("[WDT] Disabled");
    }
}
