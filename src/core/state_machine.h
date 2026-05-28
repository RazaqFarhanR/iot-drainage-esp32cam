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

    // Mode request from MQTT
    bool     maintenanceRequested;

    // Upload failure tracking
    bool     lastUploadFailed;

    // Double reset detection
    uint32_t lastResetTime;
    uint8_t  resetCount;

    // Validation marker
    uint32_t magic;
};

#define RTC_DATA_MAGIC 0xIFMS2026

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
     * Check if factory reset was triggered (long press >10s).
     * Must be called early in boot.
     */
    bool checkFactoryReset();

    /**
     * Check for double reset (2x press < 3s).
     */
    bool checkDoubleReset();

    /**
     * Access RTC data (persists across deep sleep).
     */
    RTCData& getRTCData();

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
