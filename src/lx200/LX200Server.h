#ifndef LX200_SERVER_H
#define LX200_SERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include "../astronomy/Coordinates.h"
#include "../config/Config.h"

#define LX200_PORT 4030
#define MAX_CLIENTS 2

class LX200Server {
public:
    LX200Server(Coordinates* coords, Config* config);
    
    void begin();
    void handle();
    
    bool isClientConnected();
    int getClientCount();
    
private:
    WiFiServer* server;
    WiFiClient clients[MAX_CLIENTS];
    
    Coordinates* _coords;
    Config* _config;
    
    // Per-client command buffers
    char commandBuffer[MAX_CLIENTS][64];
    int bufferPos[MAX_CLIENTS];
    
    // Command handlers
    void processCommand(WiFiClient& client, String cmd);
    
    // LX200 command implementations
    void handleGetRA(WiFiClient& client);           // :GR#
    void handleGetDec(WiFiClient& client);          // :GD#
    void handleGetAltitude(WiFiClient& client);     // :GA#
    void handleGetAzimuth(WiFiClient& client);      // :GZ#
    void handleSlewToTarget(WiFiClient& client);    // :MS#
    void handleSyncToTarget(WiFiClient& client);    // :CM#
    void handleGetProductName(WiFiClient& client);  // :GVP#
    void handleSetTargetRA(WiFiClient& client, String value);   // :Sr#
    void handleSetTargetDec(WiFiClient& client, String value);  // :Sd#
    void handleGetSiteLatitude(WiFiClient& client); // :Gt#
    void handleGetSiteLongitude(WiFiClient& client); // :Gg#
    void handleSetSiteLatitude(WiFiClient& client, String value);  // :St#
    void handleSetSiteLongitude(WiFiClient& client, String value); // :Sg#
    void handleSetLocalTime(WiFiClient& client, String value);     // :SL#
    void handleSetDate(WiFiClient& client, String value);          // :SC#
    void handleSetUTCOffset(WiFiClient& client, String value);     // :SG#
    void handleGetVersionNumber(WiFiClient& client);               // :GVN#
    void handleGetCalendarDate(WiFiClient& client);                // :GC#
    void handleGetFirmwareDate(WiFiClient& client);                // :GVD#
    void handleGetLocalTime12(WiFiClient& client);                 // :GL#
    void handleGetUTCOffsetFormatted(WiFiClient& client);          // :GG#
    void handleGetFirmwareTime(WiFiClient& client);                // :GVT#
    
    // Helper functions
    String raToString(double ra);      // Convert RA hours to HH:MM:SS
    String decToString(double dec);    // Convert Dec degrees to DD:MM:SS
    double parseRA(String raStr);      // Parse RA string to hours
    double parseDec(String decStr);    // Parse Dec string to degrees
    
    // Target coordinates (for :MS# and :CM# commands)
    double targetRA;
    double targetDec;
    
    // Pending time values (set by :SL, used by :SC)
    int pendingHour;
    int pendingMinute;
    int pendingSecond;
    bool timeSet;
};

#endif // LX200_SERVER_H
