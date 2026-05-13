#pragma once

/**
 * @file ultrasonic.h
 * @brief AJ-SR04M ultrasonic sensor with median filter & smoothing (§4.1).
 *
 * Sampling:
 *  - Operational: 15 samples, 50ms delay
 *  - Commissioning: 5 samples, 500ms interval
 *  - Diagnostic: 30 samples
 *
 * Filter: Median of valid readings (exclude 0 and >500cm)
 * Smoothing: D_final = 0.4 * median + 0.6 * D_last (via RTC memory)
 */

struct MeasurementResult {
    float   median;           // Median of valid samples
    float   smoothed;         // After smoothing with history (D_final)
    float   waterLevel;       // Height_sensor - smoothed + offset
    float   variance;         // Variance of valid samples
    float   minVal;           // Minimum valid reading
    float   maxVal;           // Maximum valid reading
    int     validCount;       // Number of valid samples
    int     totalCount;       // Total samples attempted
    bool    valid;            // At least 1 valid reading
};

namespace Ultrasonic {
    /**
     * Initialize sensor pins.
     */
    void init();

    /**
     * Take measurement with specified sample count.
     * Applies median filter and smoothing.
     * @param sampleCount Number of samples to take
     * @param applySmoothing Apply EMA smoothing with RTC history
     * @return MeasurementResult with all statistics
     */
    MeasurementResult measure(int sampleCount, bool applySmoothing = true);

    /**
     * Take a single raw distance reading.
     * @return distance in cm, or -1.0 if failed
     */
    float measureSingle();

    /**
     * Calculate water level from distance.
     * @param distance Measured distance (cm)
     * @return Water level (cm) = Height_sensor - distance + offset
     */
    float calculateWaterLevel(float distance);
}
