#pragma once

#include <cstdint>

/**
 * @file ntp_sync.h
 * @brief NTP time synchronization with RTC memory fallback (§11.8).
 */

namespace NTPSync {
    /**
     * Synchronize time from NTP server (5s timeout).
     * Stores epoch base in RTC memory.
     * @return true if sync succeeded
     */
    bool sync();

    /**
     * Get current estimated time.
     * Uses NTP if synced, otherwise estimates from RTC base.
     * @return Unix epoch timestamp
     */
    uint32_t getTime();

    /**
     * Check if time has been synced this session.
     */
    bool isSynced();

    /**
     * Check if daily snapshot is due (§3.4).
     * @param hour Target hour (0-23)
     * @param minute Target minute (0-59)
     * @return true if current time matches ±30 min window
     */
    bool isDailySnapshotDue(int hour, int minute);
}
