#pragma once

namespace RainSensor {
    /**
     * Initialize rain sensor pin.
     */
    void init();

    /**
     * Check if rain is currently detected (active LOW).
     */
    bool isRaining();

    /**
     * Check if this wake was triggered by rain (EXT0).
     */
    bool wasRainWake();

    /**
     * Update EXT0 wake counter. Call after each wake.
     * Handles cooldown logic:
     *  - EXT0 wake → counter++
     *  - Timer wake → counter = 0
     *  - Counter >= 5 → disable EXT0 for 5 min
     */
    void updateWakeCounter();

    /**
     * Check if EXT0 is in cooldown.
     */
    bool isEXT0Cooled();
}
