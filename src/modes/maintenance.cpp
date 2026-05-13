#include "maintenance.h"
#include "../core/state_machine.h"
#include "../core/watchdog.h"
#include "../config/defaults.h"
#include "../sensor/ultrasonic.h"
#include "../camera/camera_handler.h"
#include "../connectivity/wifi_manager.h"
#include "../connectivity/mqtt_handler.h"
#include "../connectivity/http_client.h"
#include <ArduinoJson.h>
#include <Arduino.h>

/**
 * @file maintenance.cpp
 * @brief Maintenance Mode (§3) — remote diagnostics.
 *
 * - Activated via MQTT command or SENSOR_STUCK
 * - 30-sample sensor diagnostic
 * - On-demand camera snapshot
 * - 3-minute timeout
 */

static bool snapshotRequested = false;
static bool sensorDiagRequested = false;

static void onMaintenanceCommand(const char *cmd, JsonDocument &doc) {
    if (strcmp(cmd, "snapshot") == 0) {
        snapshotRequested = true;
        Serial.println("[MAINT] Snapshot requested");
    } else if (strcmp(cmd, "diagnostic") == 0) {
        sensorDiagRequested = true;
        Serial.println("[MAINT] Sensor diagnostic requested");
    } else if (strcmp(cmd, "enter_maintenance") == 0) {
        // Already in maintenance — ignore
    }
}

// ============================================
// Sensor Diagnostic (§3.2)
// ============================================

static void runSensorDiagnostic() {
    Serial.println("[MAINT] Running sensor diagnostic (30 samples)...");
    Watchdog::setPhase(WDTPhase::SENSOR);

    Ultrasonic::init();
    MeasurementResult result = Ultrasonic::measure(DIAGNOSTIC_SAMPLE_COUNT, false);

    // Determine sensor status (§3.2)
    const char *sensorStatus = "OK";
    if (!result.valid || result.median <= 0 || result.median > SENSOR_MAX_CM) {
        sensorStatus = "FAULT";
    } else if (result.variance > VARIANCE_UNSTABLE_LIMIT) {
        sensorStatus = "UNSTABLE";
    }

    // Publish diagnostic (§3.2)
    MQTTHandler::publishDiagnostic(
        result.totalCount,
        result.median,
        result.variance,
        result.minVal,
        result.maxVal,
        sensorStatus
    );

    Serial.printf("[MAINT] Diagnostic: %s (median=%.1f, var=%.1f, range=%.1f-%.1f)\n",
                  sensorStatus, result.median, result.variance,
                  result.minVal, result.maxVal);
}

// ============================================
// On-Demand Snapshot (§3.3)
// ============================================

static void takeSnapshot() {
    Serial.println("[MAINT] Taking on-demand snapshot...");
    Watchdog::setPhase(WDTPhase::UPLOAD);

    if (Camera::init()) {
        camera_fb_t *fb = Camera::captureBestPhoto();
        if (fb) {
            if (HTTPClient_::uploadSnapshot(fb)) {
                Serial.println("[MAINT] Snapshot uploaded successfully");
            } else {
                Serial.println("[MAINT] Snapshot upload failed");
            }
            Camera::returnFrame(fb);
        }
        Camera::deinit();
    } else {
        Serial.println("[MAINT] Camera init failed");
    }
}

// ============================================
// Main Maintenance Loop
// ============================================

void MaintenanceMode::run() {
    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║    MAINTENANCE MODE                  ║");
    Serial.println("╚══════════════════════════════════════╝\n");

    snapshotRequested = false;
    sensorDiagRequested = false;

    // Connect WiFi
    Watchdog::setPhase(WDTPhase::WIFI);
    if (!WiFiMgr::connect()) {
        Serial.println("[MAINT] WiFi failed — exiting");
        StateMachine::requestMode(SystemMode::OPERATIONAL);
        return;
    }

    // Connect MQTT and subscribe
    if (!MQTTHandler::connect()) {
        Serial.println("[MAINT] MQTT failed — exiting");
        WiFiMgr::disconnect();
        StateMachine::requestMode(SystemMode::OPERATIONAL);
        return;
    }

    MQTTHandler::setCommandCallback(onMaintenanceCommand);

    // Run initial sensor diagnostic automatically
    runSensorDiagnostic();

    // Wait for commands — 3 minute timeout (§3.1)
    unsigned long startTime = millis();
    Serial.println("[MAINT] Listening for commands (3 min timeout)...");

    while ((millis() - startTime) < MAINTENANCE_TIMEOUT_MS) {
        Watchdog::feed();
        MQTTHandler::loop();

        if (snapshotRequested) {
            takeSnapshot();
            snapshotRequested = false;
        }

        if (sensorDiagRequested) {
            runSensorDiagnostic();
            sensorDiagRequested = false;
        }

        delay(100);
    }

    // Cleanup
    Serial.println("[MAINT] Timeout — returning to Operational");
    MQTTHandler::disconnect();
    WiFiMgr::disconnect();

    StateMachine::requestMode(SystemMode::OPERATIONAL);
}
