#ifndef COORDINATES_H
#define COORDINATES_H

#include <Arduino.h>
#include <time.h>
#include "../config/Config.h"
#include "../sensors/SensorManager.h"

struct EquatorialCoords {
    double ra;      // Right Ascension in hours (0-24)
    double dec;     // Declination in degrees (-90 to +90)
};

struct HorizontalCoords {
    float az;       // Azimuth in degrees (0-360)
    float alt;      // Altitude in degrees (-90 to +90)
};

class Coordinates {
public:
    Coordinates(Config* config, SensorManager* sensors);
    
    // Convert horizontal to equatorial coordinates
    EquatorialCoords horizontalToEquatorial(HorizontalCoords horiz, time_t time);
    
    // Convert equatorial to horizontal coordinates
    HorizontalCoords equatorialToHorizontal(EquatorialCoords eq, time_t time);
    
    // Get current telescope pointing coordinates (RA/Dec)
    EquatorialCoords getCurrentPosition();
    
    // Get raw sensor position
    TelescopePosition getRawPosition();
    
    // Two-star alignment transformation
    void performAlignment();
    bool isAligned();
    
    // Calculate Local Sidereal Time
    double getLocalSiderealTime(time_t time);
    
    // Calculate Hour Angle
    double getHourAngle(double ra, time_t time);
    
    // Julian Date calculations
    double getJulianDate(time_t time);
    
private:
    Config* _config;
    SensorManager* _sensors;
    
    // Alignment matrices (for two-star alignment correction)
    bool _alignmentValid;
    float _alignmentMatrix[3][3];
    
    // Helper functions
    double normalizeRA(double ra);      // Normalize RA to 0-24 hours
    double normalizeDec(double dec);    // Clamp Dec to -90 to +90
    double normalizeAz(double az);      // Normalize Az to 0-360
    double normalizeAlt(double alt);    // Clamp Alt to -90 to +90
    
    void computeAlignmentMatrix();
    HorizontalCoords applyAlignmentCorrection(HorizontalCoords raw);
};

#endif // COORDINATES_H
