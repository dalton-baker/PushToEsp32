#include "CaptivePortal.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <sys/time.h>

#define AP_SSID "PushTo-Setup"
#define AP_PASSWORD "telescope"
#define AP_CHANNEL 6
#define AP_MAX_CONNECTIONS 4

CaptivePortal::CaptivePortal(Config* config, SensorManager* sensors, Coordinates* coords) {
    _config = config;
    _sensors = sensors;
    _coords = coords;
    server = nullptr;
    ws = nullptr;
    _running = false;
    _shutdownRequestedAt = 0;
}

void CaptivePortal::begin() {
    // Mount LittleFS
    if (!LittleFS.begin(true)) {
        Serial.println("[WiFi] LittleFS mount failed! Continuing without filesystem...");
    } else {
        Serial.println("[WiFi] LittleFS mounted successfully");
    }
    
    // Configure and start AP with fixed IP 192.168.0.1
    Serial.println("[WiFi] Configuring access point...");
    WiFi.mode(WIFI_AP);
    delay(100);
    
    IPAddress local_IP(192, 168, 0, 1);
    IPAddress gateway(192, 168, 0, 1);
    IPAddress subnet(255, 255, 255, 0);
    
    if (!WiFi.softAPConfig(local_IP, gateway, subnet)) {
        Serial.println("[WiFi] ERROR: AP config failed!");
    }
    
    if (!WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, false, AP_MAX_CONNECTIONS)) {
        Serial.println("[WiFi] ERROR: softAP start failed!");
        return;
    }
    
    delay(200); // Allow AP to stabilize
    
    apIP = WiFi.softAPIP().toString();
    Serial.println("[WiFi] AP started - SSID: " + String(AP_SSID));
    Serial.println("[WiFi] AP IP: " + apIP);
    Serial.println("[WiFi] AP MAC: " + WiFi.softAPmacAddress());
    Serial.println("[WiFi] Channel: " + String(AP_CHANNEL));
    
    // Start mDNS for setup.local
    if (MDNS.begin("setup")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println("[WiFi] mDNS started: setup.local");
    } else {
        Serial.println("[WiFi] WARNING: mDNS failed to start");
    }
    
    // Start web server
    server = new AsyncWebServer(80);
    
    // Setup WebSocket
    ws = new AsyncWebSocket("/ws");
    ws->onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client, 
                       AwsEventType type, void* arg, uint8_t* data, size_t len) {
        this->onWebSocketEvent(server, client, type, arg, data, len);
    });
    server->addHandler(ws);
    
    setupRoutes();
    server->begin();
    
    _running = true;
    Serial.println("[WiFi] Web server started on port 80");
    Serial.println("[WiFi] Access at: http://192.168.0.1 or http://setup.local");
}

void CaptivePortal::handle() {
    if (!_running) return;
    
    // Deferred shutdown: wait for response to flush before tearing down WiFi
    if (_shutdownRequestedAt > 0 && millis() - _shutdownRequestedAt > 3000) {
        _shutdownRequestedAt = 0;
        shutdown();
        return;
    }
    
    if (ws) {
        ws->cleanupClients();
    }
}

void CaptivePortal::shutdown() {
    if (!_running) return;
    
    Serial.println("[WiFi] Shutting down web server...");
    
    if (ws) {
        ws->closeAll();
    }
    if (server) {
        server->end();
    }
    
    MDNS.end();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    
    _running = false;
    Serial.println("[WiFi] Server shut down - WiFi off to conserve power");
}

bool CaptivePortal::isRunning() {
    return _running;
}

String CaptivePortal::getAPIP() {
    return apIP;
}

String CaptivePortal::getSSID() {
    return AP_SSID;
}

String CaptivePortal::getPassword() {
    return AP_PASSWORD;
}

