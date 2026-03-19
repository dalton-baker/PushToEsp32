#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include "AS5600.h"

#define AVERAGE_SAMPLES 16

struct TelescopePosition {
    float azimuth;     // Azimuth in degrees (0-360)
    float altitude;    // Altitude in degrees (-90 to +90)
    bool valid;
    uint32_t timestamp;
};

class SensorManager {
public:
    SensorManager();
    
    bool begin(uint8_t sda, uint8_t scl, uint32_t i2cFreq = 100000);
    
    // Reading sensors (call at high rate, e.g., 100Hz)
    void readSensors();
    
    // Get current position (averaged)
    TelescopePosition getPosition();
    
    // Calibration
    void calibrateAltitude();  // Set current orientation as level
    void setAzimuthZero();     // Set current azimuth as zero
    
    // Diagnostics
    bool isAS5600Connected();
    bool isMPU6050Connected();
    bool isMagnetOK();
    
    // Hot-plug: check and reinitialize disconnected/reconnected devices
    void checkConnections();
    
    String getDiagnostics();
    
private:
    AS5600 as5600;
    Adafruit_MPU6050 mpu6050;
    
    // Rolling average buffers
    float altitudeSamples[AVERAGE_SAMPLES];
    int sampleIndex;
    int sampleCount;
    
    // Current values
    float currentAzimuth;
    float currentAltitude;
    float altitudeOffset;
    
    bool as5600Initialized;
    bool mpu6050Initialized;
    
    // Helper functions
    float calculateAltitudeFromAccel(float ax, float ay, float az);
    float getAveragedAltitude();
    
    bool initAS5600();
    bool initMPU6050();
};

#endif // SENSOR_MANAGER_H
