#include "ultrasonic.h"
#include "../core/nvs_manager.h"
#include "../core/state_machine.h"
#include "../core/watchdog.h"
#include "../config/pins.h"
#include "../config/defaults.h"
#include <Arduino.h>
#include <algorithm>

/**
 * @file ultrasonic.cpp
 * @brief AJ-SR04M measurement with median filter + EMA smoothing (§4.1).
 */

void Ultrasonic::init() {
    pinMode(PIN_ULTRASONIC_TRIG, OUTPUT);
    digitalWrite(PIN_ULTRASONIC_TRIG, LOW);
    pinMode(PIN_ULTRASONIC_ECHO, INPUT);
    delay(50);  // Stabilize
    Serial.println("[Sensor] Ultrasonic initialized");
}

float Ultrasonic::measureSingle() {
    // Trigger pulse — 10µs HIGH
    digitalWrite(PIN_ULTRASONIC_TRIG, LOW);
    delayMicroseconds(5);
    digitalWrite(PIN_ULTRASONIC_TRIG, HIGH);
    delayMicroseconds(15);  // >10µs for AJ-SR04M
    digitalWrite(PIN_ULTRASONIC_TRIG, LOW);

    // Measure echo — timeout 40ms (~6.8m max)
    long duration = pulseIn(PIN_ULTRASONIC_ECHO, HIGH, 40000);

    if (duration > 0 && duration < 40000) {
        float distance = (duration / 2.0f) * 0.0343f;
        return distance;
    }
    return -1.0f;
}

MeasurementResult Ultrasonic::measure(int sampleCount, bool applySmoothing) {
    Watchdog::setPhase(WDTPhase::SENSOR);

    MeasurementResult result;
    memset(&result, 0, sizeof(result));
    result.totalCount = sampleCount;
    result.minVal = 9999.0f;
    result.maxVal = 0.0f;

    // Collect samples
    float samples[50];  // Max 50 samples
    int validIdx = 0;

    for (int i = 0; i < sampleCount && i < 50; i++) {
        Watchdog::feed();
        float d = measureSingle();

        // Filter artefacts (§4.1): discard 0 and >500
        if (d > SENSOR_MIN_CM && d <= SENSOR_MAX_CM) {
            samples[validIdx++] = d;
            if (d < result.minVal) result.minVal = d;
            if (d > result.maxVal) result.maxVal = d;
        }

        delay(SAMPLE_DELAY_MS);
    }

    result.validCount = validIdx;

    if (validIdx == 0) {
        result.valid = false;
        result.median = 0.0f;
        result.smoothed = 0.0f;
        result.minVal = 0.0f;
        result.maxVal = 0.0f;
        Serial.println("[Sensor] No valid readings!");
        return result;
    }

    result.valid = true;

    // Sort for median
    std::sort(samples, samples + validIdx);
    result.median = samples[validIdx / 2];

    // Calculate variance
    float sum = 0, sumSq = 0;
    for (int i = 0; i < validIdx; i++) {
        sum += samples[i];
        sumSq += samples[i] * samples[i];
    }
    float mean = sum / validIdx;
    result.variance = (sumSq / validIdx) - (mean * mean);

    // Smoothing with RTC history (§4.1)
    RTCData &rtc = StateMachine::getRTCData();
    if (applySmoothing && rtc.hasLastDistance) {
        // D_final = 0.4 * median_new + 0.6 * D_last
        result.smoothed = (SMOOTHING_ALPHA * result.median) +
                          ((1.0f - SMOOTHING_ALPHA) * rtc.lastDistance);
    } else {
        // Bootstrap: first cycle — D_final = median (§4.1)
        result.smoothed = result.median;
    }

    // Update RTC history
    rtc.lastDistance = result.smoothed;
    rtc.hasLastDistance = true;

    // Calculate water level
    result.waterLevel = calculateWaterLevel(result.smoothed);

    Serial.printf("[Sensor] Median=%.1f Smoothed=%.1f WaterLevel=%.1f "
                  "Var=%.2f Valid=%d/%d\n",
                  result.median, result.smoothed, result.waterLevel,
                  result.variance, validIdx, sampleCount);

    return result;
}

float Ultrasonic::calculateWaterLevel(float distance) {
    SensorConfig cfg;
    float height = DEFAULT_HEIGHT_SENSOR;
    float offset = DEFAULT_OFFSET;

    if (NVSManager::loadSensorConfig(cfg) && cfg.valid) {
        height = cfg.height_sensor_cm;
        offset = cfg.offset_cm;
    }

    // §6: Water_Level = Height_sensor - Measured_Distance + Offset
    float level = height - distance + offset;
    return (level < 0) ? 0.0f : level;
}
