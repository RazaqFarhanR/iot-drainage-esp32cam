#pragma once

#include <cstdint>
#include "ultrasonic.h"

/**
 * @file self_check.h
 * @brief Auto self-check & progressive fault escalation (§3.5, §11.3).
 *
 * Flags:
 *  - SENSOR_FAULT:    median=0 or >500
 *  - SENSOR_UNSTABLE: variance >50cm²
 *  - SPIKE_DETECTED:  Δ >30cm in 1 cycle
 *  - SENSOR_STUCK:    constant for 5 cycles
 *  - SENSOR_SUBMERGED: fault >3x (forced BAHAYA)
 *  - SENSOR_DISPLACED: baseline drift >40cm for 3 cycles
 */

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
     * Update baseline average (§11.9).
     * Called after each valid measurement.
     * @param waterLevel Current water level
     */
    void updateBaseline(float waterLevel);

    /**
     * Get sensor flag as string.
     */
    const char* flagToString(SensorFlag flag);
}
