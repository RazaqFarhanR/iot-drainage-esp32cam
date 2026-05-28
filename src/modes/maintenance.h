#pragma once

namespace MaintenanceMode {
    /**
     * Run maintenance mode.
     * Stays awake for 3 minutes, processes diagnostic commands.
     */
    void run();
}
