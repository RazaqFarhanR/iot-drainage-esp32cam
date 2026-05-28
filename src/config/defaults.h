#pragma once
#include <cstdint>
#include "../env.h"

// NVS Namespace Names
#define NVS_NS_WIFI          "wifi_cfg"
#define NVS_NS_SENSOR        "sensor_cfg"
#define NVS_NS_THRESHOLD     "threshold_cfg"
#define NVS_NS_AUTH          "auth_cfg"
#define NVS_NS_DEVICE        "device_cfg"
#define NVS_NS_STAGING       "cfg_staging"

// Default Threshold Values (cm)
constexpr float DEFAULT_THRESHOLD_NORMAL   = 40.0f;
constexpr float DEFAULT_THRESHOLD_BAHAYA   = 80.0f;
constexpr float DEFAULT_HEIGHT_SENSOR      = 150.0f;
constexpr float DEFAULT_OFFSET             = 0.0f;

// Sensor Sampling Parameters
constexpr int   OPERATIONAL_SAMPLE_COUNT   = 15;    // — samples per wake cycle
constexpr int   COMMISSIONING_SAMPLE_COUNT = 5;     // — lighter sampling
constexpr int   DIAGNOSTIC_SAMPLE_COUNT    = 30;    // — 2x operational
constexpr int   SAMPLE_DELAY_MS            = 50;    // — min delay between pings
constexpr float SENSOR_MIN_CM              = 0.0f;  // Values ≤ 0 are artefacts
constexpr float SENSOR_MAX_CM              = 500.0f;// Values > 500 are artefacts
constexpr float SMOOTHING_ALPHA            = 0.4f;  // — D_final = α·median + (1-α)·D_last

// Self-Check Thresholds
constexpr float VARIANCE_UNSTABLE_LIMIT    = 50.0f; // — cm² threshold for UNSTABLE
constexpr float SPIKE_THRESHOLD_CM         = 30.0f; // — Δcm for SPIKE_DETECTED
constexpr int   STUCK_CYCLE_COUNT          = 5;     // — cycles for SENSOR_STUCK
constexpr float BASELINE_DRIFT_CM          = 40.0f; // — drift threshold
constexpr int   BASELINE_DRIFT_CYCLES      = 3;     // — consecutive cycles

// Progressive Fault Escalation
constexpr int   FAULT_ESCALATION_ALERT     = 2;     // 2-3x → send SENSOR_UNSTABLE
constexpr int   FAULT_ESCALATION_BAHAYA    = 4;     // >3x → force BAHAYA
constexpr int   FAULT_ESCALATION_SLEEP     = 10;    // >10x → sleep 60 min

// Sleep Intervals (seconds) per Status
constexpr uint64_t SLEEP_NORMAL_SEC        = 3600;  // 60 minutes
constexpr uint64_t SLEEP_WASPADA_SEC       = 600;   // 10 minutes
constexpr uint64_t SLEEP_BAHAYA_SEC        = 120;   // 2 minutes
constexpr uint64_t SLEEP_FAULT_SEC         = 3600;  // 60 minutes ( >10x)

// Offline Mode Backoff (seconds)
constexpr uint64_t BACKOFF_TIER1_SEC       = 300;   // Retry 1-3: 5 min
constexpr uint64_t BACKOFF_TIER2_SEC       = 900;   // Retry 4-6: 15 min
constexpr uint64_t BACKOFF_TIER3_SEC       = 3600;  // Retry 7-10: 60 min
constexpr uint64_t BACKOFF_TIER4_SEC       = 21600; // Retry >10: 6 hours

// Mode Timeouts (seconds)
constexpr unsigned long COMMISSIONING_TIMEOUT_MS  = 600000;   // 10 minutes
constexpr unsigned long COMMISSIONING_IDLE_MS     = 120000;   // 2 min no WS client
constexpr unsigned long MAINTENANCE_TIMEOUT_MS    = 180000;   // 3 minutes
constexpr int           WIFI_STATIC_TIMEOUT_SEC   = 10;       // — static IP timeout
constexpr int           WIFI_MAX_FAIL_COUNT       = 3;        // — auto-recovery

// HTTP Per-Phase Timeouts (ms)
constexpr int HTTP_CONNECT_TIMEOUT_MS      = 5000;
constexpr int HTTP_HEADER_TIMEOUT_MS       = 3000;
constexpr int HTTP_UPLOAD_PER_KB_MS        = 2000;
constexpr int HTTP_RESPONSE_TIMEOUT_MS     = 5000;
constexpr int HTTP_TOTAL_TIMEOUT_MS        = 30000;

// Watchdog Timeouts (seconds)
constexpr int WDT_BOOT_SEC                 = 30;
constexpr int WDT_WIFI_SEC                 = 15;
constexpr int WDT_SENSOR_SEC               = 10;
constexpr int WDT_UPLOAD_SEC               = 45;
constexpr int WDT_SLEEP_SEC                = 5;

// MQTT Configuration (Moved to env.h)

// Commissioning Mode
constexpr int   COMMISSIONING_CPU_MHZ      = 80;    // — thermal mitigation
constexpr int   WS_TELEMETRY_INTERVAL_MS   = 1000;  // — 1 second
constexpr int   WS_CAMERA_INTERVAL_MS      = 10000; // Reduced to 10s to prevent overheating
constexpr int   AP_LED_BLINK_MS            = 200;   // — fast blink

// Rain Sensor EXT0 Cooldown
constexpr int   EXT0_COOLDOWN_THRESHOLD    = 5;
constexpr uint64_t EXT0_COOLDOWN_SLEEP_SEC = 300;   // 5 minutes

// Photo Upload Retry
constexpr int   MAX_UPLOAD_RETRY_CYCLES    = 5;

// NTP Configuration (URL moved to env.h)
constexpr int   NTP_TIMEOUT_SEC            = 5;

// Web UI Security
constexpr int   PIN_LENGTH                 = 4;
constexpr int   PIN_MAX_ATTEMPTS           = 3;
constexpr unsigned long PIN_LOCKOUT_MS     = 300000; // 5 minutes

// Camera Multi-Frame
constexpr int   CAMERA_DUMMY_FRAMES        = 3;

// LED Patterns
constexpr int   LED_SUCCESS_BLINK_MS       = 500;   // 1x blink
constexpr int   LED_FAIL_BLINK_MS          = 100;   // 3x fast blink
constexpr int   LED_FAIL_BLINK_COUNT       = 3;

// Backend Defaults (Port moved to env.h)

// Token Verification
constexpr int   TOKEN_EXPIRY_SEC           = 60;

// Daily Snapshot
constexpr int   DAILY_SNAPSHOT_HOUR        = 7;     // 07:00 AM
constexpr int   DAILY_SNAPSHOT_MINUTE      = 0;

// Double Reset Detection
constexpr unsigned long DOUBLE_RESET_WINDOW_MS = 3000; // — 2x press < 3 sec
