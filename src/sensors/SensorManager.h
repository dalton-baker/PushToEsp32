#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include "AS5600.h"

#define MPU6050_ADDRESS 0x68

// Register map shared by the MPU6050 and its pin-compatible relatives.
#define MPU_REG_SMPLRT_DIV   0x19
#define MPU_REG_CONFIG       0x1A
#define MPU_REG_GYRO_CONFIG  0x1B
#define MPU_REG_ACCEL_CONFIG 0x1C
#define MPU_REG_ACCEL_CONFIG2 0x1D
#define MPU_REG_PWR_MGMT_1   0x6B
#define MPU_REG_PWR_MGMT_2   0x6C
#define MPU_REG_WHO_AM_I     0x75
#define AVERAGE_SAMPLES 16

// Accelerometer zero-g bias for THIS unit, in m/s^2.
//
// Measured 2026-09-04 by sweeping the altitude axis from horizon to zenith and
// fitting the centre of the gravity circle. Fixing the circle's radius at 9.81
// is what makes that fit solvable from a single-plane sweep.
//
// This is a physical property of the individual chip, stable over years.
// Re-measure only if the sensor is replaced, the board is remounted, or
// pointing accuracy degrades. Uncorrected this bias skewed altitude by up to
// 6.5 degrees; corrected, the residual is under 0.6 degrees. Any constant
// offset that remains is absorbed by SkySafari's own alignment.
#define ACCEL_BIAS_X  0.5944f
#define ACCEL_BIAS_Y  0.5499f
#define ACCEL_BIAS_Z -1.0806f

// Consecutive failed probes before a sensor is declared gone. The long Cat5
// run to the sensor head glitches occasionally; dropping an axis on a single
// bad read would surface in SkySafari as a jump.
#define SENSOR_ABSENT_THRESHOLD 2

// Latest reading of both physical axes, in sensor-native and normalized form.
struct SensorSample {
    bool azimuthValid;
    uint16_t azimuthRawTicks;   // AS5600 raw count, 0..4095
    float azimuthDegrees;       // 0..360, clockwise from above

    bool altitudeValid;
    float altitudeRawDegrees;   // Instantaneous accelerometer pitch
    float altitudeDegrees;      // Rolling-averaged pitch, -90..+90
    float accelX;               // Raw gravity vector, m/s^2, for calibration
    float accelY;
    float accelZ;

    float gravityMagnitude;     // |g| after bias removal; must stay near 9.81
    uint8_t averagedSamples;    // How full the rolling-average buffer is
    uint32_t timestamp;         // millis() when this sample was taken
};

// Slow-moving sensor health, refreshed at a low rate rather than every sample.
struct SensorHealth {
    bool azimuthSensorPresent;
    bool altitudeSensorPresent;
    bool magnetDetected;
    bool magnetTooWeak;
    bool magnetTooStrong;
    uint8_t magnetAgc;
    uint16_t magnetMagnitude;
};

// Owns the I2C sensor hardware. Not thread-safe: all methods are intended to
// be called from a single sampling task, which publishes the results into
// TelescopeState for everything else to read.
class SensorManager {
public:
    SensorManager();

    bool begin(uint8_t sda, uint8_t scl, uint32_t i2cFreq = 100000);

    // Take one reading of both axes. Call at the sampling rate.
    void update();

    // Re-probe the bus for hot-plugged sensors and refresh health. Slow;
    // call at a low rate (a couple of times per second at most).
    void refreshHealth();

    SensorSample getSample() const;
    SensorHealth getHealth() const;

    // Multi-line human-readable summary for serial logging.
    String describe() const;

private:
    AS5600 _as5600;
    Adafruit_MPU6050 _mpu6050;

    // Rolling average buffer for altitude
    float _altitudeSamples[AVERAGE_SAMPLES];
    int _sampleIndex;
    int _sampleCount;

    SensorSample _sample;
    SensorHealth _health;

    uint8_t _mpuWhoAmI;

    bool _as5600Initialized;
    bool _mpu6050Initialized;
    uint8_t _as5600Misses;
    uint8_t _mpu6050Misses;

    bool initAS5600();
    bool initMPU6050();
    bool probeMPU6050();
    void dumpMpuConfig();
    bool writeMpuRegister(uint8_t reg, uint8_t value);
    uint8_t readMpuRegister(uint8_t reg);
    bool configureMpu();
    static const char* describeMpuId(uint8_t whoAmI);

    float calculateAltitudeFromAccel(float ax, float ay, float az) const;
    float getAveragedAltitude() const;
    void clearAltitudeAverage();
};

#endif // SENSOR_MANAGER_H
