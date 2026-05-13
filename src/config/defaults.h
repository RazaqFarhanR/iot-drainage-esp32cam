#pragma once
#include <cstdint>

/**
 * @file defaults.h
 * @brief System-wide constants, defaults, and timeout values per SRS.
 */

// ==============================================
// NVS Namespace Names
// ==============================================
#define NVS_NS_WIFI          "wifi_cfg"
#define NVS_NS_SENSOR        "sensor_cfg"
#define NVS_NS_THRESHOLD     "threshold_cfg"
#define NVS_NS_AUTH          "auth_cfg"
#define NVS_NS_DEVICE        "device_cfg"
#define NVS_NS_STAGING       "cfg_staging"

// ==============================================
// Default Threshold Values (cm)
// ==============================================
constexpr float DEFAULT_THRESHOLD_NORMAL   = 40.0f;
constexpr float DEFAULT_THRESHOLD_BAHAYA   = 80.0f;
constexpr float DEFAULT_HEIGHT_SENSOR      = 150.0f;
constexpr float DEFAULT_OFFSET             = 0.0f;

// ==============================================
// Sensor Sampling Parameters
// ==============================================
constexpr int   OPERATIONAL_SAMPLE_COUNT   = 15;    // §4.1 — samples per wake cycle
constexpr int   COMMISSIONING_SAMPLE_COUNT = 5;     // §2.3 — lighter sampling
constexpr int   DIAGNOSTIC_SAMPLE_COUNT    = 30;    // §3.2 — 2x operational
constexpr int   SAMPLE_DELAY_MS            = 50;    // §4.1 — min delay between pings
constexpr float SENSOR_MIN_CM              = 0.0f;  // Values ≤ 0 are artefacts
constexpr float SENSOR_MAX_CM              = 500.0f;// Values > 500 are artefacts
constexpr float SMOOTHING_ALPHA            = 0.4f;  // §4.1 — D_final = α·median + (1-α)·D_last

// ==============================================
// Self-Check Thresholds
// ==============================================
constexpr float VARIANCE_UNSTABLE_LIMIT    = 50.0f; // §3.5 — cm² threshold for UNSTABLE
constexpr float SPIKE_THRESHOLD_CM         = 30.0f; // §3.5 — Δcm for SPIKE_DETECTED
constexpr int   STUCK_CYCLE_COUNT          = 5;     // §3.5 — cycles for SENSOR_STUCK
constexpr float BASELINE_DRIFT_CM          = 40.0f; // §11.9 — drift threshold
constexpr int   BASELINE_DRIFT_CYCLES      = 3;     // §11.9 — consecutive cycles

// ==============================================
// Progressive Fault Escalation (§11.3)
// ==============================================
constexpr int   FAULT_ESCALATION_ALERT     = 2;     // 2-3x → send SENSOR_UNSTABLE
constexpr int   FAULT_ESCALATION_BAHAYA    = 4;     // >3x → force BAHAYA
constexpr int   FAULT_ESCALATION_SLEEP     = 10;    // >10x → sleep 60 min

// ==============================================
// Sleep Intervals (seconds) per Status (§6.1)
// ==============================================
constexpr uint64_t SLEEP_NORMAL_SEC        = 3600;  // 60 minutes
constexpr uint64_t SLEEP_WASPADA_SEC       = 600;   // 10 minutes
constexpr uint64_t SLEEP_BAHAYA_SEC        = 120;   // 2 minutes
constexpr uint64_t SLEEP_FAULT_SEC         = 3600;  // 60 minutes (§11.3 >10x)

// ==============================================
// Offline Mode Backoff (§11.1) (seconds)
// ==============================================
constexpr uint64_t BACKOFF_TIER1_SEC       = 300;   // Retry 1-3: 5 min
constexpr uint64_t BACKOFF_TIER2_SEC       = 900;   // Retry 4-6: 15 min
constexpr uint64_t BACKOFF_TIER3_SEC       = 3600;  // Retry 7-10: 60 min
constexpr uint64_t BACKOFF_TIER4_SEC       = 21600; // Retry >10: 6 hours

