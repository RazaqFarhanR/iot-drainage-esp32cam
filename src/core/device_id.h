#pragma once

namespace DeviceID {
    /**
     * Initialize device ID. Generates from MAC if not in NVS.
     * Must be called after NVSManager::init().
     */
    void init();

    /**
     * Get the cached device ID string.
     * @return e.g. "IFMS-A1B2C3"
     */
    const char* get();
}
