#pragma once

/**
 * @file device_id.h
 * @brief Auto-generate device ID from MAC address (§11.10).
 * Format: "IFMS-XXYYZZ" (3 last bytes of MAC in hex)
 */

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

    /**
     * Get the device location string.
     */
    const char* getLocation();
}
