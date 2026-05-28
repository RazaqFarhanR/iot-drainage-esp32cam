#pragma once

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
