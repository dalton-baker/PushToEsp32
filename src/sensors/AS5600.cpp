#include "AS5600.h"

AS5600::AS5600(TwoWire *wire) {
    _wire = wire;
}

bool AS5600::begin() {
    if (!isConnected()) {
        return false;
    }

    // Check if magnet is detected
    delay(10); // Give sensor time to settle
    return isMagnetDetected();
}

bool AS5600::isConnected() {
    _wire->beginTransmission(AS5600_ADDRESS);
    return (_wire->endTransmission() == 0);
}

uint16_t AS5600::getRawAngle() {
    return readRegister16(AS5600_RAW_ANGLE_REG) & 0x0FFF;
}

float AS5600::getAngleDegrees() {
    return (getRawAngle() * 360.0f) / AS5600_COUNTS_PER_REV;
}

uint8_t AS5600::getStatus() {
    return readRegister8(AS5600_STATUS_REG);
}

bool AS5600::isMagnetDetected() {
    return (getStatus() & 0x20) != 0; // MD bit
}

bool AS5600::isMagnetTooWeak() {
    return (getStatus() & 0x10) != 0; // ML bit
}

bool AS5600::isMagnetTooStrong() {
    return (getStatus() & 0x08) != 0; // MH bit
}

uint8_t AS5600::getAGC() {
    return readRegister8(AS5600_AGC_REG);
}

uint16_t AS5600::getMagnitude() {
    return readRegister16(AS5600_MAGNITUDE_REG);
}

uint16_t AS5600::readRegister16(uint8_t reg) {
    _wire->beginTransmission(AS5600_ADDRESS);
    _wire->write(reg);
    _wire->endTransmission(false);

    _wire->requestFrom(AS5600_ADDRESS, 2);
    if (_wire->available() >= 2) {
        uint16_t high = _wire->read();
        uint16_t low = _wire->read();
        return (high << 8) | low;
    }
    return 0;
}

uint8_t AS5600::readRegister8(uint8_t reg) {
    _wire->beginTransmission(AS5600_ADDRESS);
    _wire->write(reg);
    _wire->endTransmission(false);

    _wire->requestFrom(AS5600_ADDRESS, 1);
    if (_wire->available()) {
        return _wire->read();
    }
    return 0;
}
