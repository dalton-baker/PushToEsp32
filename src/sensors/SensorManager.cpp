#include "SensorManager.h"

SensorManager::SensorManager() : as5600(&Wire) {
    sampleIndex = 0;
    sampleCount = 0;
    currentAzimuth = 0.0;
    currentAltitude = 0.0;
    altitudeOffset = 0.0;
    as5600Initialized = false;
    mpu6050Initialized = false;
    
    // Initialize sample buffer
    for (int i = 0; i < AVERAGE_SAMPLES; i++) {
        altitudeSamples[i] = 0.0;
    }
}

bool SensorManager::begin(uint8_t sda, uint8_t scl, uint32_t i2cFreq) {
    Serial.println("[SensorMgr] Initializing I2C...");
    
    // Initialize I2C with custom pins and frequency
    Wire.begin(sda, scl);
    Wire.setClock(i2cFreq);
    
    Serial.println("[SensorMgr] I2C initialized, waiting for sensors...");
    delay(500); // Give sensors more time to power up
    
    // Initialize AS5600
    Serial.println("[SensorMgr] Attempting AS5600 init...");
    as5600.begin(); // Call begin even if it fails
    
    // Check if AS5600 is actually responding via I2C
    delay(50);
    as5600Initialized = as5600.isConnected();
    if (!as5600Initialized) {
        Serial.println("[SensorMgr] AS5600 NOT responding to I2C!");
    } else {
        Serial.println("[SensorMgr] AS5600 detected and responding!");
        Serial.print("[SensorMgr] AS5600 raw angle: ");
        Serial.println(as5600.getRawAngle());
    }
    
    delay(100);
    
    // Initialize MPU6050
    Serial.println("[SensorMgr] Attempting MPU6050 init...");
    mpu6050.begin(0x68, &Wire); // Call begin even if it fails
    
    // Check if MPU6050 is actually responding via I2C
    delay(50);
    Wire.beginTransmission(0x68);
    mpu6050Initialized = (Wire.endTransmission() == 0);
    
    if (!mpu6050Initialized) {
        Serial.println("[SensorMgr] MPU6050 NOT responding to I2C!");
    } else {
        Serial.println("[SensorMgr] MPU6050 detected and responding!");
        
        // Configure MPU6050
        mpu6050.setAccelerometerRange(MPU6050_RANGE_2_G);
        mpu6050.setGyroRange(MPU6050_RANGE_250_DEG);
        mpu6050.setFilterBandwidth(MPU6050_BAND_21_HZ);
    }
    
    Serial.print("[SensorMgr] Initialization complete - AS5600: ");
    Serial.print(as5600Initialized ? "OK" : "FAILED");
    Serial.print(", MPU6050: ");
    Serial.println(mpu6050Initialized ? "OK" : "FAILED");
    
    return as5600Initialized || mpu6050Initialized; // Return true if at least one works
}

void SensorManager::readSensors() {
    // Read azimuth from AS5600 if initialized
    if (as5600Initialized) {
        currentAzimuth = as5600.getAngleDegrees();
    }
    
    // Read acceleration from MPU6050 if initialized
    if (mpu6050Initialized) {
        sensors_event_t a, g, temp;
        mpu6050.getEvent(&a, &g, &temp);
    
        // Calculate altitude from accelerometer
        float rawAltitude = calculateAltitudeFromAccel(a.acceleration.x, 
                                                         a.acceleration.y, 
                                                         a.acceleration.z);
        
        // Apply offset
        rawAltitude -= altitudeOffset;
        
        // Add to rolling average buffer
        altitudeSamples[sampleIndex] = rawAltitude;
        sampleIndex = (sampleIndex + 1) % AVERAGE_SAMPLES;
        if (sampleCount < AVERAGE_SAMPLES) {
            sampleCount++;
        }
        
        // Update averaged altitude
        currentAltitude = getAveragedAltitude();
    }
}

TelescopePosition SensorManager::getPosition() {
    TelescopePosition pos;
    pos.azimuth = currentAzimuth;
    pos.altitude = currentAltitude;
    pos.valid = (as5600Initialized || mpu6050Initialized);
    pos.timestamp = millis();
    return pos;
}

void SensorManager::calibrateAltitude() {
    if (!mpu6050Initialized) return;
    
    // Read current altitude and set as zero offset
    sensors_event_t a, g, temp;
    mpu6050.getEvent(&a, &g, &temp);
    
    altitudeOffset = calculateAltitudeFromAccel(a.acceleration.x, 
                                                  a.acceleration.y, 
                                                  a.acceleration.z);
    
    Serial.print("Altitude calibrated. Offset: ");
    Serial.println(altitudeOffset);
}

void SensorManager::setAzimuthZero() {
    if (!as5600Initialized) return;
    
    uint16_t currentRaw = as5600.getRawAngle();
    as5600.setZeroPosition(currentRaw);
    
    Serial.print("Azimuth zero set at raw value: ");
    Serial.println(currentRaw);
}

bool SensorManager::isAS5600Connected() {
    return as5600.isConnected();
}

bool SensorManager::isMPU6050Connected() {
    Wire.beginTransmission(0x68);
    return (Wire.endTransmission() == 0);
}

bool SensorManager::isMagnetOK() {
    if (!as5600Initialized) return false;
    return as5600.isMagnetDetected() && 
           !as5600.isMagnetTooWeak() && 
           !as5600.isMagnetTooStrong();
}

String SensorManager::getDiagnostics() {
    String diag = "Sensor Diagnostics:\n";
    diag += "AS5600: " + String(isAS5600Connected() ? "Connected" : "Disconnected") + "\n";
    diag += "MPU6050: " + String(isMPU6050Connected() ? "Connected" : "Disconnected") + "\n";
    
    if (isAS5600Connected()) {
        diag += "Magnet Detected: " + String(as5600.isMagnetDetected() ? "Yes" : "No") + "\n";
        diag += "Magnet Too Weak: " + String(as5600.isMagnetTooWeak() ? "Yes" : "No") + "\n";
        diag += "Magnet Too Strong: " + String(as5600.isMagnetTooStrong() ? "Yes" : "No") + "\n";
        diag += "AGC: " + String(as5600.getAGC()) + "\n";
        diag += "Magnitude: " + String(as5600.getMagnitude()) + "\n";
    }
    
    diag += "Current Azimuth: " + String(currentAzimuth, 2) + "°\n";
    diag += "Current Altitude: " + String(currentAltitude, 2) + "°\n";
    diag += "Sample Count: " + String(sampleCount) + "/" + String(AVERAGE_SAMPLES) + "\n";
    
    return diag;
}

float SensorManager::calculateAltitudeFromAccel(float ax, float ay, float az) {
    // Calculate pitch angle from accelerometer
    // Assuming the sensor is mounted on the tube with Y-axis along tube
    // and Z-axis perpendicular to tube (pointing up when level)
    
    float pitch = atan2(-ax, sqrt(ay * ay + az * az)) * 180.0 / PI;
    return pitch;
}

float SensorManager::getAveragedAltitude() {
    if (sampleCount == 0) return 0.0;
    
    float sum = 0.0;
    for (int i = 0; i < sampleCount; i++) {
        sum += altitudeSamples[i];
    }
    return sum / sampleCount;
}
