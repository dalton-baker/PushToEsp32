#include "Coordinates.h"
#include <math.h>

// Use Arduino's built-in macros
// DEG_TO_RAD and RAD_TO_DEG are already defined in Arduino.h
#define HOURS_TO_RAD (PI / 12.0)
#define RAD_TO_HOURS (12.0 / PI)

// ---------------------------------------------------------------------------
// Internal geometry helpers (file-scope, not exported)
// ---------------------------------------------------------------------------

// Convert alt/az (degrees) to a unit vector in local horizontal frame:
//   v[0] = East  (cos(alt)*sin(az))
//   v[1] = North (cos(alt)*cos(az))
//   v[2] = Up    (sin(alt))
static void altAzToUnitVec(float alt_deg, float az_deg, double v[3]) {
    double alt_r = alt_deg * DEG_TO_RAD;
    double az_r  = az_deg  * DEG_TO_RAD;
    v[0] = cos(alt_r) * sin(az_r);
    v[1] = cos(alt_r) * cos(az_r);
    v[2] = sin(alt_r);
}

static void normalize3(double v[3]) {
    double len = sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (len > 1e-10) { v[0] /= len; v[1] /= len; v[2] /= len; }
}

static void cross3(const double a[3], const double b[3], double out[3]) {
    out[0] = a[1]*b[2] - a[2]*b[1];
    out[1] = a[2]*b[0] - a[0]*b[2];
    out[2] = a[0]*b[1] - a[1]*b[0];
}

Coordinates::Coordinates(Config* config, SensorManager* sensors) {
    _config = config;
    _sensors = sensors;
    _alignmentValid = false;
    _useRotationMatrix = false;
    _altOffset = 0.0f;
    _azOffset = 0.0f;
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

HorizontalCoords Coordinates::getCorrectedHorizontal() {
    TelescopePosition pos = _sensors->getPosition();

    HorizontalCoords horiz = {pos.azimuth, pos.altitude};
    if (!pos.valid) {
        return horiz;
    }
    if (_alignmentValid) {
        horiz = applyAlignmentCorrection(horiz);
    }
    return horiz;
}

TelescopePosition Coordinates::getRawPosition() {
    return _sensors->getPosition();
}

void Coordinates::performAlignment() {
    if (_config->isAligned()) {
        computeAlignmentMatrix();
        _alignmentValid = true;
    } else {
        // No stars configured — make sure no stale correction lingers.
        _alignmentValid = false;
        _useRotationMatrix = false;
        _altOffset = 0.0f;
        _azOffset = 0.0f;
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
    AlignmentStar s0 = _config->getAlignmentStar(0);
    AlignmentStar s1 = _config->getAlignmentStar(1);

    _useRotationMatrix = false;
    _altOffset = 0.0f;
    _azOffset  = 0.0f;

    if (s0.valid && s1.valid) {
        // ---- 2-star: solve for the 3D rotation that maps sensor frame to sky ----
        // Build unit vectors for measured and expected (catalog) positions.
        double m0[3], m1[3], c0[3], c1[3];
        altAzToUnitVec(s0.alt, s0.az,              m0);
        altAzToUnitVec(s1.alt, s1.az,              m1);
        altAzToUnitVec(s0.expected_alt, s0.expected_az, c0);
        altAzToUnitVec(s1.expected_alt, s1.expected_az, c1);

        // Build orthonormal basis from the measured pair.
        // e1 along first star, e3 perpendicular to the plane, e2 completing right-hand frame.
        double e1[3], e2[3], e3[3];
        e1[0] = m0[0]; e1[1] = m0[1]; e1[2] = m0[2];
        normalize3(e1);
        cross3(m0, m1, e3);
        double crossLen = sqrt(e3[0]*e3[0] + e3[1]*e3[1] + e3[2]*e3[2]);
        if (crossLen < 1e-4) {
            // Stars too close together — degenerate, fall back to 1-star with s0
            _altOffset = s0.alt_offset;
            _azOffset  = s0.az_offset;
            return;
        }
        normalize3(e3);
        cross3(e3, e1, e2);

        // Build orthonormal basis from the catalog pair (same structure).
        double f1[3], f2[3], f3[3];
        f1[0] = c0[0]; f1[1] = c0[1]; f1[2] = c0[2];
        normalize3(f1);
        cross3(c0, c1, f3);
        normalize3(f3);
        cross3(f3, f1, f2);

        // R = F * E^T, where F = [f1|f2|f3] and E = [e1|e2|e3] as column matrices.
        // R[i][j] = f1[i]*e1[j] + f2[i]*e2[j] + f3[i]*e3[j]
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                _alignmentMatrix[i][j] = (float)(f1[i]*e1[j] + f2[i]*e2[j] + f3[i]*e3[j]);

        _useRotationMatrix = true;

    } else if (s0.valid) {
        // ---- 1-star: simple additive offset ----
        _altOffset = s0.alt_offset;
        _azOffset  = s0.az_offset;
    } else if (s1.valid) {
        _altOffset = s1.alt_offset;
        _azOffset  = s1.az_offset;
    }
}

HorizontalCoords Coordinates::applyAlignmentCorrection(HorizontalCoords raw) {
    if (_useRotationMatrix) {
        double v[3];
        altAzToUnitVec(raw.alt, raw.az, v);

        double w[3] = {0.0, 0.0, 0.0};
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                w[i] += (double)_alignmentMatrix[i][j] * v[j];

        // Clamp w[2] to [-1,1] to guard against floating-point drift into asin domain error
        if (w[2] >  1.0) w[2] =  1.0;
        if (w[2] < -1.0) w[2] = -1.0;

        HorizontalCoords c;
        c.alt = (float)normalizeAlt(asin(w[2]) * RAD_TO_DEG);
        c.az  = (float)normalizeAz(atan2(w[0], w[1]) * RAD_TO_DEG);
        return c;
    } else {
        HorizontalCoords c;
        c.alt = (float)normalizeAlt(raw.alt + _altOffset);
        c.az  = (float)normalizeAz(raw.az  + _azOffset);
        return c;
    }
}

bool Coordinates::captureAlignmentOffset(int starNum, double ra, double dec, time_t whenUtc) {
    if (starNum < 0 || starNum > 1) return false;
    TelescopePosition pos = _sensors->getPosition();
    if (!pos.valid) return false;

    // Compute where the star *should* be in alt/az right now.
    EquatorialCoords eq = { ra, dec };
    HorizontalCoords expected = equatorialToHorizontal(eq, whenUtc);

    // Additive offset (used by 1-star mode and stored for potential fallback).
    float altOff = expected.alt - pos.altitude;
    float azOff  = expected.az  - pos.azimuth;
    while (azOff >  180.0f) azOff -= 360.0f;
    while (azOff < -180.0f) azOff += 360.0f;

    // Store offset and the catalog position (needed to rebuild rotation matrix on reboot).
    _config->setAlignmentStarOffsets(starNum, azOff, altOff, expected.az, expected.alt);
    performAlignment();
    return true;
}
