#include "SensorManager.h"

SensorManager::SensorManager() : _as5600(&Wire) {
    _sampleIndex = 0;
    _sampleCount = 0;
    _as5600Initialized = false;
    _mpu6050Initialized = false;
    _mpuWhoAmI = 0;
    _as5600Misses = 0;
    _mpu6050Misses = 0;

    memset(&_sample, 0, sizeof(_sample));
    memset(&_health, 0, sizeof(_health));

    for (int i = 0; i < AVERAGE_SAMPLES; i++) {
        _altitudeSamples[i] = 0.0f;
    }
}

bool SensorManager::begin(uint8_t sda, uint8_t scl, uint32_t i2cFreq) {
    Serial.println("[Sensors] Initializing I2C...");

    Wire.begin(sda, scl);
    Wire.setClock(i2cFreq);

    Serial.printf("[Sensors] I2C up on SDA=%u SCL=%u @ %u Hz\n", sda, scl, i2cFreq);
    delay(500); // Give sensors time to power up

    _as5600Initialized = initAS5600();
    delay(100);
    _mpu6050Initialized = initMPU6050();

    refreshHealth();

    Serial.printf("[Sensors] Init complete - AS5600: %s, MPU6050: %s\n",
                  _as5600Initialized ? "OK" : "FAILED",
                  _mpu6050Initialized ? "OK" : "FAILED");

    // At least one axis working is enough to be useful.
    return _as5600Initialized || _mpu6050Initialized;
}

bool SensorManager::initAS5600() {
    Serial.println("[Sensors] Probing AS5600 (azimuth)...");
    _as5600.begin();
    delay(50);

    if (!_as5600.isConnected()) {
        Serial.println("[Sensors] AS5600 not responding on I2C");
        return false;
    }

    Serial.printf("[Sensors] AS5600 detected, raw angle %u\n", _as5600.getRawAngle());
    if (!_as5600.isMagnetDetected()) {
        Serial.println("[Sensors] WARNING: AS5600 reports no magnet detected");
    }
    return true;
}

bool SensorManager::probeMPU6050() {
    Wire.beginTransmission(MPU6050_ADDRESS);
    return (Wire.endTransmission() == 0);
}

bool SensorManager::writeMpuRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(MPU6050_ADDRESS);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

uint8_t SensorManager::readMpuRegister(uint8_t reg) {
    Wire.beginTransmission(MPU6050_ADDRESS);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return 0xFF;
    Wire.requestFrom(MPU6050_ADDRESS, 1);
    return Wire.available() ? Wire.read() : 0xFF;
}

const char* SensorManager::describeMpuId(uint8_t whoAmI) {
    switch (whoAmI) {
        case 0x68: return "MPU6050";
        case 0x70: return "MPU6500";
        case 0x71: return "MPU9250";
        case 0x73: return "MPU9255";
        case 0x74: return "MPU6515";
        case 0x98: return "ICM20689";
        default:   return "unknown";
    }
}

// Reset and configure the chip ourselves.
//
// Adafruit's begin() refuses anything whose WHO_AM_I is not 0x68, so on the
// MPU6500 and other pin-compatible parts it returns false BEFORE running its
// reset-and-configure sequence. Ignoring that return leaves the device running
// on whatever register state it happens to hold - and because an ESP32 reset
// does not power-cycle the sensor, that stale state survives reboots and
// reflashes. The symptom is a gravity vector whose magnitude is not 9.81, which
// in turn makes altitude badly wrong.
bool SensorManager::configureMpu() {
    if (!writeMpuRegister(MPU_REG_PWR_MGMT_1, 0x80)) {  // DEVICE_RESET
        return false;
    }
    delay(100);                                          // reset takes ~50ms

    writeMpuRegister(MPU_REG_PWR_MGMT_1, 0x01);          // wake, clock = gyro X PLL
    delay(10);
    writeMpuRegister(MPU_REG_PWR_MGMT_2, 0x00);          // every axis enabled
    writeMpuRegister(MPU_REG_CONFIG, 0x04);              // DLPF ~21Hz
    writeMpuRegister(MPU_REG_SMPLRT_DIV, 0x00);
    writeMpuRegister(MPU_REG_GYRO_CONFIG, 0x00);         // +/-250 deg/s
    writeMpuRegister(MPU_REG_ACCEL_CONFIG, 0x00);        // +/-2g -> 16384 LSB/g
    writeMpuRegister(MPU_REG_ACCEL_CONFIG2, 0x04);       // accel DLPF, MPU6500+
    delay(50);

    return readMpuRegister(MPU_REG_ACCEL_CONFIG) == 0x00;
}

