#include "Config.h"

Config::Config() {
    site.latitude = 0.0;
    site.longitude = 0.0;
    site.valid = false;
    aligned = false;
    deviceName = "PushTo-ESP32";
}

void Config::begin() {
    prefs.begin("pushto", false);
    load();
}

void Config::save() {
    // Save site configuration
    prefs.putDouble("lat", site.latitude);
    prefs.putDouble("lon", site.longitude);
    prefs.putBool("site_valid", site.valid);
    
    // Save alignment stars
    prefs.putBool("aligned", aligned);
    for (int i = 0; i < 2; i++) {
        String prefix = "star" + String(i) + "_";
        prefs.putDouble((prefix + "ra").c_str(), stars[i].ra);
        prefs.putDouble((prefix + "dec").c_str(), stars[i].dec);
        prefs.putFloat((prefix + "az").c_str(), stars[i].az);
        prefs.putFloat((prefix + "alt").c_str(), stars[i].alt);
    }
    
    // Save WiFi and device name
    prefs.putString("wifi_ssid", wifiSSID);
    prefs.putString("wifi_pass", wifiPassword);
    prefs.putString("device_name", deviceName);
}

void Config::load() {
    // Load site configuration
    site.latitude = prefs.getDouble("lat", 0.0);
    site.longitude = prefs.getDouble("lon", 0.0);
    site.valid = prefs.getBool("site_valid", false);
    
    // Load alignment stars
    aligned = prefs.getBool("aligned", false);
    for (int i = 0; i < 2; i++) {
        String prefix = "star" + String(i) + "_";
        stars[i].ra = prefs.getDouble((prefix + "ra").c_str(), 0.0);
        stars[i].dec = prefs.getDouble((prefix + "dec").c_str(), 0.0);
        stars[i].az = prefs.getFloat((prefix + "az").c_str(), 0.0);
        stars[i].alt = prefs.getFloat((prefix + "alt").c_str(), 0.0);
    }
    
    // Load WiFi and device name
    wifiSSID = prefs.getString("wifi_ssid", "");
    wifiPassword = prefs.getString("wifi_pass", "");
    deviceName = prefs.getString("device_name", "PushTo-ESP32");
}

void Config::reset() {
    prefs.clear();
    site.valid = false;
    aligned = false;
    save();
}

void Config::setSite(double lat, double lon) {
    site.latitude = lat;
    site.longitude = lon;
    site.valid = true;
    save();
}

SiteConfig Config::getSite() {
    return site;
}

void Config::setAlignmentStar(int starNum, double ra, double dec, float az, float alt) {
    if (starNum >= 0 && starNum < 2) {
        stars[starNum].ra = ra;
        stars[starNum].dec = dec;
        stars[starNum].az = az;
        stars[starNum].alt = alt;
        
        // Mark as aligned if both stars are set
        if (stars[0].ra != 0.0 && stars[1].ra != 0.0) {
            aligned = true;
        }
        save();
    }
}

AlignmentStar Config::getAlignmentStar(int starNum) {
    if (starNum >= 0 && starNum < 2) {
        return stars[starNum];
    }
    return AlignmentStar{0, 0, 0, 0};
}

bool Config::isAligned() {
    return aligned;
}

void Config::clearAlignment() {
    aligned = false;
    for (int i = 0; i < 2; i++) {
        stars[i] = {0, 0, 0, 0};
    }
    save();
}

void Config::setWiFi(const char* ssid, const char* password) {
    wifiSSID = ssid;
    wifiPassword = password;
    save();
}

String Config::getWiFiSSID() {
    return wifiSSID;
}

String Config::getWiFiPassword() {
    return wifiPassword;
}

String Config::getDeviceName() {
    return deviceName;
}

void Config::setDeviceName(const char* name) {
    deviceName = name;
    save();
}
