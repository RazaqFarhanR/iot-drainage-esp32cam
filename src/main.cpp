#include <Arduino.h>
#include "core/nvs_manager.h"
#include "core/device_id.h"
#include "core/watchdog.h"
#include "core/state_machine.h"
#include "config/pins.h"
#include "config/defaults.h"
#include "modes/commissioning.h"
#include "modes/operational.h"
#include "modes/maintenance.h"
#include "modes/offline.h"
#include <driver/rtc_io.h>

/**
 * @file main.cpp
 * @brief IFMS — Intelligent Flood Monitoring System
 *        Entry point with mode dispatcher.
 *
 * Architecture: Clean modular design
 *  - config/    → Pin & constant definitions
 *  - core/      → State machine, NVS, Device ID, Watchdog
 *  - connectivity/ → WiFi, MQTT, HTTP, NTP
 *  - sensor/    → Ultrasonic, Rain, Self-check
 *  - camera/    → OV2640 handler
 *  - modes/     → Commissioning, Operational, Maintenance, Offline
 *  - web/       → Captive portal UI
 */

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n");
    Serial.println("╔══════════════════════════════════════════════╗");
    Serial.println("║  IFMS — Intelligent Flood Monitoring System  ║");
    Serial.println("║  ESP32-CAM AI-Thinker | Clean Architecture   ║");
    Serial.println("╚══════════════════════════════════════════════╝");
    Serial.println();

    // Release held pins from deep sleep
    rtc_gpio_hold_dis((gpio_num_t)PIN_ULTRASONIC_TRIG);
    rtc_gpio_hold_dis((gpio_num_t)PIN_ULTRASONIC_ECHO);

    // Initialize flash LED to OFF
    pinMode(PIN_FLASH_LED, OUTPUT);
    digitalWrite(PIN_FLASH_LED, LOW);

    // Initialize core systems
    Watchdog::init();
    NVSManager::init();
    DeviceID::init();
    StateMachine::init();
    StateMachine::logState();

    // Dispatch to appropriate mode
    bool running = true;
    while (running) {
        switch (StateMachine::getCurrentMode()) {
            case SystemMode::COMMISSIONING:
                CommissioningMode::run();
                // After commissioning, check if mode changed
                if (StateMachine::getCurrentMode() == SystemMode::OPERATIONAL) {
                    // run operational next
                    continue;
                }
                running = false;
                break;

            case SystemMode::OPERATIONAL:
                OperationalMode::run();
                // Operational calls enterDeepSleep() at the end
                // If it returns, a mode transition was requested
                if (StateMachine::getCurrentMode() == SystemMode::MAINTENANCE) {
                    continue;
                } else if (StateMachine::getCurrentMode() == SystemMode::COMMISSIONING) {
                    continue;
                } else if (StateMachine::getCurrentMode() == SystemMode::OFFLINE) {
                    continue;
                }
                running = false;
                break;

            case SystemMode::MAINTENANCE:
                MaintenanceMode::run();
                // After maintenance, go to operational (sleep)
                if (StateMachine::getCurrentMode() == SystemMode::OPERATIONAL) {
                    // Enter deep sleep for next operational cycle
                    Watchdog::setPhase(WDTPhase::SLEEP);
                    StateMachine::enterDeepSleep(SLEEP_NORMAL_SEC);
                }
                running = false;
                break;

            case SystemMode::OFFLINE:
                OfflineMode::run();
                // Offline calls enterDeepSleep() or transitions
                if (StateMachine::getCurrentMode() == SystemMode::COMMISSIONING) {
                    continue;
                }
                running = false;
                break;
        }
    }
}

void loop() {
    // All modes are run-to-completion in setup().
    // The device enters deep sleep at the end of each cycle.
    // This function should never be reached in normal operation.
}