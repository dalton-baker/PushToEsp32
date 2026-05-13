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
        prefs.putFloat((prefix + "azo").c_str(), stars[i].az_offset);
        prefs.putFloat((prefix + "alto").c_str(), stars[i].alt_offset);
        prefs.putFloat((prefix + "exp_az").c_str(), stars[i].expected_az);
        prefs.putFloat((prefix + "exp_alt").c_str(), stars[i].expected_alt);
        prefs.putBool((prefix + "valid").c_str(), stars[i].valid);
        prefs.putString((prefix + "name").c_str(), starNames[i]);
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
        stars[i].az_offset = prefs.getFloat((prefix + "azo").c_str(), 0.0);
        stars[i].alt_offset = prefs.getFloat((prefix + "alto").c_str(), 0.0);
        stars[i].expected_az = prefs.getFloat((prefix + "exp_az").c_str(), 0.0);
        stars[i].expected_alt = prefs.getFloat((prefix + "exp_alt").c_str(), 0.0);
        stars[i].valid = prefs.getBool((prefix + "valid").c_str(), false);
        starNames[i] = prefs.getString((prefix + "name").c_str(), "");
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

void Config::setAlignmentStar(int starNum, double ra, double dec, float az, float alt, const String& name) {
    if (starNum >= 0 && starNum < 2) {
        stars[starNum].ra = ra;
        stars[starNum].dec = dec;
        stars[starNum].az = az;
        stars[starNum].alt = alt;
        stars[starNum].az_offset = 0.0f;
        stars[starNum].alt_offset = 0.0f;
        stars[starNum].expected_az = 0.0f;
        stars[starNum].expected_alt = 0.0f;
        stars[starNum].valid = true;
        starNames[starNum] = name;

        // Aligned as soon as at least one star is set; refined when 2 are set
        aligned = stars[0].valid || stars[1].valid;
        save();
    }
}

void Config::setAlignmentStarOffsets(int starNum, float az_offset, float alt_offset, float exp_az, float exp_alt) {
    if (starNum >= 0 && starNum < 2) {
        stars[starNum].az_offset = az_offset;
        stars[starNum].alt_offset = alt_offset;
        stars[starNum].expected_az = exp_az;
        stars[starNum].expected_alt = exp_alt;
        save();
    }
}

AlignmentStar Config::getAlignmentStar(int starNum) {
    if (starNum >= 0 && starNum < 2) {
        return stars[starNum];
    }
    return AlignmentStar{0, 0, 0, 0, 0, 0, 0, 0, false};
}

String Config::getAlignmentStarName(int starNum) {
    if (starNum >= 0 && starNum < 2) {
        return starNames[starNum];
    }
    return "";
}

bool Config::isAligned() {
    return aligned;
}

void Config::clearAlignment() {
    aligned = false;
    for (int i = 0; i < 2; i++) {
        stars[i] = {0, 0, 0, 0, 0, 0, 0, 0, false};
        starNames[i] = "";
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
