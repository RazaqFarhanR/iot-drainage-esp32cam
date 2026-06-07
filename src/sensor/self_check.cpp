#include "self_check.h"
#include "../core/state_machine.h"
#include "../core/nvs_manager.h"
#include "../config/defaults.h"
#include <Arduino.h>
#include <cmath>

const char* SelfCheck::flagToString(SensorFlag flag) {
    switch (flag) {
        case SensorFlag::OK:                return "OK";
        case SensorFlag::SENSOR_FAULT:      return "SENSOR_FAULT";
        case SensorFlag::SENSOR_UNSTABLE:   return "SENSOR_UNSTABLE";
        case SensorFlag::SPIKE_DETECTED:    return "SPIKE_DETECTED";
        case SensorFlag::SENSOR_STUCK:      return "SENSOR_STUCK";
        case SensorFlag::SENSOR_SUBMERGED:  return "SENSOR_SUBMERGED";
        case SensorFlag::SENSOR_DISPLACED:  return "SENSOR_DISPLACED";
        default:                            return "UNKNOWN";
    }
}

SelfCheckResult SelfCheck::check(const MeasurementResult &result) {
    SelfCheckResult out;
    memset(&out, 0, sizeof(out));
    out.flag = SensorFlag::OK;
    out.flagStr = "OK";

    RTCData &rtc = StateMachine::getRTCData();

    // --- Check 1: SENSOR_FAULT (median=0 or >500) ---
    if (!result.valid || result.median <= SENSOR_MIN_CM || result.median > SENSOR_MAX_CM) {
        rtc.faultCounter++;
        Serial.printf("[SelfCheck] FAULT detected (count: %d)\n", rtc.faultCounter);

        // Progressive escalation
        if (rtc.faultCounter > FAULT_ESCALATION_SLEEP) {
            // >10x → sleep 60 min
            out.flag = SensorFlag::SENSOR_SUBMERGED;
            out.shouldSkipData = true;
            out.forceBahaya = true;
            out.overrideSleepSec = SLEEP_FAULT_SEC;
            
            StateMachine::bufferLog("ERROR", "Sensor malfungsi > 10 siklus. Menjeda operasional selama 60 menit.");
        } else if (rtc.faultCounter > FAULT_ESCALATION_BAHAYA) {
            // >3x → force BAHAYA, assume submerged
            out.flag = SensorFlag::SENSOR_SUBMERGED;
            out.shouldSkipData = false;
            out.forceBahaya = true;
        } else if (rtc.faultCounter >= FAULT_ESCALATION_ALERT) {
            // 2-3x → UNSTABLE alert
            out.flag = SensorFlag::SENSOR_UNSTABLE;
            out.shouldSkipData = false;
        } else {
            // 1x → FAULT flag
            out.flag = SensorFlag::SENSOR_FAULT;
            out.shouldSkipData = true;
        }
        out.flagStr = flagToString(out.flag);
        return out;
    }

    // Valid reading — reset fault counter
    rtc.faultCounter = 0;

    // --- Check 2: SENSOR_UNSTABLE (variance >50cm²) ---
    if (result.variance > VARIANCE_UNSTABLE_LIMIT) {
        out.flag = SensorFlag::SENSOR_UNSTABLE;
        out.flagStr = flagToString(out.flag);
        Serial.printf("[SelfCheck] UNSTABLE — variance=%.1f\n", result.variance);
        return out;
    }

    // --- Check 3: SPIKE_DETECTED (Δ >30cm in 1 cycle) ---
    if (rtc.hasLastDistance) {
        float delta = fabsf(result.median - rtc.lastDistance);
        if (delta > SPIKE_THRESHOLD_CM) {
            out.flag = SensorFlag::SPIKE_DETECTED;
            out.flagStr = flagToString(out.flag);
            out.shouldSkipSmoothing = true;
            Serial.printf("[SelfCheck] SPIKE — Δ=%.1f cm\n", delta);
            return out;
        }
    }

    // --- Check 4: SENSOR_STUCK (constant for 5 cycles) ---
    // Shift history and add current
    for (int i = 0; i < 4; i++) {
        rtc.lastMeasurements[i] = rtc.lastMeasurements[i + 1];
    }
    rtc.lastMeasurements[4] = result.median;

    if (rtc.stuckCounter >= 4) {
        bool allSame = true;
        for (int i = 0; i < 4; i++) {
            if (fabsf(rtc.lastMeasurements[i] - rtc.lastMeasurements[i + 1]) > 0.5f) {
                allSame = false;
                break;
            }
        }
        if (allSame) {
            out.flag = SensorFlag::SENSOR_STUCK;
            out.flagStr = flagToString(out.flag);
            out.enterMaintenance = true;
            Serial.println("[SelfCheck] STUCK — constant readings for 5 cycles");
            return out;
        }
    }
    if (rtc.stuckCounter < 255) rtc.stuckCounter++;

    out.flagStr = flagToString(out.flag);
    return out;
}

void SelfCheck::updateBaseline(float waterLevel) {
    RTCData &rtc = StateMachine::getRTCData();
    if (rtc.baselineAvg == 0.0f) {
        rtc.baselineAvg = waterLevel;
    } else {
        rtc.baselineAvg = (0.95f * rtc.baselineAvg) + (0.05f * waterLevel);
    }
}
