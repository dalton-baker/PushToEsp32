#ifndef AS5600_H
#define AS5600_H

#include <Arduino.h>
#include <Wire.h>

#define AS5600_ADDRESS 0x36
#define AS5600_RAW_ANGLE_REG 0x0C
#define AS5600_ANGLE_REG 0x0E
#define AS5600_STATUS_REG 0x0B
#define AS5600_AGC_REG 0x1A
#define AS5600_MAGNITUDE_REG 0x1B

class AS5600 {
public:
    AS5600(TwoWire *wire = &Wire);
    
    bool begin();
    bool isConnected();
    
    // Read raw angle (0-4095, 12-bit)
    uint16_t getRawAngle();
    
    // Read angle in degrees (0-360)
    float getAngleDegrees();
    
    // Read angle in radians (0-2π)
    float getAngleRadians();
    
    // Status information
    uint8_t getStatus();
    bool isMagnetDetected();
    bool isMagnetTooWeak();
    bool isMagnetTooStrong();
    
    // AGC and magnitude (for diagnostics)
    uint8_t getAGC();
    uint16_t getMagnitude();
    
    // Zero position setting
    void setZeroPosition(uint16_t position);
    uint16_t getZeroPosition();
    
private:
    TwoWire *_wire;
    uint16_t _zeroPosition;
    
    uint16_t readRegister16(uint8_t reg);
    uint8_t readRegister8(uint8_t reg);
    void writeRegister16(uint8_t reg, uint16_t value);
};

#endif // AS5600_H
