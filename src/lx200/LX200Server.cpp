#include "LX200Server.h"
#include <time.h>
#include <sys/time.h>

LX200Server::LX200Server(Coordinates* coords, Config* config) {
    _coords = coords;
    _config = config;
    server = nullptr;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        bufferPos[i] = 0;
        commandBuffer[i][0] = '\0';
    }
    targetRA = 0.0;
    targetDec = 0.0;
    pendingHour = 0;
    pendingMinute = 0;
    pendingSecond = 0;
    timeSet = false;
}

void LX200Server::begin() {
    server = new WiFiServer(LX200_PORT);
    server->begin();
    Serial.print("LX200 server started on port ");
    Serial.println(LX200_PORT);
}

void LX200Server::handle() {
    if (!server) return;
    
    // Check for new clients
    if (server->hasClient()) {
        bool accepted = false;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i] || !clients[i].connected()) {
                if (clients[i]) clients[i].stop();
                clients[i] = server->available();
                bufferPos[i] = 0; // Clear buffer for new client
                commandBuffer[i][0] = '\0';
                Serial.print("New LX200 client connected: ");
                Serial.println(i);
                accepted = true;
                break;
            }
        }
        if (!accepted) {
            server->available().stop(); // Reject if no slots
        }
    }
    
    // Handle existing clients
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && clients[i].connected()) {
            while (clients[i].available()) {
                char c = clients[i].read();
                
                // Check for ACK byte (0x06) - process immediately without waiting for '#'
                if (c == 0x06) {
                    commandBuffer[i][0] = c;
                    commandBuffer[i][1] = '\0';
                    processCommand(clients[i], String(commandBuffer[i]));
                    bufferPos[i] = 0;
                } else if (c == '#') {
                    // End of command
                    commandBuffer[i][bufferPos[i]] = '\0';
                    processCommand(clients[i], String(commandBuffer[i]));
                    bufferPos[i] = 0;
                } else if (bufferPos[i] < 63) {
                    commandBuffer[i][bufferPos[i]++] = c;
                }
            }
        }
    }
}

bool LX200Server::isClientConnected() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && clients[i].connected()) {
            return true;
        }
    }
    return false;
}

int LX200Server::getClientCount() {
    int count = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && clients[i].connected()) {
            count++;
        }
    }
    return count;
}

