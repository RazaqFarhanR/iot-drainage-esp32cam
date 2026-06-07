#pragma once

#include <cstdint>

enum class SystemMode : uint8_t {
    COMMISSIONING = 0,
    OPERATIONAL   = 1,
    MAINTENANCE   = 2,
    OFFLINE       = 3
};

// RTC Memory structure — persists across deep sleep
struct RTCData {
    // Smoothing state
    float    lastDistance;
    bool     hasLastDistance;
    float    baselineAvg;

    // Offline retry counter
    uint16_t offlineRetryCount;

    // WiFi fail counter
    uint8_t  wifiFailCount;

    // Fault escalation
    uint8_t  faultCounter;

    // Stuck detection
    float    lastMeasurements[5];
    uint8_t  stuckCounter;

    // Rain sensor EXT0 cooldown
    uint8_t  ext0WakeCount;

    // Pending upload
    bool     pendingUpload;
    uint8_t  pendingUploadRetries;

    // NTP time estimation
    uint32_t ntpEpochBase;
    uint32_t ntpMillisBase;
    bool     ntpSynced;

    // Baseline drift
    uint8_t  driftCounter;

    // Daily snapshot tracking
    uint8_t  lastSnapshotDay;



    // Upload failure tracking
    bool     lastUploadFailed;

    // Maintenance mode flag
    bool     maintenanceRequested;

    // Double reset detection
    uint32_t lastResetTime;
    uint8_t  resetCount;

    // Pending log to flush via MQTT
    char     pendingLog[128];
    char     pendingLogLevel[16];

    // Validation marker
    uint32_t magic;
};

#define RTC_DATA_MAGIC 0x107D2026

namespace StateMachine {

    /**
     * Initialize state machine. Determines boot mode.
     * Must be called after NVSManager::init().
     */
    void init();

    /**
     * Get the current system mode.
     */
    SystemMode getCurrentMode();

    /**
     * Request transition to a specific mode.
     */
    void requestMode(SystemMode mode);

    /**
     * Detect button clicks during boot window (2.5s).
     * Returns the number of clicks detected.
     */
    int detectBootClicks();

    /**
     * Local Commissioning state accessors.
     */
    bool isLocalCommissioning();
    void setLocalCommissioning(bool state);

    /**
     * Check for double reset (2x press < 3s).
     */
    bool checkDoubleReset();

    /**
     * Access RTC data (persists across deep sleep).
     */
    RTCData& getRTCData();

    /**
     * Buffer a log message with priority overwrite (ERROR > WARNING > INFO).
     */
    void bufferLog(const char* level, const char* message);

    /**
     * Validate and initialize RTC data if corrupt/first boot.
     */
    void validateRTCData();

    /**
     * Enter deep sleep with specified duration.
     * Handles EXT0 rain wake configuration.
     */
    void enterDeepSleep(uint64_t sleepSeconds);

    /**
     * Get sleep duration based on offline retry count.
     */
    uint64_t getOfflineBackoffSeconds();

    /**
     * Log current mode to serial.
     */
    void logState();
}
