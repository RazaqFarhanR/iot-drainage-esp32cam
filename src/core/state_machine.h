#pragma once

#include <cstdint>

/**
 * @file state_machine.h
 * @brief System mode state machine with transition logic (§1.2, §1.3).
 *
 * Modes:
 *  - COMMISSIONING: AP + WebSocket + Captive Portal
 *  - OPERATIONAL:   STA + Deep Sleep cycles
 *  - MAINTENANCE:   STA + diagnostics (3 min timeout)
 *  - OFFLINE:       Deep Sleep + exponential backoff retry
 */

enum class SystemMode : uint8_t {
    COMMISSIONING = 0,
    OPERATIONAL   = 1,
    MAINTENANCE   = 2,
    OFFLINE       = 3
};

// RTC Memory structure — persists across deep sleep
struct RTCData {
    // Smoothing state (§4.1)
    float    lastDistance;
    bool     hasLastDistance;

    // Offline retry counter (§11.1)
    uint16_t offlineRetryCount;

    // WiFi fail counter (§5)
    uint8_t  wifiFailCount;

    // Fault escalation (§11.3)
    uint8_t  faultCounter;

    // Stuck detection (§3.5)
    float    lastMeasurements[5];
    uint8_t  stuckCounter;

    // Rain sensor EXT0 cooldown (§11.4)
    uint8_t  ext0WakeCount;

    // Pending upload (§11.5)
    bool     pendingUpload;
    uint8_t  pendingUploadRetries;

    // NTP time estimation (§11.8)
    uint32_t ntpEpochBase;
    uint32_t ntpMillisBase;
    bool     ntpSynced;

    // Baseline drift (§11.9)
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
     * Get sleep duration based on offline retry count (§11.1).
     */
    uint64_t getOfflineBackoffSeconds();

    /**
     * Log current mode to serial.
     */
    void logState();
}
