#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <Preferences.h>

struct AlignmentStar {
    double ra;           // Right Ascension (hours)
    double dec;          // Declination (degrees)
    float az;            // Measured azimuth at alignment (degrees)
    float alt;           // Measured altitude at alignment (degrees)
    float az_offset;     // expected_az - measured_az (deg, signed)
    float alt_offset;    // expected_alt - measured_alt (deg, signed)
    float expected_az;   // Catalog azimuth at the moment this star was synced
    float expected_alt;  // Catalog altitude at the moment this star was synced
    bool valid;          // True if this star slot has been populated
};

struct SiteConfig {
    double latitude;   // Site latitude (degrees, positive north)
    double longitude;  // Site longitude (degrees, positive east)
    bool valid;
};

class Config {
public:
    Config();
    
    void begin();
    void save();
    void load();
    void reset();
    
    // Site configuration
    void setSite(double lat, double lon);
    SiteConfig getSite();
    
    // Alignment stars
    void setAlignmentStar(int starNum, double ra, double dec, float az, float alt, const String& name = "");
    void setAlignmentStarOffsets(int starNum, float az_offset, float alt_offset, float exp_az, float exp_alt);
    AlignmentStar getAlignmentStar(int starNum);
    String getAlignmentStarName(int starNum);
    bool isAligned();
    void clearAlignment();
    
    // WiFi credentials (for optional client mode)
    void setWiFi(const char* ssid, const char* password);
    String getWiFiSSID();
    String getWiFiPassword();
    
    // Device info
    String getDeviceName();
    void setDeviceName(const char* name);
    
private:
    Preferences prefs;
    SiteConfig site;
    AlignmentStar stars[2];
    String starNames[2];
    String deviceName;
    String wifiSSID;
    String wifiPassword;
    bool aligned;
};

#endif // CONFIG_H
