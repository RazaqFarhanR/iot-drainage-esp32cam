#pragma once

/**
 * @file operational.h
 * @brief Operational Mode — STA + Deep Sleep cycle (§4.2).
 */

namespace OperationalMode {
    /**
     * Run one operational cycle.
     * Lifecycle: Wake → Connect → Self-Check → Sync Config
     *            → Measure → Process → Transmit → Sleep
     */
    void run();
}
