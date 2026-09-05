#ifndef TELESCOPE_STATE_H
#define TELESCOPE_STATE_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "../sensors/SensorManager.h"

// Encoder-equivalent resolution reported for BOTH telescope axes, in counts
// per full revolution.
//
// 4096 is the AS5600's native 12-bit resolution, so azimuth counts map 1:1
// onto raw encoder ticks with no requantization. Altitude has no real encoder,
// so its accelerometer angle is scaled onto the same synthetic scale; SkySafari
// only cares that the number is consistent.
//
// This value must match what is entered for both axes in SkySafari's
// "Basic Encoder System" scope settings.
#define TELESCOPE_COUNTS_PER_REV 4096

// One telescope axis, in every form a consumer might want.
struct TelescopeAxis {
    float rawValue;      // Sensor-native reading (AS5600 ticks / accel pitch deg)
    float degrees;       // Normalized angle in degrees
    int32_t count;       // Encoder-equivalent count, 0..TELESCOPE_COUNTS_PER_REV-1
    bool valid;          // False when the backing sensor is absent
};

// A consistent, point-in-time view of the mount. Consumers copy one of these
// and are then free of any locking concerns.
struct TelescopeSnapshot {
    TelescopeAxis azimuth;
    TelescopeAxis altitude;
    SensorHealth health;
    float accelX;              // Raw gravity vector, for mounting calibration
    float accelY;
    float accelZ;
    float gravityMagnitude;    // |g| after bias removal; must stay near 9.81
    uint32_t sampleTimestamp;  // millis() of the underlying sensor sample
    uint32_t updateCount;      // Monotonic count of publishes since boot
};

// The single point of truth for the mount's measured physical state.
//
// The sampling task publishes into it; the protocol layer and the optional
// diagnostic display read from it. It knows nothing about BLE, BBox or the
// display, and performs no astronomy.
class TelescopeState {
public:
    TelescopeState();

    // Must be called before any task touches the state.
    void begin();

    // Called by the sampling task with the latest hardware readings.
    void publish(const SensorSample& sample, const SensorHealth& health);

    // Thread-safe consistent copy.
    TelescopeSnapshot get() const;

    // Encoder-equivalent resolution, in counts per revolution, for both axes.
    static int32_t countsPerRevolution() { return TELESCOPE_COUNTS_PER_REV; }

    // Map an angle in degrees onto an encoder count in 0..countsPerRevolution()-1.
    // Negative and out-of-range angles wrap, exactly as a real encoder would.
    static int32_t degreesToCount(float degrees);

private:
    SemaphoreHandle_t _mutex;
    TelescopeSnapshot _snapshot;
};

#endif // TELESCOPE_STATE_H
