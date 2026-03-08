#include "CaptivePortal.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

#define AP_SSID "PushTo-Setup"
#define AP_PASSWORD "telescope"
#define DNS_PORT 53

CaptivePortal::CaptivePortal(Config* config, SensorManager* sensors, Coordinates* coords) {
    _config = config;
    _sensors = sensors;
    _coords = coords;
    server = nullptr;
    dnsServer = nullptr;
    apMode = true;
}

void CaptivePortal::begin() {
    // Mount LittleFS
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed! Continuing without filesystem...");
    } else {
        Serial.println("LittleFS mounted successfully");
    }
    
    // Start in AP mode
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    
    apIP = WiFi.softAPIP().toString();
    Serial.print("AP Mode - IP: ");
    Serial.println(apIP);
    
    // Start DNS server for captive portal
    dnsServer = new DNSServer();
    dnsServer->start(DNS_PORT, "*", WiFi.softAPIP());
    
    // Start web server
    server = new AsyncWebServer(80);
    setupRoutes();
    server->begin();
    
    Serial.println("Captive portal started");
}

void CaptivePortal::handle() {
    if (dnsServer) {
        dnsServer->processNextRequest();
    }
}

bool CaptivePortal::isAPMode() {
    return apMode;
}

String CaptivePortal::getAPIP() {
    return apIP;
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
    
    // Captive portal redirect - for old /config etc URLs
    server->on("/config", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/config.html");
    });
    
    server->on("/align", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/align.html");
    });
    
    server->on("/diagnostics", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/diagnostics.html");
    });
    
    // Captive portal redirect for everything else
    server->onNotFound([](AsyncWebServerRequest* request) {
        request->redirect("/");
    });
}

void CaptivePortal::handleGetConfig(AsyncWebServerRequest* request) {
    StaticJsonDocument<256> doc;
    SiteConfig site = _config->getSite();
    
    doc["latitude"] = site.latitude;
    doc["longitude"] = site.longitude;
    doc["timezone"] = site.timezone;
    doc["valid"] = site.valid;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void CaptivePortal::handleConfigSubmit(AsyncWebServerRequest* request) {
    if (request->hasParam("lat", true) && request->hasParam("lon", true) && request->hasParam("tz", true)) {
        double lat = request->getParam("lat", true)->value().toDouble();
        double lon = request->getParam("lon", true)->value().toDouble();
        int tz = request->getParam("tz", true)->value().toInt() * 3600;
        
        _config->setSite(lat, lon, tz);
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
        
        // Parse RA and Dec (simplified parsing)
        double ra = raStr.toDouble(); // TODO: Proper HH:MM:SS parsing
        double dec = decStr.toDouble(); // TODO: Proper DD:MM:SS parsing
        
        TelescopePosition pos = _sensors->getPosition();
        _config->setAlignmentStar(starNum, ra, dec, pos.azimuth, pos.altitude);
        _coords->performAlignment();
        
        request->send(200, "text/plain", "Star " + String(starNum + 1) + " set!");
    } else {
        request->send(400, "text/plain", "Missing parameters");
    }
}

void CaptivePortal::handleGetStatus(AsyncWebServerRequest* request) {
    StaticJsonDocument<256> doc;
    
    doc["sensorsOK"] = _sensors->isAS5600Connected() && _sensors->isMPU6050Connected();
    doc["siteValid"] = _config->getSite().valid;
    doc["aligned"] = _coords->isAligned();
    doc["ip"] = apIP;
    
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
    request->send(200, "text/plain", diag);
}
