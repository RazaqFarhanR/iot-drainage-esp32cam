#include "device_id.h"
#include "nvs_manager.h"
#include <WiFi.h>
#include <Arduino.h>

/**
 * @file device_id.cpp
 * @brief Auto-generate unique device ID from ESP32 MAC address (§11.10).
 */

static char cachedDeviceId[16] = {0};
static char cachedLocation[64] = {0};

void DeviceID::init() {
    DeviceConfig cfg;
    if (NVSManager::loadDeviceConfig(cfg) && cfg.valid && strlen(cfg.device_id) > 0) {
        strncpy(cachedDeviceId, cfg.device_id, sizeof(cachedDeviceId) - 1);
        strncpy(cachedLocation, cfg.location, sizeof(cachedLocation) - 1);
        Serial.printf("[DeviceID] Loaded from NVS: %s\n", cachedDeviceId);
        return;
    }

    // Generate from MAC address
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(cachedDeviceId, sizeof(cachedDeviceId), "IFMS-%02X%02X%02X",
             mac[3], mac[4], mac[5]);
    strncpy(cachedLocation, "Drainage-Channel", sizeof(cachedLocation) - 1);

    // Save to NVS
    DeviceConfig newCfg;
    memset(&newCfg, 0, sizeof(newCfg));
    strncpy(newCfg.device_id, cachedDeviceId, sizeof(newCfg.device_id) - 1);
    strncpy(newCfg.location, cachedLocation, sizeof(newCfg.location) - 1);
    newCfg.valid = true;
    NVSManager::saveDeviceConfig(newCfg);

    Serial.printf("[DeviceID] Generated: %s\n", cachedDeviceId);
}

const char* DeviceID::get() {
    return cachedDeviceId;
}

const char* DeviceID::getLocation() {
    return cachedLocation;
}
