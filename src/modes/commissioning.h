#pragma once

/**
 * @file commissioning.h
 * @brief Commissioning Mode — AP + WebSocket + Captive Portal (§2).
 */

namespace CommissioningMode {
    /**
     * Run commissioning mode.
     * Starts AP, Web UI, WebSocket telemetry.
     * Blocks until config saved or timeout.
     */
    void run();
}