// ==============================================
// Mode Timeouts (seconds)
// ==============================================
constexpr unsigned long COMMISSIONING_TIMEOUT_MS  = 600000;   // 10 minutes (§1.3)
constexpr unsigned long COMMISSIONING_IDLE_MS     = 120000;   // 2 min no WS client (§2.1)
constexpr unsigned long MAINTENANCE_TIMEOUT_MS    = 180000;   // 3 minutes (§3.1)
constexpr int           WIFI_STATIC_TIMEOUT_SEC   = 10;       // §5 — static IP timeout
constexpr int           WIFI_MAX_FAIL_COUNT       = 3;        // §5 — auto-recovery

// ==============================================
// HTTP Per-Phase Timeouts (ms) (§11.12)
// ==============================================
constexpr int HTTP_CONNECT_TIMEOUT_MS      = 5000;
constexpr int HTTP_HEADER_TIMEOUT_MS       = 3000;
constexpr int HTTP_UPLOAD_PER_KB_MS        = 2000;
constexpr int HTTP_RESPONSE_TIMEOUT_MS     = 5000;
constexpr int HTTP_TOTAL_TIMEOUT_MS        = 30000;

// ==============================================
// Watchdog Timeouts (seconds) (§11.13)
// ==============================================
constexpr int WDT_BOOT_SEC                 = 30;
constexpr int WDT_WIFI_SEC                 = 15;
constexpr int WDT_SENSOR_SEC               = 10;
constexpr int WDT_UPLOAD_SEC               = 45;
constexpr int WDT_SLEEP_SEC                = 5;

// ==============================================
// MQTT Configuration
// ==============================================
#define MQTT_BROKER_DEFAULT   "broker.emqx.io"
#define MQTT_PORT_DEFAULT     1883
#define MQTT_QOS              1

// ==============================================
// Commissioning Mode
// ==============================================
constexpr int   COMMISSIONING_CPU_MHZ      = 80;    // §2.1 — thermal mitigation
constexpr int   WS_TELEMETRY_INTERVAL_MS   = 1000;  // §2.3 — 1 second
constexpr int   WS_CAMERA_INTERVAL_MS      = 5000;  // §2.3 — 5 seconds
constexpr int   AP_LED_BLINK_MS            = 200;   // §2.3 — fast blink

// ==============================================
// Rain Sensor EXT0 Cooldown (§11.4)
// ==============================================
constexpr int   EXT0_COOLDOWN_THRESHOLD    = 5;
constexpr uint64_t EXT0_COOLDOWN_SLEEP_SEC = 300;   // 5 minutes

// ==============================================
// Photo Upload Retry (§11.5)
// ==============================================
constexpr int   MAX_UPLOAD_RETRY_CYCLES    = 5;

// ==============================================
// NTP Configuration (§11.8)
// ==============================================
#define NTP_SERVER            "pool.ntp.org"
constexpr int   NTP_TIMEOUT_SEC            = 5;

// ==============================================
// Web UI Security (§11.7)
// ==============================================
constexpr int   PIN_LENGTH                 = 4;
constexpr int   PIN_MAX_ATTEMPTS           = 3;
constexpr unsigned long PIN_LOCKOUT_MS     = 300000; // 5 minutes

// ==============================================
// Camera Multi-Frame (§11.11)
// ==============================================
constexpr int   CAMERA_DUMMY_FRAMES        = 3;

// ==============================================
// LED Patterns (§4.2)
// ==============================================
constexpr int   LED_SUCCESS_BLINK_MS       = 500;   // 1x blink
constexpr int   LED_FAIL_BLINK_MS          = 100;   // 3x fast blink
constexpr int   LED_FAIL_BLINK_COUNT       = 3;

// ==============================================
// Backend Defaults
// ==============================================
constexpr int   DEFAULT_BACKEND_PORT       = 3000;

// ==============================================
// Token Verification (§11.6)
// ==============================================
constexpr int   TOKEN_EXPIRY_SEC           = 60;

// ==============================================
// Daily Snapshot (§3.4)
// ==============================================
constexpr int   DAILY_SNAPSHOT_HOUR        = 7;     // 07:00 AM
constexpr int   DAILY_SNAPSHOT_MINUTE      = 0;

// ==============================================
// Double Reset Detection
// ==============================================
constexpr unsigned long DOUBLE_RESET_WINDOW_MS = 3000; // §1.3 — 2x press < 3 sec