bool SensorManager::initMPU6050() {
    Serial.println("[Sensors] Probing accelerometer...");

    if (!probeMPU6050()) {
        Serial.println("[Sensors] Accelerometer not responding on I2C");
        return false;
    }

    _mpuWhoAmI = readMpuRegister(MPU_REG_WHO_AM_I);

    // Did the sensor keep its power across this reset?
    //
    // CONFIG defaults to 0x00 at power-on and we always set it to 0x04, so
    // reading 0x04 here means the chip was never power-cycled. An ESP32 reset
    // alone does not power-cycle it; a genuine loss of the 3V3 rail does.
    // This is how to tell whether a power switch really cuts the rail or merely
    // resets the MCU while peripherals keep draining the battery.
    uint8_t configBefore = readMpuRegister(MPU_REG_CONFIG);
    Serial.printf("[Power] Sensor CONFIG before init = 0x%02X -> sensor %s\n",
                  configBefore,
                  configBefore == 0x04 ? "KEPT POWER across this reset"
                                       : "WAS POWER-CYCLED (3V3 rail dropped)");

    // Creates the library's bus object, which getEvent() needs. It returns
    // false on anything that is not a genuine MPU6050; that is expected and not
    // fatal, because we configure the device ourselves below.
    bool libraryAccepted = _mpu6050.begin(MPU6050_ADDRESS, &Wire);

    if (!configureMpu()) {
        Serial.println("[Sensors] Accelerometer failed to accept configuration");
        return false;
    }

    Serial.printf("[Sensors] Accelerometer WHO_AM_I=0x%02X (%s), library %s, "
                  "configured manually\n",
                  _mpuWhoAmI, describeMpuId(_mpuWhoAmI),
                  libraryAccepted ? "accepted it" : "rejected it (expected)");
    dumpMpuConfig();
    return true;
}

void SensorManager::dumpMpuConfig() {
    struct { uint8_t reg; const char* name; } regs[] = {
        {MPU_REG_PWR_MGMT_1,   "PWR_MGMT_1  "},
        {MPU_REG_PWR_MGMT_2,   "PWR_MGMT_2  "},
        {MPU_REG_GYRO_CONFIG,  "GYRO_CONFIG "},
        {MPU_REG_ACCEL_CONFIG, "ACCEL_CONFIG"},
        {MPU_REG_CONFIG,       "CONFIG      "},
        {MPU_REG_SMPLRT_DIV,   "SMPLRT_DIV  "},
        {MPU_REG_WHO_AM_I,     "WHO_AM_I    "},
    };
    Serial.println("[Sensors] Accelerometer register state:");
    for (auto& r : regs) {
        Serial.printf("  %s (0x%02X) = 0x%02X\n",
                      r.name, r.reg, readMpuRegister(r.reg));
    }
}

void SensorManager::update() {
    // Azimuth from the AS5600.
    //
    // In this build the AS5600 chip is mounted upside-down on the rotating
    // assembly while the magnet stays stationary below it. With that
    // arrangement the chip's native angle already increases clockwise as
    // viewed from above (N -> E -> S -> W), matching the astronomical
    // alt/az convention, so no inversion is needed.
    if (_as5600Initialized) {
        uint16_t ticks = _as5600.getRawAngle();
        _sample.azimuthRawTicks = ticks;
        _sample.azimuthDegrees = (ticks * 360.0f) / AS5600_COUNTS_PER_REV;
        _sample.azimuthValid = true;
    } else {
        _sample.azimuthValid = false;
    }

    // Altitude from the accelerometer, smoothed by a rolling average.
    if (_mpu6050Initialized) {
        // g and temp are unused.
        sensors_event_t a, g, temp;
        _mpu6050.getEvent(&a, &g, &temp);

        float rawAltitude = calculateAltitudeFromAccel(a.acceleration.x,
                                                       a.acceleration.y,
                                                       a.acceleration.z);

        _altitudeSamples[_sampleIndex] = rawAltitude;
        _sampleIndex = (_sampleIndex + 1) % AVERAGE_SAMPLES;
        if (_sampleCount < AVERAGE_SAMPLES) {
            _sampleCount++;
        }

        _sample.accelX = a.acceleration.x;
        _sample.accelY = a.acceleration.y;
        _sample.accelZ = a.acceleration.z;

        // Sanity signal: with the bias removed this must sit near 9.81 in every
        // orientation. Anything else means the sensor is misconfigured, and it
        // is the cheapest way to catch that before it corrupts pointing.
        float cx = a.acceleration.x - ACCEL_BIAS_X;
        float cy = a.acceleration.y - ACCEL_BIAS_Y;
        float cz = a.acceleration.z - ACCEL_BIAS_Z;
        _sample.gravityMagnitude = sqrtf(cx*cx + cy*cy + cz*cz);
        _sample.altitudeRawDegrees = rawAltitude;
        _sample.altitudeDegrees = getAveragedAltitude();
        _sample.altitudeValid = true;
    } else {
        _sample.altitudeValid = false;
    }

    _sample.averagedSamples = (uint8_t)_sampleCount;
    _sample.timestamp = millis();
}

