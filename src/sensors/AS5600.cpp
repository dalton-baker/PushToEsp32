#include "AS5600.h"

AS5600::AS5600(TwoWire *wire) {
    _wire = wire;
    _zeroPosition = 0;
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
    return readRegister16(AS5600_RAW_ANGLE_REG);
}

float AS5600::getAngleDegrees() {
    uint16_t raw = getRawAngle();
    // Apply zero position offset
    int32_t adjusted = (int32_t)raw - (int32_t)_zeroPosition;
    if (adjusted < 0) adjusted += 4096;
    
    // Convert to degrees (4096 = 360 degrees)
    return (adjusted * 360.0) / 4096.0;
}

float AS5600::getAngleRadians() {
    return getAngleDegrees() * (PI / 180.0);
}

uint8_t AS5600::getStatus() {
    return readRegister8(AS5600_STATUS_REG);
}

bool AS5600::isMagnetDetected() {
    uint8_t status = getStatus();
    return (status & 0x20) != 0; // MD bit
}

bool AS5600::isMagnetTooWeak() {
    uint8_t status = getStatus();
    return (status & 0x10) != 0; // ML bit
}

bool AS5600::isMagnetTooStrong() {
    uint8_t status = getStatus();
    return (status & 0x08) != 0; // MH bit
}

uint8_t AS5600::getAGC() {
    return readRegister8(AS5600_AGC_REG);
}

uint16_t AS5600::getMagnitude() {
    return readRegister16(AS5600_MAGNITUDE_REG);
}

void AS5600::setZeroPosition(uint16_t position) {
    _zeroPosition = position;
}

uint16_t AS5600::getZeroPosition() {
    return _zeroPosition;
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

void AS5600::writeRegister16(uint8_t reg, uint16_t value) {
    _wire->beginTransmission(AS5600_ADDRESS);
    _wire->write(reg);
    _wire->write((value >> 8) & 0xFF);
    _wire->write(value & 0xFF);
    _wire->endTransmission();
}
