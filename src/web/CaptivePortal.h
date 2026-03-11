#ifndef CAPTIVE_PORTAL_H
#define CAPTIVE_PORTAL_H

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include "../config/Config.h"
#include "../sensors/SensorManager.h"
#include "../astronomy/Coordinates.h"

class CaptivePortal {
public:
    CaptivePortal(Config* config, SensorManager* sensors, Coordinates* coords);
    
    void begin();
    void handle();
    
    bool isAPMode();
    String getAPIP();
    
    // WebSocket methods
    void broadcastPosition();
    bool hasWebSocketClients();
    
private:
    Config* _config;
    SensorManager* _sensors;
    Coordinates* _coords;
    
    AsyncWebServer* server;
    AsyncWebSocket* ws;
    DNSServer* dnsServer;
    
    bool apMode;
    String apIP;
    
    // Web page setup
    void setupRoutes();
    
    // WebSocket event handler
    void onWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, 
                          AwsEventType type, void* arg, uint8_t* data, size_t len);
    
    // API handlers
    void handleGetConfig(AsyncWebServerRequest* request);
    void handleConfigSubmit(AsyncWebServerRequest* request);
    void handleAlignmentSubmit(AsyncWebServerRequest* request);
    void handleGetStatus(AsyncWebServerRequest* request);
    void handleGetPosition(AsyncWebServerRequest* request);
    void handleGetDiagnostics(AsyncWebServerRequest* request);
    void handleSetTime(AsyncWebServerRequest* request);
};

#endif // CAPTIVE_PORTAL_H
