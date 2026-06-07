#include "nvs_manager.h"
#include "../config/defaults.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <Arduino.h>
#include "state_machine.h"

// Internal Helpers

static uint32_t crc32Table[256];
static bool crc32Initialized = false;

static void initCRC32Table() {
    if (crc32Initialized) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
        crc32Table[i] = crc;
    }
    crc32Initialized = true;
}

uint32_t NVSManager::computeCRC32(const void *data, size_t length) {
    initCRC32Table();
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0; i < length; i++) {
        crc = (crc >> 8) ^ crc32Table[(crc ^ bytes[i]) & 0xFF];
    }
    return crc ^ 0xFFFFFFFF;
}

/**
 * Write blob to NVS with CRC32 verification via staging.
 */
static bool writeVerified(const char *ns, const char *key, const void *data, size_t len) {
    uint32_t expectedCRC = NVSManager::computeCRC32(data, len);

    // Step 1: Write to staging
    nvs_handle_t staging;
    if (nvs_open(NVS_NS_STAGING, NVS_READWRITE, &staging) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(staging, key, data, len);
    if (err != ESP_OK) { nvs_close(staging); return false; }
    nvs_commit(staging);

    // Step 2: Read back and verify
    size_t readLen = len;
    uint8_t *readBuf = (uint8_t *)malloc(len);
    if (!readBuf) { nvs_close(staging); return false; }

    err = nvs_get_blob(staging, key, readBuf, &readLen);
    nvs_close(staging);

    if (err != ESP_OK || readLen != len) {
        free(readBuf);
        return false;
    }

    uint32_t actualCRC = NVSManager::computeCRC32(readBuf, readLen);
    free(readBuf);

    if (actualCRC != expectedCRC) {
        Serial.println("[NVS] CRC mismatch on staging — aborting write");
        // Erase staging
        nvs_handle_t stg;
        if (nvs_open(NVS_NS_STAGING, NVS_READWRITE, &stg) == ESP_OK) {
            nvs_erase_key(stg, key);
            nvs_commit(stg);
            nvs_close(stg);
        }
        return false;
    }

    // Step 3: Copy to active namespace
    nvs_handle_t active;
    if (nvs_open(ns, NVS_READWRITE, &active) != ESP_OK) return false;
    err = nvs_set_blob(active, key, data, len);
    if (err != ESP_OK) { nvs_close(active); return false; }

    // Also store CRC
    char crcKey[20];
    snprintf(crcKey, sizeof(crcKey), "%s_crc", key);
    nvs_set_u32(active, crcKey, expectedCRC);

    nvs_commit(active);
    nvs_close(active);

    // Clean staging
    nvs_handle_t stg;
    if (nvs_open(NVS_NS_STAGING, NVS_READWRITE, &stg) == ESP_OK) {
        nvs_erase_key(stg, key);
        nvs_commit(stg);
        nvs_close(stg);
    }

    return true;
}

/**
 * Read blob from NVS with CRC32 verification.
 */
static bool readVerified(const char *ns, const char *key, void *data, size_t len) {
    nvs_handle_t handle;
    if (nvs_open(ns, NVS_READONLY, &handle) != ESP_OK) return false;

    size_t readLen = len;
    esp_err_t err = nvs_get_blob(handle, key, data, &readLen);
    if (err != ESP_OK || readLen != len) {
        nvs_close(handle);
        return false;
    }

    // Verify CRC
    char crcKey[20];
    snprintf(crcKey, sizeof(crcKey), "%s_crc", key);
    uint32_t storedCRC = 0;
    err = nvs_get_u32(handle, crcKey, &storedCRC);
    nvs_close(handle);

    if (err != ESP_OK) {
        // No CRC stored — legacy data, accept it
        return true;
    }

    uint32_t actualCRC = NVSManager::computeCRC32(data, len);
    if (actualCRC != storedCRC) {
        Serial.printf("[NVS] CRC mismatch for %s/%s — data corrupt\n", ns, key);
        return false;
    }

    return true;
}

// Public API

bool NVSManager::init() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        Serial.println("[NVS] Erasing flash due to corruption...");
        StateMachine::bufferLog("WARNING", "NVS Corrupt terdeteksi. Melakukan Format Ulang");
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    return (err == ESP_OK);
}

bool NVSManager::hasValidWiFiConfig() {
    WiFiConfig cfg;
    if (!loadWiFiConfig(cfg)) return false;
    return cfg.valid && strlen(cfg.ssid) > 0;
}

