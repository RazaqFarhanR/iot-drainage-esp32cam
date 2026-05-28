#pragma once

enum class WDTPhase {
    BOOT,       // 30s
    WIFI,       // 15s
    SENSOR,     // 10s
    UPLOAD,     // 45s
    SLEEP       // 5s
};

namespace Watchdog {
    /**
     * Initialize WDT with BOOT phase timeout.
     */
    void init();

    /**
     * Reconfigure WDT timeout for specific phase.
     */
    void setPhase(WDTPhase phase);

    /**
     * Feed/reset the watchdog timer.
     */
    void feed();

    /**
     * Disable WDT (e.g., before deep sleep).
     */
    void disable();
}
