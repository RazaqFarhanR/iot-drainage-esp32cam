#pragma once

namespace CommissioningMode {
    /**
     * Run commissioning mode.
     * Starts AP, Web UI, WebSocket telemetry.
     * Blocks until config saved or timeout.
     */
    void run();
}
