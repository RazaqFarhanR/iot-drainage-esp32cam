#include "ntp_sync.h"
#include "../core/state_machine.h"
#include "../config/defaults.h"
#include <Arduino.h>
#include <time.h>
#include <sys/time.h>

static bool synced = false;

bool NTPSync::sync() {
    Serial.println("[NTP] Syncing...");

    configTime(0, 0, NTP_SERVER);

    unsigned long start = millis();
    time_t now = 0;
    struct tm timeinfo;

    while (now < 1000000 && (millis() - start) < (NTP_TIMEOUT_SEC * 1000)) {
        time(&now);
        localtime_r(&now, &timeinfo);
        delay(100);
    }

    if (now > 1000000) {
        // Success — store in RTC memory
        RTCData &rtc = StateMachine::getRTCData();
        rtc.ntpEpochBase = (uint32_t)now;
        rtc.ntpMillisBase = millis();
        rtc.ntpSynced = true;
        synced = true;

        Serial.printf("[NTP] Synced: %u (epoch)\n", (uint32_t)now);
        return true;
    }

    Serial.println("[NTP] Sync failed — using RTC estimation");
    synced = false;
    return false;
}

uint32_t NTPSync::getTime() {
    return (uint32_t)time(nullptr);
}

bool NTPSync::isSynced() {
    return synced || StateMachine::getRTCData().ntpSynced;
}

bool NTPSync::isDailySnapshotDue(int hour, int minute) {
    uint32_t now = getTime();
    if (now < 1000000) return false;  // No valid time

    struct tm timeinfo;
    time_t t = (time_t)now;
    localtime_r(&t, &timeinfo);

    // Check if current hour matches target ±30min window
    int currentMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
    int targetMinutes = hour * 60 + minute;
    int diff = abs(currentMinutes - targetMinutes);

    RTCData &rtc = StateMachine::getRTCData();
    if (diff <= 30 && timeinfo.tm_mday != rtc.lastSnapshotDay) {
        rtc.lastSnapshotDay = timeinfo.tm_mday;
        return true;
    }

    return false;
}