void CaptivePortal::setupRoutes() {
    // Serve HTML files from LittleFS
    server->on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(LittleFS, "/index.html", "text/html");
    });
    
    server->on("/index.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(LittleFS, "/index.html", "text/html");
    });
    
    server->on("/config.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(LittleFS, "/config.html", "text/html");
    });
    
    server->on("/align.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(LittleFS, "/align.html", "text/html");
    });
    
    server->on("/diagnostics.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(LittleFS, "/diagnostics.html", "text/html");
    });
    
    // Serve stars.json for star picker
    server->on("/stars.json", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(LittleFS, "/stars.json", "application/json");
    });
    
    // API: Get config
    server->on("/api/config", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetConfig(request);
    });
    
    // API: Config submit
    server->on("/api/config", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleConfigSubmit(request);
    });
    
    // API: Alignment submit
    server->on("/api/align", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleAlignmentSubmit(request);
    });
    
    // API: Get alignment star status
    server->on("/api/align/stars", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetAlignmentStars(request);
    });
    
    // API: Get status
    server->on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetStatus(request);
    });
    
    // API: Get position
    server->on("/api/position", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetPosition(request);
    });
    
    // API: Get diagnostics
    server->on("/api/diagnostics", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetDiagnostics(request);
    });
    
    // API: Set time
    server->on("/api/time", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleSetTime(request);
    });
    
    // API: Finalize setup (shut down WiFi)
    server->on("/api/finalize", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleFinalize(request);
    });
    
    // Redirect shorthand URLs
    server->on("/config", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/config.html");
    });
    
    server->on("/align", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/align.html");
    });
    
    server->on("/diagnostics", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/diagnostics.html");
    });
    
    // 404 for unknown routes
    server->onNotFound([](AsyncWebServerRequest* request) {
        request->send(404, "text/plain", "Not found");
    });
}

