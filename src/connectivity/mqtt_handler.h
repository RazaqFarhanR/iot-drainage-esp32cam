#pragma once

#include <ArduinoJson.h>

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
     * Publish telemetry data.
     */
    bool publishTelemetry(float waterLevel, float rawDistance,
                          const char *status, const char *sensorFlag,
                          bool rainDetected, int rssi, bool timeSynced,
                          bool lastUploadFailed);

    /**
     * Publish diagnostic results.
     */
    bool publishDiagnostic(int sampleCount, float median,
                           float variance, float minVal, float maxVal,
                           const char *sensorStatus);

    /**
     * Publish response to a command.
     */
    bool publishResponse(const char *cmd, const char *msg_id, const char *status, int code, const char *message);

    /**
     * Publish system log event.
     */
    bool publishLog(const char *level, const char *message);

    /**
     * Set callback for incoming commands.
     */
    void setCommandCallback(MQTTCommandCallback callback);

    /**
     * Disconnect from broker.
     */
    void disconnect();
}
