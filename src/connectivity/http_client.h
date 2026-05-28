#pragma once

#include "esp_camera.h"

namespace HTTPClient_ {
    /**
     * Fetch threshold config from backend.
     * GET /api/devices/{id}/config
     * @return true if config fetched and applied
     */
    bool fetchConfig();

    /**
     * Upload BAHAYA photo to backend.
     * POST /api/upload-image
     * @return true if upload succeeded
     */
    bool uploadBahayaImage(camera_fb_t *fb);

    /**
     * Upload diagnostic/daily snapshot (,).
     * POST /api/devices/{id}/snapshot
     * @return true if upload succeeded
     */
    bool uploadSnapshot(camera_fb_t *fb);
}
