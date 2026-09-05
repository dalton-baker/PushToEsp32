#ifndef AS5600_H
#define AS5600_H

#include <Arduino.h>
#include <Wire.h>

#define AS5600_ADDRESS 0x36
#define AS5600_RAW_ANGLE_REG 0x0C
#define AS5600_STATUS_REG 0x0B
#define AS5600_AGC_REG 0x1A
#define AS5600_MAGNITUDE_REG 0x1B

// Counts per revolution of the AS5600's 12-bit output.
#define AS5600_COUNTS_PER_REV 4096

class AS5600 {
public:
    AS5600(TwoWire *wire = &Wire);

    bool begin();
    bool isConnected();

    // Read raw angle (0-4095, 12-bit)
    uint16_t getRawAngle();

    // Read angle in degrees (0-360)
    float getAngleDegrees();

    // Status information
    uint8_t getStatus();
    bool isMagnetDetected();
    bool isMagnetTooWeak();
    bool isMagnetTooStrong();

    // AGC and magnitude (for diagnostics)
    uint8_t getAGC();
    uint16_t getMagnitude();

private:
    TwoWire *_wire;

    uint16_t readRegister16(uint8_t reg);
    uint8_t readRegister8(uint8_t reg);
};

#endif // AS5600_H
