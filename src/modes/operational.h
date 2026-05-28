#pragma once

#include "../sensor/ultrasonic.h"
#include "../sensor/self_check.h"
#include <stdint.h>

namespace OperationalMode {
    void run();

    // Private helpers for readability
    bool connectNetwork();
    MeasurementResult takeMeasurements(SelfCheckResult &checkResult);
    const char* determineStatus(const MeasurementResult &meas, const SelfCheckResult &check, uint64_t &sleepSec);
    void transmitData(const MeasurementResult &meas, const SelfCheckResult &check, const char *status, bool rainDetected);
    void handleCameraUploads(const char *status, bool shouldSkipData);
    void goToSleep(uint64_t sleepSec, const SelfCheckResult &check);
}
