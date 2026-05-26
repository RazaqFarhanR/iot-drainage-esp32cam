#include "camera_handler.h"
#include "../config/pins.h"
#include "../config/defaults.h"
#include "../core/watchdog.h"
#include <Arduino.h>

/**
 * @file camera_handler.cpp
 * @brief Camera implementation with multi-frame exposure (§11.11).
 *
 * Multi-frame process:
 *  1. Capture 3 dummy frames (warmup, discard)
 *  2. Capture frame with flash OFF
 *  3. Capture frame with flash ON
 *  4. Compare average brightness
 *  5. Return the brighter frame
 */

static bool cameraReady = false;

// ============================================
// Brightness Analysis
// ============================================

static float calculateBrightness(camera_fb_t *fb) {
    if (!fb || fb->len == 0) return 0.0f;

    // Sample every 100th byte for speed
    uint32_t sum = 0;
    int count = 0;
    for (size_t i = 0; i < fb->len; i += 100) {
        sum += fb->buf[i];
        count++;
    }
    return (count > 0) ? ((float)sum / count) : 0.0f;
}

// ============================================
// Public API
// ============================================

bool Camera::init() {
    if (cameraReady) return true;

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = CAM_PIN_D0;
    config.pin_d1 = CAM_PIN_D1;
    config.pin_d2 = CAM_PIN_D2;
    config.pin_d3 = CAM_PIN_D3;
    config.pin_d4 = CAM_PIN_D4;
    config.pin_d5 = CAM_PIN_D5;
    config.pin_d6 = CAM_PIN_D6;
    config.pin_d7 = CAM_PIN_D7;
    config.pin_xclk = CAM_PIN_XCLK;
    config.pin_pclk = CAM_PIN_PCLK;
    config.pin_vsync = CAM_PIN_VSYNC;
    config.pin_href = CAM_PIN_HREF;
    config.pin_sccb_sda = CAM_PIN_SIOD;
    config.pin_sccb_scl = CAM_PIN_SIOC;
    config.pin_pwdn = CAM_PIN_PWDN;
    config.pin_reset = CAM_PIN_RESET;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;

    // §10: PSRAM detection — UXGA if available, SVGA fallback
    if (psramFound()) {
        config.frame_size = FRAMESIZE_UXGA;
        config.jpeg_quality = 8;
        config.fb_count = 2;
        Serial.println("[Camera] PSRAM found → UXGA");
    } else {
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 10;
        config.fb_count = 1;
        Serial.println("[Camera] No PSRAM → SVGA fallback");
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("[Camera] Init failed: 0x%x\n", err);
        return false;
    }

    // Configure sensor settings
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_brightness(s, 0);
        s->set_contrast(s, 1);
        s->set_whitebal(s, 1);
        s->set_awb_gain(s, 1);
        s->set_exposure_ctrl(s, 1);
        s->set_hmirror(s, 0);
        s->set_vflip(s, 1);
    }

    cameraReady = true;
    Serial.println("[Camera] Initialized OK");
    return true;
}

void Camera::deinit() {
    if (cameraReady) {
        esp_camera_deinit();
        cameraReady = false;
        Serial.println("[Camera] Deinitialized");
    }
}

camera_fb_t* Camera::captureBestPhoto() {
    if (!cameraReady && !init()) return nullptr;

    Watchdog::feed();

    // Step 1: Warmup — capture and discard dummy frames (§11.11)
    for (int i = 0; i < CAMERA_DUMMY_FRAMES; i++) {
        camera_fb_t *dummy = esp_camera_fb_get();
        if (dummy) esp_camera_fb_return(dummy);
        delay(50);
    }

    // Step 2: Capture with flash OFF
    pinMode(PIN_FLASH_LED, OUTPUT);
    digitalWrite(PIN_FLASH_LED, LOW);
    delay(100);
    camera_fb_t *frameOff = esp_camera_fb_get();
    float brightnessOff = calculateBrightness(frameOff);

    Watchdog::feed();

    // Step 3: Capture with flash ON
    digitalWrite(PIN_FLASH_LED, HIGH);
    delay(200);  // Let exposure adjust
    camera_fb_t *frameOn = esp_camera_fb_get();
    float brightnessOn = calculateBrightness(frameOn);
    digitalWrite(PIN_FLASH_LED, LOW);

    Watchdog::feed();

    // Step 4: Compare and keep the better one
    Serial.printf("[Camera] Brightness — OFF=%.1f, ON=%.1f\n",
                  brightnessOff, brightnessOn);

    if (brightnessOn > brightnessOff) {
        // Flash ON is better (dark conditions)
        if (frameOff) esp_camera_fb_return(frameOff);
        Serial.println("[Camera] Using flash ON frame");
        return frameOn;
    } else {
        // Flash OFF is better (daytime)
        if (frameOn) esp_camera_fb_return(frameOn);
        Serial.println("[Camera] Using flash OFF frame");
        return frameOff;
    }
}

camera_fb_t* Camera::captureStableFrame() {
    if (!cameraReady && !init()) return nullptr;

    Watchdog::feed();

    // Warmup
    for (int i = 0; i < CAMERA_DUMMY_FRAMES; i++) {
        camera_fb_t *dummy = esp_camera_fb_get();
        if (dummy) esp_camera_fb_return(dummy);
        delay(50);
    }

    Watchdog::feed();
    return esp_camera_fb_get();
}

camera_fb_t* Camera::captureFrame() {
    if (!cameraReady && !init()) return nullptr;
    return esp_camera_fb_get();
}

void Camera::returnFrame(camera_fb_t *fb) {
    if (fb) {
        esp_camera_fb_return(fb);
    }
}

bool Camera::isInitialized() {
    return cameraReady;
}
