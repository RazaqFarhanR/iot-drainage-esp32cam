#pragma once

namespace OfflineMode {
    /**
     * Run offline mode.
     * Attempts WiFi reconnect. If fails, sleeps with exponential backoff.
     */
    void run();
}
