#pragma once

#include <ArduinoJson.h>

/**
 * @file mqtt_handler.h
 * @brief MQTT pub/sub with token verification (§9.1, §11.6).
 *
 * Topics:
 *  - Publish:   ifms/{device_id}/telemetry
 *  - Publish:   ifms/{device_id}/diagnostic
 *  - Subscribe: ifms/{device_id}/cmd
 */

// Callback type for incoming MQTT commands
typedef void (*MQTTCommandCallback)(const char *cmd, JsonDocument &doc);

namespace MQTTHandler {
    /**
     * Initialize MQTT client and connect to broker.
     * @return true if connected
     */
    bool connect();

    /**
     * Process MQTT loop. Call frequently.
     */
    void loop();

    /**
     * Check if connected to broker.
     */
    bool isConnected();

    /**
     * Publish telemetry data (§9.1).
     */
    bool publishTelemetry(float waterLevel, float rawDistance,
                          const char *status, const char *sensorFlag,
                          bool rainDetected, int rssi, bool timeSynced,
                          bool lastUploadFailed);

    /**
     * Publish diagnostic results (§3.2).
     */
    bool publishDiagnostic(int sampleCount, float median,
                           float variance, float minVal, float maxVal,
                           const char *sensorStatus);

    /**
     * Set callback for incoming commands.
     */
    void setCommandCallback(MQTTCommandCallback callback);

    /**
     * Disconnect from broker.
     */
    void disconnect();
}
