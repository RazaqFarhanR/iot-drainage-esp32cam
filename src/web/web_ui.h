#pragma once

#include <cstdint>
#include <cstddef>

/**
 * @file web_ui.h
 * @brief Captive portal Web UI with PIN auth (§2.4, §11.7).
 *
 * Serves configuration form at http://192.168.4.1
 * Protected by 4-digit PIN authentication.
 */

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
     * Get the generated PIN for serial display.
     */
    const char* getPIN();
}