void LX200Server::processCommand(WiFiClient& client, String cmd) {
    Serial.print("LX200 command: ");
    Serial.print(cmd);
    Serial.print(" (len=");
    Serial.print(cmd.length());
    Serial.print(", bytes=");
    for (int i = 0; i < cmd.length(); i++) {
        Serial.print((int)cmd[i]);
        Serial.print(" ");
    }
    Serial.println(")");
    
    // Handle ACK (0x06) - sent by Stellarium to identify LX200
    if (cmd.length() == 1 && cmd[0] == 0x06) {
        client.write("P"); // P for Polar mode (or 'A' for Alt-Az)
        client.flush();
        Serial.println("-> Sent 'P' (LX200 identification)");
        return;
    }
    
    // Ignore empty commands (from double ##)
    if (cmd.length() == 0) {
        return;
    }
    
    if (cmd.startsWith(":GR")) {
        handleGetRA(client);
    } else if (cmd.startsWith(":GD")) {
        handleGetDec(client);
    } else if (cmd.startsWith(":GA")) {
        handleGetAltitude(client);
    } else if (cmd.startsWith(":GZ")) {
        handleGetAzimuth(client);
    } else if (cmd.startsWith(":Ka")) {
        // Keep aligned / start constant update
        Serial.println("-> Keeping aligned (OK)");
        // No response needed for Ka command
    } else if (cmd.startsWith(":D")) {
        // Distance bars toggle (just acknowledge)
        Serial.println("-> Distance bars toggle (ignored)");
        // No response needed
    } else if (cmd.startsWith(":MS")) {
        handleSlewToTarget(client);
    } else if (cmd.startsWith(":CM")) {
        handleSyncToTarget(client);
    } else if (cmd.startsWith(":GVP")) {
        handleGetProductName(client);
    } else if (cmd.startsWith(":Sr")) {
        handleSetTargetRA(client, cmd.substring(3));
    } else if (cmd.startsWith(":Sd")) {
        handleSetTargetDec(client, cmd.substring(3));
    } else if (cmd.startsWith(":Gt")) {
        handleGetSiteLatitude(client);
    } else if (cmd.startsWith(":Gg")) {
        handleGetSiteLongitude(client);
    } else if (cmd.startsWith(":St")) {
        handleSetSiteLatitude(client, cmd.substring(3));
    } else if (cmd.startsWith(":Sg")) {
        handleSetSiteLongitude(client, cmd.substring(3));
    } else if (cmd.startsWith(":SL")) {
        handleSetLocalTime(client, cmd.substring(3));
    } else if (cmd.startsWith(":SC")) {
        handleSetDate(client, cmd.substring(3));
    } else if (cmd.startsWith(":SG")) {
        handleSetUTCOffset(client, cmd.substring(3));
    } else if (cmd.startsWith(":GVN")) {
        handleGetVersionNumber(client);
    } else if (cmd.startsWith(":GC")) {
        handleGetCalendarDate(client);
    } else if (cmd.startsWith(":GW")) {
        // Get UTC offset hours - return the stored timezone
        SiteConfig site = _config->getSite();
        char buffer[8];
        sprintf(buffer, "%+03d", (int)site.timezone);
        client.print(buffer);
        client.write("#");
        client.flush();
        Serial.println("-> Sent UTC offset");
    } else if (cmd.startsWith(":GVD")) {
        handleGetFirmwareDate(client);
    } else if (cmd.startsWith(":GL")) {
        handleGetLocalTime12(client);
    } else if (cmd.startsWith(":GG")) {
        handleGetUTCOffsetFormatted(client);
    } else if (cmd.startsWith(":GVT")) {
        handleGetFirmwareTime(client);
    } else if (cmd.startsWith(":RS")) {
        // Set slew rate to max (fastest) - push-to only, acknowledge
        Serial.println("-> Set slew rate to max (ignored - push-to)");
        // No response needed
    } else if (cmd.startsWith(":RM")) {
        // Set slew rate to medium - push-to only, acknowledge
        Serial.println("-> Set slew rate to medium (ignored - push-to)");
        // No response needed
    } else if (cmd.startsWith(":RC")) {
        // Set slew rate to centering - push-to only, acknowledge
        Serial.println("-> Set slew rate to centering (ignored - push-to)");
        // No response needed
    } else if (cmd.startsWith(":RG")) {
        // Set slew rate to guiding - push-to only, acknowledge
        Serial.println("-> Set slew rate to guiding (ignored - push-to)");
        // No response needed
    } else {
        // Unknown command, send acknowledgment
        Serial.println("-> Unknown command, sending '0'");
        client.write("0");
        client.flush();
    }
}

void LX200Server::handleGetRA(WiFiClient& client) {
    EquatorialCoords pos = _coords->getCurrentPosition();
    String ra = raToString(pos.ra);
    Serial.print("-> Sending RA: ");
    Serial.println(ra);
    client.print(ra);
    client.write("#");
    client.flush();
}

void LX200Server::handleGetDec(WiFiClient& client) {
    EquatorialCoords pos = _coords->getCurrentPosition();
    String dec = decToString(pos.dec);
    Serial.print("-> Sending Dec: ");
    Serial.println(dec);
    client.print(dec);
    client.write("#");
    client.flush();
}

void LX200Server::handleGetAltitude(WiFiClient& client) {
    TelescopePosition pos = _coords->getRawPosition();
    String alt = decToString(pos.altitude); // Use same format as Dec
    client.print(alt);
    client.write("#");
    client.flush();
}

void LX200Server::handleGetAzimuth(WiFiClient& client) {
    TelescopePosition pos = _coords->getRawPosition();
    String az = decToString(pos.azimuth); // Use same format, but 0-360
    client.print(az);
    client.write("#");
    client.flush();
}

void LX200Server::handleSlewToTarget(WiFiClient& client) {
    // Push-to only - acknowledge but don't slew
    client.write("0"); // Slew possible (but we won't actually do it)
    client.flush();
    Serial.println("Slew command acknowledged (push-to only)");
}

void LX200Server::handleSyncToTarget(WiFiClient& client) {
    // Sync command - could be used for alignment
    client.print("Coordinates matched.        #");
    client.flush();
    Serial.println("Sync command received");
    
    // TODO: Use this for alignment if desired
}

void LX200Server::handleGetProductName(WiFiClient& client) {
    String name = _config->getDeviceName();
    client.print(name);
    client.write("#");
    client.flush();
}

void LX200Server::handleSetTargetRA(WiFiClient& client, String value) {
    targetRA = parseRA(value);
    client.write("1"); // Success
    client.flush();
}