void SensorManager::refreshHealth() {
    // Hot-plug detection, debounced. A sensor must miss SENSOR_ABSENT_THRESHOLD
    // consecutive probes before it is treated as gone, so a one-off bus timeout
    // does not knock an axis out mid-observation.
    if (_as5600.isConnected()) {
        _mpuWhoAmI = 0;
    _as5600Misses = 0;
        if (!_as5600Initialized) {
            _as5600Initialized = initAS5600();
            if (_as5600Initialized) {
                Serial.println("[Sensors] AS5600 reconnected");
            }
        }
    } else if (_as5600Initialized && ++_as5600Misses >= SENSOR_ABSENT_THRESHOLD) {
        _as5600Initialized = false;
        Serial.println("[Sensors] AS5600 disconnected");
    }

    if (probeMPU6050()) {
        _mpu6050Misses = 0;
        if (!_mpu6050Initialized) {
            _mpu6050Initialized = initMPU6050();
            if (_mpu6050Initialized) {
                Serial.println("[Sensors] MPU6050 reconnected");
            }
        }
    } else if (_mpu6050Initialized && ++_mpu6050Misses >= SENSOR_ABSENT_THRESHOLD) {
        _mpu6050Initialized = false;
        clearAltitudeAverage();
        Serial.println("[Sensors] MPU6050 disconnected");
    }

    _health.azimuthSensorPresent = _as5600Initialized;
    _health.altitudeSensorPresent = _mpu6050Initialized;

    if (_health.azimuthSensorPresent) {
        uint8_t status = _as5600.getStatus();
        _health.magnetDetected = (status & 0x20) != 0;
        _health.magnetTooWeak = (status & 0x10) != 0;
        _health.magnetTooStrong = (status & 0x08) != 0;
        _health.magnetAgc = _as5600.getAGC();
        _health.magnetMagnitude = _as5600.getMagnitude();
    } else {
        _health.magnetDetected = false;
        _health.magnetTooWeak = false;
        _health.magnetTooStrong = false;
        _health.magnetAgc = 0;
        _health.magnetMagnitude = 0;
    }
}

SensorSample SensorManager::getSample() const {
    return _sample;
}

SensorHealth SensorManager::getHealth() const {
    return _health;
}

String SensorManager::describe() const {
    String diag = "Sensor Diagnostics:\n";
    diag += "  AS5600 (az):  " + String(_health.azimuthSensorPresent ? "Connected" : "Disconnected") + "\n";
    diag += "  MPU6050 (alt): " + String(_health.altitudeSensorPresent ? "Connected" : "Disconnected") + "\n";

    if (_health.azimuthSensorPresent) {
        diag += "  Magnet detected:  " + String(_health.magnetDetected ? "Yes" : "No") + "\n";
        diag += "  Magnet too weak:  " + String(_health.magnetTooWeak ? "Yes" : "No") + "\n";
        diag += "  Magnet too strong: " + String(_health.magnetTooStrong ? "Yes" : "No") + "\n";
        diag += "  AGC:       " + String(_health.magnetAgc) + "\n";
        diag += "  Magnitude: " + String(_health.magnetMagnitude) + "\n";
    }

    diag += "  Azimuth:  " + String(_sample.azimuthDegrees, 2) + " deg (raw " + String(_sample.azimuthRawTicks) + ")\n";
    diag += "  Altitude: " + String(_sample.altitudeDegrees, 2) + " deg (raw " + String(_sample.altitudeRawDegrees, 2) + ")\n";
    diag += "  Gravity |g|: " + String(_sample.gravityMagnitude, 3) + " m/s^2 (expect ~9.81)\n";
    diag += "  Averaged samples: " + String(_sample.averagedSamples) + "/" + String(AVERAGE_SAMPLES) + "\n";

    return diag;
}

float SensorManager::calculateAltitudeFromAccel(float ax, float ay, float az) const {
    // Remove the chip's zero-g bias first. An off-centre gravity vector skews
    // atan2 by several degrees in the middle of the range, which is invisible
    // in a stationary reading and only shows up when the tube moves.
    ax -= ACCEL_BIAS_X;
    ay -= ACCEL_BIAS_Y;
    az -= ACCEL_BIAS_Z;

    // Pitch angle from the accelerometer. The sensor is mounted on the tube
    // with the Y axis along the tube and Z perpendicular to it (pointing up
    // when level).
    return atan2(-ax, sqrt(ay * ay + az * az)) * 180.0 / PI;
}

float SensorManager::getAveragedAltitude() const {
    if (_sampleCount == 0) return 0.0f;

    float sum = 0.0f;
    for (int i = 0; i < _sampleCount; i++) {
        sum += _altitudeSamples[i];
    }
    return sum / _sampleCount;
}

void SensorManager::clearAltitudeAverage() {
    // Drop the averaging window so readings from before a disconnect never
    // blend into readings from after it.
    //
    // The last reported angle is deliberately left in place. BBox has no way to
    // say "unknown", so holding the last known altitude is far less misleading
    // to SkySafari than suddenly reporting the horizon.
    _sampleIndex = 0;
    _sampleCount = 0;
}
