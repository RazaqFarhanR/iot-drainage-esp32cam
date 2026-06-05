#pragma once

#include <cstdint>
#include <cstring>

// WiFi configuration stored in NVS
struct WiFiConfig {
    char     ssid[33];
    char     password[65];
    char     ip[16];
    char     gateway[16];
    char     subnet[16];
    uint8_t  bssid[6];
    uint8_t  channel;
    bool     valid;
};

// Sensor calibration stored in NVS
struct SensorConfig {
    float    height_sensor_cm;
    float    offset_cm;
    bool     valid;
};

// Threshold configuration stored in NVS
struct ThresholdConfig {
    float    threshold_normal_cm;
    float    threshold_bahaya_cm;
    bool     valid;
};

// Backend server configuration
struct BackendConfig {
    char     host[128];
    uint16_t port;
    bool     valid;
};

// Auth configuration
struct AuthConfig {
    char     pin[5];       // 4-digit + null
    int      fail_count;
    unsigned long lockout_until;  // millis timestamp
    bool     valid;
};

// Camera configuration
struct CameraConfig {
    uint8_t  flash_mode; // 0=OFF, 1=ON, 2=AUTO
    bool     valid;
};

// Device configuration
struct DeviceConfig {
    char     device_id[16];  // "IFMS-XXYYZZ"
    char     device_secret[65];
    bool     valid;
};

namespace NVSManager {

    /**
     * Initialize NVS flash. Call once at boot.
     * @return true if NVS initialized successfully
     */
    bool init();

    /**
     * Check if NVS contains valid WiFi credentials.
     * Used for conditional boot logic.
     */
    bool hasValidWiFiConfig();

    // --- WiFi Config ---
    bool loadWiFiConfig(WiFiConfig &cfg);
    bool saveWiFiConfig(const WiFiConfig &cfg);
    bool clearWiFiConfig();

    // --- Sensor Config ---
    bool loadSensorConfig(SensorConfig &cfg);
    bool saveSensorConfig(const SensorConfig &cfg);

    // --- Threshold Config ---
    bool loadThresholdConfig(ThresholdConfig &cfg);
    bool saveThresholdConfig(const ThresholdConfig &cfg);

    // --- Backend Config ---
    bool loadBackendConfig(BackendConfig &cfg);
    bool saveBackendConfig(const BackendConfig &cfg);

    // --- Auth Config ---
    bool loadAuthConfig(AuthConfig &cfg);
    bool saveAuthConfig(const AuthConfig &cfg);

    // --- Device Config ---
    bool loadDeviceConfig(DeviceConfig &cfg);
    bool saveDeviceConfig(const DeviceConfig &cfg);

    // --- Camera Config ---
    bool loadCameraConfig(CameraConfig &cfg);
    bool saveCameraConfig(const CameraConfig &cfg);

    /**
     * Factory reset — erase all NVS namespaces.
     */
    bool factoryReset();

    /**
     * Compute CRC32 for data verification.
     */
    uint32_t computeCRC32(const void *data, size_t length);
}
