#pragma once

#include <cstdint>
#include "ultrasonic.h"

enum class SensorFlag {
    OK,
    SENSOR_FAULT,
    SENSOR_UNSTABLE,
    SPIKE_DETECTED,
    SENSOR_STUCK,
    SENSOR_SUBMERGED,
    SENSOR_DISPLACED
};

struct SelfCheckResult {
    SensorFlag  flag;
    const char* flagStr;
    bool        shouldSkipData;     // Don't transmit numeric data
    bool        shouldSkipSmoothing;// Use raw median
    bool        forceBahaya;        // Force BAHAYA status
    bool        enterMaintenance;   // Trigger maintenance mode
    uint64_t    overrideSleepSec;   // 0 = use normal, >0 = override
};

namespace SelfCheck {
    /**
     * Run self-check on measurement result.
     * Updates RTC counters for fault escalation.
     * @param result The measurement to check
     * @return SelfCheckResult with flags and recommended actions
     */
    SelfCheckResult check(const MeasurementResult &result);

    /**
     * Update baseline average.
     * Called after each valid measurement.
     * @param waterLevel Current water level
     */
    void updateBaseline(float waterLevel);

    /**
     * Get sensor flag as string.
     */
    const char* flagToString(SensorFlag flag);
}
