#pragma once

#include "esp_camera.h"

namespace Camera {
    /**
     * Initialize camera module.
     * Detects PSRAM for resolution selection.
     * @return true if camera initialized
     */
    bool init();

    /**
     * Deinitialize camera to free resources.
     */
    void deinit();

    /**
     * Capture best photo using multi-frame exposure.
     * Takes 3 dummy frames, then flash OFF/ON comparison.
     * Caller must return frame buffer via returnFrame().
     * @return camera_fb_t* or nullptr if failed
     */
    camera_fb_t* captureBestPhoto();

    /**
     * Capture a single stable frame (takes dummy frames first).
     * Used for on-demand Preview Camera.
     * @return camera_fb_t* or nullptr
     */
    camera_fb_t* captureStableFrame();

    /**
     * Capture a single frame (no multi-exposure).
     * Used for WebSocket streaming in Commissioning mode.
     * @return camera_fb_t* or nullptr
     */
    camera_fb_t* captureFrame();

    /**
     * Return frame buffer to camera driver.
     */
    void returnFrame(camera_fb_t *fb);

    /**
     * Check if camera was initialized.
     */
    bool isInitialized();
}