void LX200Server::handleSetTargetDec(WiFiClient& client, String value) {
    targetDec = parseDec(value);
    client.write("1"); // Success
    client.flush();
}

void LX200Server::handleGetSiteLatitude(WiFiClient& client) {
    SiteConfig site = _config->getSite();
    String lat = decToString(site.latitude);
    client.print(lat);
    client.write("#");
    client.flush();
}

void LX200Server::handleGetSiteLongitude(WiFiClient& client) {
    SiteConfig site = _config->getSite();
    String lon = decToString(site.longitude);
    client.print(lon);
    client.write("#");
    client.flush();
}

String LX200Server::raToString(double ra) {
    int hours = (int)ra;
    double remainder = (ra - hours) * 60.0;
    int minutes = (int)remainder;
    double seconds = (remainder - minutes) * 60.0;
    int tenths = (int)((seconds / 60.0) * 10.0); // Convert seconds to tenths of minutes
    
    char buffer[16];
    sprintf(buffer, "%02d:%02d.%01d", hours, minutes, tenths);
    return String(buffer);
}

String LX200Server::decToString(double dec) {
    char sign = dec >= 0 ? '+' : '-';
    dec = abs(dec);
    
    int degrees = (int)dec;
    double remainder = (dec - degrees) * 60.0;
    int minutes = (int)remainder;
    double seconds = (remainder - minutes) * 60.0;
    int tenths = (int)((seconds / 60.0) * 10.0); // Convert seconds to tenths of minutes
    
    char buffer[16];
    sprintf(buffer, "%c%02d*%02d.%01d", sign, degrees, minutes, tenths);
    return String(buffer);
}

double LX200Server::parseRA(String raStr) {
    // Parse HH:MM:SS format
    int firstColon = raStr.indexOf(':');
    int secondColon = raStr.indexOf(':', firstColon + 1);
    
    if (firstColon == -1) return 0.0;
    
    int hours = raStr.substring(0, firstColon).toInt();
    int minutes = raStr.substring(firstColon + 1, secondColon).toInt();
    int seconds = 0;
    
    if (secondColon != -1) {
        seconds = raStr.substring(secondColon + 1).toInt();
    }
    
    return hours + (minutes / 60.0) + (seconds / 3600.0);
}

double LX200Server::parseDec(String decStr) {
    // Parse ±DD*MM:SS or ±DD:MM:SS format
    bool negative = decStr.charAt(0) == '-';
    
    int firstSep = decStr.indexOf('*');
    if (firstSep == -1) firstSep = decStr.indexOf(':');
    int secondSep = decStr.indexOf(':', firstSep + 1);
    
    if (firstSep == -1) return 0.0;
    
    int degrees = decStr.substring(0, firstSep).toInt();
    int minutes = decStr.substring(firstSep + 1, secondSep).toInt();
    int seconds = 0;
    
    if (secondSep != -1) {
        seconds = decStr.substring(secondSep + 1).toInt();
    }
    
    double result = abs(degrees) + (minutes / 60.0) + (seconds / 3600.0);
    return negative ? -result : result;
}

void LX200Server::handleSetSiteLatitude(WiFiClient& client, String value) {
    double lat = parseDec(value);
    SiteConfig site = _config->getSite();
    _config->setSite(lat, site.longitude, site.timezone);
    
    Serial.print("Site latitude set to: ");
    Serial.println(lat);
    
    client.write("1"); // Success
    client.flush();
}

void LX200Server::handleSetSiteLongitude(WiFiClient& client, String value) {
    double lon = parseDec(value);
    SiteConfig site = _config->getSite();
    _config->setSite(site.latitude, lon, site.timezone);
    
    Serial.print("Site longitude set to: ");
    Serial.println(lon);
    
    client.write("1"); // Success
    client.flush();
}

void LX200Server::handleSetLocalTime(WiFiClient& client, String value) {
    // Parse HH:MM:SS format
    // Format: HH:MM:SS
    Serial.print("Set local time request: ");
    Serial.println(value);
    
    int firstColon = value.indexOf(':');
    int secondColon = value.indexOf(':', firstColon + 1);
    
    if (firstColon > 0 && secondColon > 0) {
        pendingHour = value.substring(0, firstColon).toInt();
        pendingMinute = value.substring(firstColon + 1, secondColon).toInt();
        pendingSecond = value.substring(secondColon + 1).toInt();
        timeSet = true;
        
        Serial.print("Stored time: ");
        Serial.print(pendingHour);
        Serial.print(":");
        Serial.print(pendingMinute);
        Serial.print(":");
        Serial.println(pendingSecond);
    }
    
    client.write("1"); // Success
    client.flush();
}

