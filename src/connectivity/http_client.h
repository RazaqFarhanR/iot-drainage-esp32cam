#pragma once

#include "esp_camera.h"

/**
 * @file http_client.h
 * @brief HTTP client with per-phase timeouts (§9.2, §11.12).
 */

namespace HTTPClient_ {
    /**
     * Fetch threshold config from backend (§6.1).
     * GET /api/devices/{id}/config
     * @return true if config fetched and applied
     */
    bool fetchConfig();

    /**
     * Upload BAHAYA photo to backend (§9.2).
     * POST /api/upload-image
     * @return true if upload succeeded
     */
    bool uploadBahayaImage(camera_fb_t *fb);

    /**
     * Upload diagnostic/daily snapshot (§3.3, §3.4).
     * POST /api/devices/{id}/snapshot
     * @return true if upload succeeded
     */
    bool uploadSnapshot(camera_fb_t *fb);
}
