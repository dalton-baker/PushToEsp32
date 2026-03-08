#include "Coordinates.h"
#include <math.h>

// Use Arduino's built-in macros
// DEG_TO_RAD and RAD_TO_DEG are already defined in Arduino.h
#define HOURS_TO_RAD (PI / 12.0)
#define RAD_TO_HOURS (12.0 / PI)

Coordinates::Coordinates(Config* config, SensorManager* sensors) {
    _config = config;
    _sensors = sensors;
    _alignmentValid = false;
}

EquatorialCoords Coordinates::horizontalToEquatorial(HorizontalCoords horiz, time_t time) {
    SiteConfig site = _config->getSite();
    
    // Convert to radians
    double az_rad = horiz.az * DEG_TO_RAD;
    double alt_rad = horiz.alt * DEG_TO_RAD;
    double lat_rad = site.latitude * DEG_TO_RAD;
    
    // Calculate declination
    double dec_rad = asin(sin(alt_rad) * sin(lat_rad) + 
                          cos(alt_rad) * cos(lat_rad) * cos(az_rad));
    
    // Calculate hour angle
    double ha_rad = atan2(-sin(az_rad) * cos(alt_rad),
                          cos(lat_rad) * sin(alt_rad) - sin(lat_rad) * cos(alt_rad) * cos(az_rad));
    
    // Convert hour angle to right ascension
    double lst = getLocalSiderealTime(time);
    double ha_hours = ha_rad * RAD_TO_HOURS;
    double ra_hours = lst - ha_hours;
    
    // Normalize
    ra_hours = normalizeRA(ra_hours);
    double dec_deg = normalizeDec(dec_rad * RAD_TO_DEG);
    
    return {ra_hours, dec_deg};
}

HorizontalCoords Coordinates::equatorialToHorizontal(EquatorialCoords eq, time_t time) {
    SiteConfig site = _config->getSite();
    
    // Convert to radians
    double ra_rad = eq.ra * HOURS_TO_RAD;
    double dec_rad = eq.dec * DEG_TO_RAD;
    double lat_rad = site.latitude * DEG_TO_RAD;
    
    // Calculate hour angle
    double lst = getLocalSiderealTime(time);
    double ha_hours = lst - eq.ra;
    double ha_rad = ha_hours * HOURS_TO_RAD;
    
    // Calculate altitude
    double alt_rad = asin(sin(dec_rad) * sin(lat_rad) + 
                          cos(dec_rad) * cos(lat_rad) * cos(ha_rad));
    
    // Calculate azimuth
    double az_rad = atan2(-sin(ha_rad) * cos(dec_rad),
                          cos(lat_rad) * sin(dec_rad) - sin(lat_rad) * cos(dec_rad) * cos(ha_rad));
    
    // Convert to degrees and normalize
    float az_deg = normalizeAz(az_rad * RAD_TO_DEG);
    float alt_deg = normalizeAlt(alt_rad * RAD_TO_DEG);
    
    return {az_deg, alt_deg};
}

EquatorialCoords Coordinates::getCurrentPosition() {
    TelescopePosition pos = _sensors->getPosition();
    
    if (!pos.valid) {
        return {0.0, 0.0};
    }
    
    HorizontalCoords horiz = {pos.azimuth, pos.altitude};
    
    // Apply alignment correction if available
    if (_alignmentValid) {
        horiz = applyAlignmentCorrection(horiz);
    }
    
    // Convert to equatorial coordinates
    time_t now = time(nullptr);
    return horizontalToEquatorial(horiz, now);
}

TelescopePosition Coordinates::getRawPosition() {
    return _sensors->getPosition();
}

void Coordinates::performAlignment() {
    if (_config->isAligned()) {
        computeAlignmentMatrix();
        _alignmentValid = true;
    }
}

bool Coordinates::isAligned() {
    return _alignmentValid && _config->isAligned();
}

double Coordinates::getLocalSiderealTime(time_t time) {
    double jd = getJulianDate(time);
    
    // Calculate Greenwich Mean Sidereal Time (GMST)
    double T = (jd - 2451545.0) / 36525.0;
    double gmst = 280.46061837 + 360.98564736629 * (jd - 2451545.0) + 
                  0.000387933 * T * T - (T * T * T / 38710000.0);
    
    // Normalize to 0-360
    while (gmst < 0) gmst += 360.0;
    while (gmst >= 360.0) gmst -= 360.0;
    
    // Convert to hours
    gmst /= 15.0;
    
    // Add longitude to get Local Sidereal Time
    SiteConfig site = _config->getSite();
    double lst = gmst + (site.longitude / 15.0);
    
    return normalizeRA(lst);
}

double Coordinates::getHourAngle(double ra, time_t time) {
    double lst = getLocalSiderealTime(time);
    double ha = lst - ra;
    return normalizeRA(ha);
}

double Coordinates::getJulianDate(time_t time) {
    // Convert Unix timestamp to Julian Date
    return (time / 86400.0) + 2440587.5;
}

double Coordinates::normalizeRA(double ra) {
    while (ra < 0.0) ra += 24.0;
    while (ra >= 24.0) ra -= 24.0;
    return ra;
}

double Coordinates::normalizeDec(double dec) {
    if (dec > 90.0) return 90.0;
    if (dec < -90.0) return -90.0;
    return dec;
}

double Coordinates::normalizeAz(double az) {
    while (az < 0.0) az += 360.0;
    while (az >= 360.0) az -= 360.0;
    return az;
}

double Coordinates::normalizeAlt(double alt) {
    if (alt > 90.0) return 90.0;
    if (alt < -90.0) return -90.0;
    return alt;
}

void Coordinates::computeAlignmentMatrix() {
    // Simplified two-star alignment
    // In a full implementation, this would compute a transformation matrix
    // based on the difference between observed and actual star positions
    // For now, we'll use a simple identity matrix
    
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            _alignmentMatrix[i][j] = (i == j) ? 1.0 : 0.0;
        }
    }
    
    // TODO: Implement proper two-star alignment matrix calculation
    // This requires solving for rotation and translation between
    // the observed horizontal coordinates and the expected coordinates
    // for the two alignment stars
}

HorizontalCoords Coordinates::applyAlignmentCorrection(HorizontalCoords raw) {
    // Apply alignment correction matrix
    // For simplified version, just return raw coordinates
    // TODO: Implement matrix transformation
    return raw;
}
