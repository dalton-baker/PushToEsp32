#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <Preferences.h>

struct AlignmentStar {
    double ra;      // Right Ascension (hours)
    double dec;     // Declination (degrees)
    float az;       // Azimuth at alignment (degrees)
    float alt;      // Altitude at alignment (degrees)
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
    void setAlignmentStar(int starNum, double ra, double dec, float az, float alt);
    AlignmentStar getAlignmentStar(int starNum);
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
    String deviceName;
    String wifiSSID;
    String wifiPassword;
    bool aligned;
};

#endif // CONFIG_H
