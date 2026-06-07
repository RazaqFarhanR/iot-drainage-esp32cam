#include <Arduino.h>
#include "core/nvs_manager.h"
#include "core/device_id.h"
#include "core/watchdog.h"
#include "core/state_machine.h"
#include "config/pins.h"
#include "config/defaults.h"
#include "modes/commissioning.h"
#include "modes/operational.h"

#include "modes/offline.h"
#include <driver/rtc_io.h>

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n");
    Serial.println("╔══════════════════════════════════════════════╗");
    Serial.println("║  IoTDrainage — Intelligent Flood Monitoring  ║");
    Serial.println("║  ESP32-CAM AI-Thinker | Clean Architecture   ║");
    Serial.println("╚══════════════════════════════════════════════╝");
    Serial.println();

    // Release hold latch and de-isolate pins that were isolated during deep sleep
    rtc_gpio_hold_dis((gpio_num_t)PIN_ULTRASONIC_TRIG);
    rtc_gpio_hold_dis((gpio_num_t)PIN_ULTRASONIC_ECHO);
    rtc_gpio_hold_dis((gpio_num_t)PIN_RAIN_SENSOR);
    rtc_gpio_hold_dis((gpio_num_t)PIN_FACTORY_RESET);

    rtc_gpio_deinit((gpio_num_t)PIN_ULTRASONIC_TRIG);
    rtc_gpio_deinit((gpio_num_t)PIN_ULTRASONIC_ECHO);
    rtc_gpio_deinit((gpio_num_t)PIN_RAIN_SENSOR);
    rtc_gpio_deinit((gpio_num_t)PIN_FACTORY_RESET);

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
            if (StateMachine::getCurrentMode() == SystemMode::COMMISSIONING) {
                continue;
            } else if (StateMachine::getCurrentMode() == SystemMode::OFFLINE) {
                continue;
            } else if (StateMachine::getCurrentMode() == SystemMode::MAINTENANCE) {
                continue;
            }
            running = false;
            break;

        case SystemMode::MAINTENANCE:
            Serial.println("[MAIN] Maintenance requested. Entering safe hibernation...");
            StateMachine::getRTCData().maintenanceRequested = false;
            StateMachine::getRTCData().stuckCounter = 0;
            // Sleep for 1 hour to prevent battery drain while waiting for technician
            StateMachine::enterDeepSleep(3600);
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