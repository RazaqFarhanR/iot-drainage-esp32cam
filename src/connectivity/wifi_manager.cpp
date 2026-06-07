#include "wifi_manager.h"
#include "../core/nvs_manager.h"
#include "../core/state_machine.h"
#include "../core/device_id.h"
#include "../core/watchdog.h"
#include "../config/defaults.h"
#include <WiFi.h>
#include <Arduino.h>

bool WiFiMgr::connect() {
    Watchdog::setPhase(WDTPhase::WIFI);

    WiFiConfig cfg;
    if (!NVSManager::loadWiFiConfig(cfg) || !cfg.valid) {
        Serial.println("[WiFi] No valid config in NVS");
        return false;
    }

    Serial.printf("[WiFi] Connecting to '%s'...\n", cfg.ssid);
    WiFi.setTxPower(WIFI_POWER_11dBm); // Reduce power to prevent brownout

    // Try fast connect with static IP + BSSID + Channel
    bool hasStaticIP = strlen(cfg.ip) > 0 && strlen(cfg.gateway) > 0;
    if (hasStaticIP && cfg.channel > 0) {
        Serial.println("[WiFi] Attempting fast connect (static IP + BSSID)...");

        IPAddress ip, gw, mask;
        ip.fromString(cfg.ip);
        gw.fromString(cfg.gateway);
        mask.fromString(cfg.subnet);

        WiFi.config(ip, gw, mask, gw);
        WiFi.begin(cfg.ssid, cfg.password, cfg.channel, cfg.bssid);

        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED &&
               (millis() - start) < (WIFI_STATIC_TIMEOUT_SEC * 1000)) {
            Watchdog::feed();
            delay(100);
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[WiFi] Fast connected! IP: %s (took %lums)\n",
                          WiFi.localIP().toString().c_str(), millis() - start);
            StateMachine::getRTCData().wifiFailCount = 0;
            StateMachine::getRTCData().offlineRetryCount = 0;
            return true;
        }

        Serial.println("[WiFi] Fast connect failed — falling back to DHCP...");
        WiFi.disconnect();
    }

    // Fallback: DHCP connect
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
    WiFi.begin(cfg.ssid, cfg.password);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < 15000) {
        Watchdog::feed();
        delay(200);
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi] DHCP connected! IP: %s\n",
                      WiFi.localIP().toString().c_str());
        // Update NVS with new connection info
        saveConnectionInfo();
        StateMachine::getRTCData().wifiFailCount = 0;
        StateMachine::getRTCData().offlineRetryCount = 0;
        return true;
    }

    Serial.println("[WiFi] Connection failed!");
    return false;
}

void WiFiMgr::startAP() {
    char apSSID[32];
    snprintf(apSSID, sizeof(apSSID), "IOT-%s", DeviceID::get() + 4);  // Skip "IOT-" prefix, use MAC part
    // Actually use the full device ID as SSID
    snprintf(apSSID, sizeof(apSSID), "%s", DeviceID::get());

    WiFi.mode(WIFI_AP);
    WiFi.setTxPower(WIFI_POWER_8_5dBm); // Reduce TX power to prevent overheating
    WiFi.softAP(apSSID);
    delay(100);

    Serial.printf("[WiFi] AP started: %s | IP: %s\n",
                  apSSID, WiFi.softAPIP().toString().c_str());
}

void WiFiMgr::stopAP() {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    Serial.println("[WiFi] AP stopped");
}

void WiFiMgr::disconnect() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("[WiFi] Disconnected");
}

bool WiFiMgr::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

int WiFiMgr::getRSSI() {
    return WiFi.RSSI();
}

void WiFiMgr::saveConnectionInfo() {
    WiFiConfig cfg;
    NVSManager::loadWiFiConfig(cfg);  // Load existing to preserve SSID/password

    // Update network params
    strncpy(cfg.ip, WiFi.localIP().toString().c_str(), sizeof(cfg.ip) - 1);
    cfg.ip[sizeof(cfg.ip) - 1] = '\0';
    strncpy(cfg.gateway, WiFi.gatewayIP().toString().c_str(), sizeof(cfg.gateway) - 1);
    cfg.gateway[sizeof(cfg.gateway) - 1] = '\0';
    strncpy(cfg.subnet, WiFi.subnetMask().toString().c_str(), sizeof(cfg.subnet) - 1);
    cfg.subnet[sizeof(cfg.subnet) - 1] = '\0';
    
    uint8_t* bssid = WiFi.BSSID();
    if (bssid) {
        memcpy(cfg.bssid, bssid, 6);
    } else {
        memset(cfg.bssid, 0, 6);
    }
    cfg.channel = WiFi.channel();
    cfg.valid = true;

    NVSManager::saveWiFiConfig(cfg);
    Serial.println("[WiFi] Connection info saved to NVS");
}

bool WiFiMgr::handleConnectFailure() {
    RTCData &rtc = StateMachine::getRTCData();
    rtc.wifiFailCount++;

    Serial.printf("[WiFi] Fail count: %d/%d\n", rtc.wifiFailCount, WIFI_MAX_FAIL_COUNT);

    if (rtc.wifiFailCount >= WIFI_MAX_FAIL_COUNT) {
        //: 3x fail → reset NVS → commissioning
        Serial.println("[WiFi] Max failures reached → clearing WiFi config");
        NVSManager::clearWiFiConfig();
        rtc.wifiFailCount = 0;
        rtc.offlineRetryCount = 0;
        return false;  // Go to commissioning
    }

    // Still have retries — go offline with backoff
    rtc.offlineRetryCount++;
    return true;  // Go to offline mode
}
