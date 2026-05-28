#pragma once

namespace WiFiMgr {
    /**
     * Connect to WiFi using saved NVS credentials.
     * Tries fast connect (static IP + BSSID) first, then DHCP fallback.
     * @return true if connected
     */
    bool connect();

    /**
     * Start Access Point mode for commissioning.
     * SSID format: "IFMS-{device_id}"
     */
    void startAP();

    /**
     * Stop AP mode.
     */
    void stopAP();

    /**
     * Disconnect WiFi and save connection info to NVS.
     */
    void disconnect();

    /**
     * Check current connection status.
     */
    bool isConnected();

    /**
     * Get current RSSI value.
     */
    int getRSSI();

    /**
     * Save current connection parameters (IP, GW, BSSID, Channel) to NVS.
     * Called after successful DHCP or first connect.
     */
    void saveConnectionInfo();

    /**
     * Handle WiFi failure with retry counting.
     * After 3 consecutive failures → triggers mode transition.
     * @return true if should enter offline mode, false if should enter commissioning
     */
    bool handleConnectFailure();
}
