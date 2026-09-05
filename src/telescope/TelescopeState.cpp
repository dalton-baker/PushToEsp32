#include "TelescopeState.h"

TelescopeState::TelescopeState() : _mutex(NULL) {
    memset(&_snapshot, 0, sizeof(_snapshot));
}

void TelescopeState::begin() {
    if (_mutex == NULL) {
        _mutex = xSemaphoreCreateMutex();
    }
}

int32_t TelescopeState::degreesToCount(float degrees) {
    float wrapped = fmodf(degrees, 360.0f);
    if (wrapped < 0.0f) wrapped += 360.0f;

    int32_t count = lroundf(wrapped * (TELESCOPE_COUNTS_PER_REV / 360.0f));

    // lroundf can land exactly on the wrap point.
    count %= TELESCOPE_COUNTS_PER_REV;
    if (count < 0) count += TELESCOPE_COUNTS_PER_REV;
    return count;
}

void TelescopeState::publish(const SensorSample& sample, const SensorHealth& health) {
    if (_mutex == NULL) return;

    TelescopeSnapshot next;

    next.azimuth.rawValue = (float)sample.azimuthRawTicks;
    next.azimuth.degrees = sample.azimuthDegrees;
    next.azimuth.count = degreesToCount(sample.azimuthDegrees);
    next.azimuth.valid = sample.azimuthValid;

    next.altitude.rawValue = sample.altitudeRawDegrees;
    next.altitude.degrees = sample.altitudeDegrees;
    next.altitude.count = degreesToCount(sample.altitudeDegrees);
    next.altitude.valid = sample.altitudeValid;

    next.accelX = sample.accelX;
    next.accelY = sample.accelY;
    next.accelZ = sample.accelZ;
    next.gravityMagnitude = sample.gravityMagnitude;
    next.health = health;
    next.sampleTimestamp = sample.timestamp;

    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        next.updateCount = _snapshot.updateCount + 1;
        _snapshot = next;
        xSemaphoreGive(_mutex);
    }
}

TelescopeSnapshot TelescopeState::get() const {
    TelescopeSnapshot copy;

    if (_mutex != NULL && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        copy = _snapshot;
        xSemaphoreGive(_mutex);
    } else {
        memset(&copy, 0, sizeof(copy));
    }

    return copy;
}
