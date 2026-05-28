#pragma once

#include <cstdint>
#include <cstddef>

namespace WebUI {
    /**
     * Initialize and start web server + WebSocket server.
     * Web UI at port 80, WebSocket at port 81.
     */
    void start();

    /**
     * Stop web server and WebSocket.
     */
    void stop();

    /**
     * Process async events. Call in loop.
     */
    void loop();

    /**
     * Send telemetry JSON to all connected WebSocket clients.
     */
    void sendTelemetry(float distCm, float waterLevelCm, int rssi, bool rain);

    /**
     * Send camera frame to all connected WebSocket clients.
     */
    void sendCameraFrame(const uint8_t *data, size_t len);

    /**
     * Check if any WebSocket client is connected.
     */
    bool hasClients();

    /**
     * Check if configuration was saved (user clicked Save & Reboot).
     */
    bool isConfigSaved();

    /**
     * Check if a camera preview was requested.
     */
    bool isPreviewRequested();

    /**
     * Clear the camera preview request flag.
     */
    void clearPreviewRequest();

    /**
     * Get the generated PIN for serial display.
     */
    const char* getPIN();
}
