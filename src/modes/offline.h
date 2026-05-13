#pragma once

/**
 * @file offline.h
 * @brief Offline Mode — exponential backoff retry (§11.1).
 */

namespace OfflineMode {
    /**
     * Run offline mode.
     * Attempts WiFi reconnect. If fails, sleeps with exponential backoff.
     */
    void run();
}
