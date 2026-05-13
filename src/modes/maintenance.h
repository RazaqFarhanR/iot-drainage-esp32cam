#pragma once

/**
 * @file maintenance.h
 * @brief Maintenance Mode — Diagnostics & on-demand snapshot (§3).
 */

namespace MaintenanceMode {
    /**
     * Run maintenance mode.
     * Stays awake for 3 minutes, processes diagnostic commands.
     */
    void run();
}