// --- WiFi Config ---
bool NVSManager::loadWiFiConfig(WiFiConfig &cfg) {
    memset(&cfg, 0, sizeof(cfg));
    return readVerified(NVS_NS_WIFI, "wifi", &cfg, sizeof(cfg));
}

bool NVSManager::saveWiFiConfig(const WiFiConfig &cfg) {
    return writeVerified(NVS_NS_WIFI, "wifi", &cfg, sizeof(cfg));
}

bool NVSManager::clearWiFiConfig() {
    nvs_handle_t handle;
    if (nvs_open(NVS_NS_WIFI, NVS_READWRITE, &handle) != ESP_OK) return false;
    nvs_erase_all(handle);
    nvs_commit(handle);
    nvs_close(handle);
    return true;
}

// --- Sensor Config ---
bool NVSManager::loadSensorConfig(SensorConfig &cfg) {
    memset(&cfg, 0, sizeof(cfg));
    return readVerified(NVS_NS_SENSOR, "sensor", &cfg, sizeof(cfg));
}

bool NVSManager::saveSensorConfig(const SensorConfig &cfg) {
    return writeVerified(NVS_NS_SENSOR, "sensor", &cfg, sizeof(cfg));
}

// --- Threshold Config ---
bool NVSManager::loadThresholdConfig(ThresholdConfig &cfg) {
    memset(&cfg, 0, sizeof(cfg));
    return readVerified(NVS_NS_THRESHOLD, "thresh", &cfg, sizeof(cfg));
}

bool NVSManager::saveThresholdConfig(const ThresholdConfig &cfg) {
    return writeVerified(NVS_NS_THRESHOLD, "thresh", &cfg, sizeof(cfg));
}

// --- Backend Config ---
bool NVSManager::loadBackendConfig(BackendConfig &cfg) {
    memset(&cfg, 0, sizeof(cfg));
    return readVerified(NVS_NS_DEVICE, "backend", &cfg, sizeof(cfg));
}

bool NVSManager::saveBackendConfig(const BackendConfig &cfg) {
    return writeVerified(NVS_NS_DEVICE, "backend", &cfg, sizeof(cfg));
}

// --- Auth Config ---
bool NVSManager::loadAuthConfig(AuthConfig &cfg) {
    memset(&cfg, 0, sizeof(cfg));
    return readVerified(NVS_NS_AUTH, "auth", &cfg, sizeof(cfg));
}

bool NVSManager::saveAuthConfig(const AuthConfig &cfg) {
    return writeVerified(NVS_NS_AUTH, "auth", &cfg, sizeof(cfg));
}

// --- Device Config ---
bool NVSManager::loadDeviceConfig(DeviceConfig &cfg) {
    memset(&cfg, 0, sizeof(cfg));
    return readVerified(NVS_NS_DEVICE, "device", &cfg, sizeof(cfg));
}

bool NVSManager::saveDeviceConfig(const DeviceConfig &cfg) {
    return writeVerified(NVS_NS_DEVICE, "device", &cfg, sizeof(cfg));
}

// --- Location Config ---
bool NVSManager::loadLocationConfig(LocationConfig &cfg) {
    memset(&cfg, 0, sizeof(cfg));
    return readVerified(NVS_NS_DEVICE, "location", &cfg, sizeof(cfg));
}

bool NVSManager::saveLocationConfig(const LocationConfig &cfg) {
    return writeVerified(NVS_NS_DEVICE, "location", &cfg, sizeof(cfg));
}

// --- Camera Config ---
bool NVSManager::loadCameraConfig(CameraConfig &cfg) {
    memset(&cfg, 0, sizeof(cfg));
    return readVerified(NVS_NS_DEVICE, "camera", &cfg, sizeof(cfg));
}

bool NVSManager::saveCameraConfig(const CameraConfig &cfg) {
    return writeVerified(NVS_NS_DEVICE, "camera", &cfg, sizeof(cfg));
}

// --- Factory Reset ---
bool NVSManager::factoryReset() {
    Serial.println("[NVS] *** FACTORY RESET — erasing all namespaces ***");
    const char *namespaces[] = {
        NVS_NS_WIFI, NVS_NS_SENSOR, NVS_NS_THRESHOLD,
        NVS_NS_AUTH, NVS_NS_DEVICE, NVS_NS_STAGING
    };
    for (auto ns : namespaces) {
        nvs_handle_t handle;
        if (nvs_open(ns, NVS_READWRITE, &handle) == ESP_OK) {
            nvs_erase_all(handle);
            nvs_commit(handle);
            nvs_close(handle);
        }
    }
    return true;
}