void LX200Server::handleSetDate(WiFiClient& client, String value) {
    // Parse MM/DD/YY format
    // Format: MM/DD/YY
    Serial.print("Set date request: ");
    Serial.println(value);
    
    int firstSlash = value.indexOf('/');
    int secondSlash = value.indexOf('/', firstSlash + 1);
    
    if (firstSlash > 0 && secondSlash > 0) {
        int month = value.substring(0, firstSlash).toInt();
        int day = value.substring(firstSlash + 1, secondSlash).toInt();
        int year = value.substring(secondSlash + 1).toInt();
        
        // Convert 2-digit year to 4-digit (assume 2000+)
        if (year < 100) year += 2000;
        
        Serial.print("Parsed date: ");
        Serial.print(month);
        Serial.print("/");
        Serial.print(day);
        Serial.print("/");
        Serial.println(year);
        
        // Set system time using ESP32 time functions
        struct tm timeinfo;
        timeinfo.tm_year = year - 1900;  // Years since 1900
        timeinfo.tm_mon = month - 1;      // 0-11
        timeinfo.tm_mday = day;
        
        // Use time from :SL command if available, otherwise use current time
        if (timeSet) {
            timeinfo.tm_hour = pendingHour;
            timeinfo.tm_min = pendingMinute;
            timeinfo.tm_sec = pendingSecond;
            timeSet = false; // Reset flag
        } else {
            // Use current time as fallback
            time_t now;
            struct tm current;
            time(&now);
            localtime_r(&now, &current);
            timeinfo.tm_hour = current.tm_hour;
            timeinfo.tm_min = current.tm_min;
            timeinfo.tm_sec = current.tm_sec;
        }
        
        time_t t = mktime(&timeinfo);
        struct timeval now = { .tv_sec = t };
        settimeofday(&now, NULL);
        
        Serial.print("System time updated to: ");
        Serial.print(timeinfo.tm_hour);
        Serial.print(":");
        Serial.print(timeinfo.tm_min);
        Serial.print(":");
        Serial.println(timeinfo.tm_sec);
    }
    
    client.print("1Updating        Planetary Data#                                #");
    client.flush();
}

void LX200Server::handleSetUTCOffset(WiFiClient& client, String value) {
    // Parse timezone offset like +07.0 or -05.0
    double offset = value.toFloat();
    
    SiteConfig site = _config->getSite();
    _config->setSite(site.latitude, site.longitude, offset);
    
    Serial.print("UTC offset set to: ");
    Serial.println(offset);
    
    client.write("1"); // Success
    client.flush();
}

void LX200Server::handleGetVersionNumber(WiFiClient& client) {
    client.print("1.0"); // Firmware version
    client.write("#");
    client.flush();
}

void LX200Server::handleGetCalendarDate(WiFiClient& client) {
    // Return current date in MM/DD/YY format
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    char buffer[16];
    sprintf(buffer, "%02d/%02d/%02d", 
            timeinfo.tm_mon + 1,      // 0-11 -> 1-12
            timeinfo.tm_mday,
            (timeinfo.tm_year % 100)); // Years since 1900 -> last 2 digits
    
    client.print(buffer);
    client.write("#");
    client.flush();
}

void LX200Server::handleGetFirmwareDate(WiFiClient& client) {
    // Return firmware build date
    client.print("Mar 07 2026");
    client.write("#");
    client.flush();
}

void LX200Server::handleGetLocalTime12(WiFiClient& client) {
    // Return local time in 24-hour format HH:MM:SS (despite function name)
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    char buffer[16];
    sprintf(buffer, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    
    client.print(buffer);
    client.write("#");
    client.flush();
}

void LX200Server::handleGetUTCOffsetFormatted(WiFiClient& client) {
    // Return UTC offset in sHH:MM format
    SiteConfig site = _config->getSite();
    int offset = (int)site.timezone;
    int hours = abs(offset);
    int minutes = (int)((abs(site.timezone) - hours) * 60);
    
    char buffer[8];
    sprintf(buffer, "%c%02d:%02d", (offset >= 0 ? '+' : '-'), hours, minutes);
    
    client.print(buffer);
    client.write("#");
    client.flush();
}

void LX200Server::handleGetFirmwareTime(WiFiClient& client) {
    // Return firmware build time (HH:MM:SS format)
    client.print("12:00:00");
    client.write("#");
    client.flush();
}
