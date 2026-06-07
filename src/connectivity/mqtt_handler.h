#pragma once

#include <ArduinoJson.h>



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
    bool publishTelemetry(float rawDistance, float waterLevel, const char *status, bool rainDetected, const char *sensorFlag, uint64_t nextWakeupSec);

    /**
     * Publish system log event.
     */
    bool publishLog(const char *level, const char *message);

    /**
     * Publish device info (IP Address, etc) on boot.
     */
    bool publishDeviceInfo();

    /**
     * Disconnect from broker.
     */
    void disconnect();
}