void CaptivePortal::handleGetConfig(AsyncWebServerRequest* request) {
    StaticJsonDocument<256> doc;
    SiteConfig site = _config->getSite();
    
    doc["latitude"] = site.latitude;
    doc["longitude"] = site.longitude;
    doc["valid"] = site.valid;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void CaptivePortal::handleConfigSubmit(AsyncWebServerRequest* request) {
    if (request->hasParam("lat", true) && request->hasParam("lon", true)) {
        double lat = request->getParam("lat", true)->value().toDouble();
        double lon = request->getParam("lon", true)->value().toDouble();
        
        _config->setSite(lat, lon);
        request->send(200, "text/plain", "Configuration saved!");
    } else {
        request->send(400, "text/plain", "Missing parameters");
    }
}

void CaptivePortal::handleAlignmentSubmit(AsyncWebServerRequest* request) {
    if (request->hasParam("clear")) {
        _config->clearAlignment();
        _coords->performAlignment();
        request->send(200, "text/plain", "Alignment cleared");
        return;
    }
    
    if (request->hasParam("star", true) && request->hasParam("ra", true) && request->hasParam("dec", true)) {
        int starNum = request->getParam("star", true)->value().toInt() - 1;
        String raStr = request->getParam("ra", true)->value();
        String decStr = request->getParam("dec", true)->value();
        String starName = "";
        if (request->hasParam("name", true)) {
            starName = request->getParam("name", true)->value();
        }
        
        // Parse RA and Dec (simplified parsing)
        double ra = raStr.toDouble(); // TODO: Proper HH:MM:SS parsing
        double dec = decStr.toDouble(); // TODO: Proper DD:MM:SS parsing
        
        TelescopePosition pos = _sensors->getPosition();
        _config->setAlignmentStar(starNum, ra, dec, pos.azimuth, pos.altitude, starName);
        _coords->performAlignment();
        
        request->send(200, "text/plain", "Star " + String(starNum + 1) + " set!");
    } else {
        request->send(400, "text/plain", "Missing parameters");
    }
}

void CaptivePortal::handleGetStatus(AsyncWebServerRequest* request) {
    StaticJsonDocument<384> doc;
    
    doc["sensorsOK"] = _sensors->isAS5600Connected() && _sensors->isMPU6050Connected();
    doc["as5600"] = _sensors->isAS5600Connected();
    doc["mpu6050"] = _sensors->isMPU6050Connected();
    doc["siteValid"] = _config->getSite().valid;
    doc["aligned"] = _coords->isAligned();
    doc["ip"] = apIP;
    doc["clients"] = WiFi.softAPgetStationNum();
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void CaptivePortal::handleGetPosition(AsyncWebServerRequest* request) {
    StaticJsonDocument<256> doc;
    
    TelescopePosition pos = _sensors->getPosition();
    EquatorialCoords eq = _coords->getCurrentPosition();
    
    doc["az"] = pos.azimuth;
    doc["alt"] = pos.altitude;
    doc["ra"] = String(eq.ra, 4);
    doc["dec"] = String(eq.dec, 4);
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void CaptivePortal::handleGetDiagnostics(AsyncWebServerRequest* request) {
    String diag = _sensors->getDiagnostics();
    diag += "\nWiFi Diagnostics:\n";
    diag += "SSID: " + String(AP_SSID) + "\n";
    diag += "IP: " + apIP + "\n";
    diag += "MAC: " + WiFi.softAPmacAddress() + "\n";
    diag += "Channel: " + String(AP_CHANNEL) + "\n";
    diag += "Connected clients: " + String(WiFi.softAPgetStationNum()) + "\n";
    diag += "Free heap: " + String(ESP.getFreeHeap()) + " bytes\n";
    request->send(200, "text/plain", diag);
}

void CaptivePortal::onWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, 
                                      AwsEventType type, void* arg, uint8_t* data, size_t len) {
    switch(type) {
        case WS_EVT_CONNECT:
            Serial.printf("[WebSocket] Client #%u connected from %s\n", 
                         client->id(), client->remoteIP().toString().c_str());
            break;
            
        case WS_EVT_DISCONNECT:
            Serial.printf("[WebSocket] Client #%u disconnected\n", client->id());
            break;
            
        case WS_EVT_DATA:
            // We don't expect data from clients, but handle it gracefully
            break;
            
        case WS_EVT_PONG:
        case WS_EVT_ERROR:
            break;
    }
}

void CaptivePortal::broadcastPosition() {
    if (!ws || ws->count() == 0) {
        return; // No clients connected
    }
    
    StaticJsonDocument<256> doc;
    
    TelescopePosition pos = _sensors->getPosition();
    EquatorialCoords eq = _coords->getCurrentPosition();
    
    doc["az"] = String(pos.azimuth, 2);
    doc["alt"] = String(pos.altitude, 2);
    doc["ra"] = String(eq.ra, 4);
    doc["dec"] = String(eq.dec, 4);
    
    String message;
    serializeJson(doc, message);
    ws->textAll(message);
}

void CaptivePortal::handleSetTime(AsyncWebServerRequest* request) {
    if (request->hasParam("timestamp", true)) {
        String timestampStr = request->getParam("timestamp", true)->value();
        time_t timestamp = (time_t)timestampStr.toInt();
        
        // Set system time
        struct timeval tv;
        tv.tv_sec = timestamp;
        tv.tv_usec = 0;
        settimeofday(&tv, NULL);
        
        Serial.print("[WiFi] System time set to: ");
        Serial.println(timestamp);
        
        // Get current time for verification
        time_t now = time(nullptr);
        char buffer[64];
        struct tm* timeinfo = gmtime(&now);
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S UTC", timeinfo);
        
        String response = "Time synchronized: ";
        response += buffer;
        
        request->send(200, "text/plain", response);
    } else {
        request->send(400, "text/plain", "Missing timestamp parameter");
    }
}

void CaptivePortal::handleFinalize(AsyncWebServerRequest* request) {
    request->send(200, "text/plain", "Finalizing setup - WiFi will shut down shortly");
    Serial.println("[WiFi] Finalize requested - scheduling shutdown");
    _shutdownRequestedAt = millis();
}

void CaptivePortal::handleGetAlignmentStars(AsyncWebServerRequest* request) {
    StaticJsonDocument<512> doc;
    
    for (int i = 0; i < 2; i++) {
        AlignmentStar star = _config->getAlignmentStar(i);
        JsonObject starObj = doc.createNestedObject("star" + String(i + 1));
        starObj["ra"] = star.ra;
        starObj["dec"] = star.dec;
        starObj["az"] = star.az;
        starObj["alt"] = star.alt;
        starObj["name"] = _config->getAlignmentStarName(i);
        starObj["configured"] = (star.ra != 0.0 || star.dec != 0.0);
    }
    
    doc["aligned"] = _config->isAligned();
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

bool CaptivePortal::hasWebSocketClients() {
    return ws && ws->count() > 0;
}
