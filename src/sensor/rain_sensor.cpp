#include "rain_sensor.h"
#include "../core/state_machine.h"
#include "../config/pins.h"
#include "../config/defaults.h"
#include <Arduino.h>
#include <esp_sleep.h>

/**
 * @file rain_sensor.cpp
 * @brief Rain sensor with EXT0 wake cooldown to prevent false positive loops (§11.4).
 */

void RainSensor::init() {
    pinMode(PIN_RAIN_SENSOR, INPUT_PULLUP);
    Serial.println("[Rain] Sensor initialized");
}

bool RainSensor::isRaining() {
    return (digitalRead(PIN_RAIN_SENSOR) == LOW);
}

bool RainSensor::wasRainWake() {
    return (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0);
}

void RainSensor::updateWakeCounter() {
    RTCData &rtc = StateMachine::getRTCData();

    if (wasRainWake()) {
        rtc.ext0WakeCount++;
        Serial.printf("[Rain] EXT0 wake count: %d/%d\n",
                      rtc.ext0WakeCount, EXT0_COOLDOWN_THRESHOLD);

        if (rtc.ext0WakeCount >= EXT0_COOLDOWN_THRESHOLD) {
            Serial.println("[Rain] EXT0 cooldown triggered — disabling rain wake for 5 min");
            // Cooldown will be handled in StateMachine::enterDeepSleep()
            // by checking ext0WakeCount before enabling EXT0
        }
    } else {
        // Timer wake — reset counter
        if (rtc.ext0WakeCount > 0) {
            Serial.printf("[Rain] Timer wake — resetting EXT0 counter (was %d)\n",
                          rtc.ext0WakeCount);
        }
        rtc.ext0WakeCount = 0;
    }
}

bool RainSensor::isEXT0Cooled() {
    return (StateMachine::getRTCData().ext0WakeCount >= EXT0_COOLDOWN_THRESHOLD);
}
