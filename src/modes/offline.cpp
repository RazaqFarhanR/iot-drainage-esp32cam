#include "offline.h"
#include "../core/state_machine.h"
#include "../core/watchdog.h"
#include "../connectivity/wifi_manager.h"
#include <Arduino.h>

void OfflineMode::run() {
    Serial.println("\n╔══════════════════════════════════════╗");
    Serial.println("║    OFFLINE MODE                      ║");
    Serial.println("╚══════════════════════════════════════╝\n");

    RTCData &rtc = StateMachine::getRTCData();
    Serial.printf("[OFFLINE] Retry count: %d\n", rtc.offlineRetryCount);

    // Try WiFi connection
    Watchdog::setPhase(WDTPhase::WIFI);
    if (WiFiMgr::connect()) {
        Serial.println("[OFFLINE] WiFi reconnected! → Operational");
        rtc.offlineRetryCount = 0;
        WiFiMgr::disconnect();
        StateMachine::requestMode(SystemMode::OPERATIONAL);
        return;
    }

    // WiFi still down — increment retry and backoff
    rtc.offlineRetryCount++;

    bool shouldGoCommissioning = !WiFiMgr::handleConnectFailure();
    if (shouldGoCommissioning) {
        Serial.println("[OFFLINE] Max WiFi failures → Commissioning");
        StateMachine::requestMode(SystemMode::COMMISSIONING);
        return;
    }

    // Calculate backoff sleep
    uint64_t backoffSec = StateMachine::getOfflineBackoffSeconds();
    Serial.printf("[OFFLINE] Sleeping for %llu seconds (retry #%d)\n",
                  backoffSec, rtc.offlineRetryCount);

    Watchdog::setPhase(WDTPhase::SLEEP);
    StateMachine::enterDeepSleep(backoffSec);
}
