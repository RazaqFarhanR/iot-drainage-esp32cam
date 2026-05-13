#include "commissioning.h"
#include "../core/state_machine.h"
#include "../core/watchdog.h"
#include "../core/device_id.h"
#include "../config/defaults.h"
#include "../config/pins.h"
#include "../sensor/ultrasonic.h"
#include "../sensor/rain_sensor.h"
#include "../camera/camera_handler.h"
#include "../connectivity/wifi_manager.h"
#include "../web/web_ui.h"
#include <Arduino.h>

/**
 * @file commissioning.cpp
 * @brief Commissioning Mode implementation (§2).
 *
 * - CPU clock 80MHz for thermal mitigation
 * - AP mode with captive portal
 * - WebSocket telemetry every 1s
 * - Camera frame every 5s (on-demand)
 * - LED blink 200ms
 * - Timeout: 2 min idle / 10 min max
 */

void CommissioningMode::run() {
    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║    COMMISSIONING MODE                ║");
    Serial.println("╚══════════════════════════════════════╝\n");

    // §2.1: CPU throttle for thermal mitigation
    setCpuFrequencyMhz(COMMISSIONING_CPU_MHZ);
    Serial.printf("[COMM] CPU throttled to %d MHz\n", COMMISSIONING_CPU_MHZ);

    // Initialize subsystems
    Ultrasonic::init();
    RainSensor::init();
    WiFiMgr::startAP();
    WebUI::start();

    // LED setup — fast blink indicates AP mode (§2.3)
    pinMode(PIN_STATUS_LED, OUTPUT);

    unsigned long startTime = millis();
    unsigned long lastClientSeen = 0;
    unsigned long lastTelemetry = 0;
    unsigned long lastCamera = 0;
    unsigned long lastLEDBlink = 0;
    bool ledState = false;
    bool hadClientEver = false;

    Serial.println("[COMM] Waiting for clients...");
    Serial.printf("[COMM] PIN: %s\n", WebUI::getPIN());

    while (true) {
        Watchdog::feed();
        WebUI::loop();

        unsigned long now = millis();
        unsigned long elapsed = now - startTime;

        // --- LED Blink (200ms) ---
        if (now - lastLEDBlink >= AP_LED_BLINK_MS) {
            ledState = !ledState;
            digitalWrite(PIN_STATUS_LED, ledState ? LOW : HIGH);  // Inverted on ESP32-CAM
            lastLEDBlink = now;
        }

        // --- Check if config was saved ---
        if (WebUI::isConfigSaved()) {
            Serial.println("[COMM] Config saved — rebooting to Operational...");
            delay(1000);
            WebUI::stop();
            WiFiMgr::stopAP();
            ESP.restart();
        }

        // --- Track client presence ---
        if (WebUI::hasClients()) {
            lastClientSeen = now;
            hadClientEver = true;
        }

        // --- Timeout: 2 min no client → exit (§2.1) ---
        if (!hadClientEver && elapsed > COMMISSIONING_IDLE_MS) {
            Serial.println("[COMM] No client connected in 2 min — exiting");
            break;
        }

        // --- Timeout: 10 min max (§1.3) ---
        if (elapsed > COMMISSIONING_TIMEOUT_MS) {
            Serial.println("[COMM] 10 min timeout — exiting");
            break;
        }

        // --- Send telemetry every 1s (§2.3) ---
        if (WebUI::hasClients() && (now - lastTelemetry >= WS_TELEMETRY_INTERVAL_MS)) {
            // Quick measurement (5 samples, no smoothing)
            MeasurementResult m = Ultrasonic::measure(COMMISSIONING_SAMPLE_COUNT, false);
            bool rain = RainSensor::isRaining();
            if (m.valid) {
                WebUI::sendTelemetry(m.median, m.waterLevel, 0, rain);
            }
            lastTelemetry = now;
        }

        // --- Send camera frame every 5s (§2.3) — on-demand ---
        if (WebUI::hasClients() && (now - lastCamera >= WS_CAMERA_INTERVAL_MS)) {
            if (!Camera::isInitialized()) Camera::init();
            camera_fb_t *fb = Camera::captureFrame();
            if (fb) {
                WebUI::sendCameraFrame(fb->buf, fb->len);
                Camera::returnFrame(fb);
            }
            lastCamera = now;
        }

        delay(50);
    }

    // Cleanup
    digitalWrite(PIN_STATUS_LED, HIGH);  // OFF
    WebUI::stop();
    WiFiMgr::stopAP();
    Camera::deinit();

    // Restore CPU clock
    setCpuFrequencyMhz(240);

    // Transition to operational or sleep
    Serial.println("[COMM] Transitioning to Operational mode...");
    StateMachine::requestMode(SystemMode::OPERATIONAL);
}
