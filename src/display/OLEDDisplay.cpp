#include "OLEDDisplay.h"

OLEDDisplay::OLEDDisplay(SensorManager* sensors, Coordinates* coordinates)
    : _display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1),
      _sensors(sensors),
      _coordinates(coordinates),
      _connected(false),
      _address(0),
      _mode(DISPLAY_SETUP) {}

bool OLEDDisplay::begin() {
    _diagnostics = "";

    _address = detectAddress();

    if (_address == 0) {
        _diagnostics += "[OLED] No SSD1306 found at 0x3C or 0x3D\n";
        _connected = false;
        Serial.print(_diagnostics);
        return false;
    }

    _diagnostics += "[OLED] Found display at 0x" + String(_address, HEX) + "\n";

    if (!_display.begin(SSD1306_SWITCHCAPVCC, _address)) {
        _diagnostics += "[OLED] SSD1306 driver init failed!\n";
        _connected = false;
        Serial.print(_diagnostics);
        return false;
    }

    _connected = true;
    _display.clearDisplay();
    _display.setTextColor(SSD1306_WHITE);
    _display.setTextSize(1);
    // Default to a comfortably dim level for night-sky observing.
    setBrightness(1);
    _display.setCursor(0, 0);
    _display.println("PushTo ESP32");
    _display.println("Starting...");
    _display.display();
    _diagnostics += "[OLED] Display initialized OK\n";
    Serial.print(_diagnostics);
    return true;
}

void OLEDDisplay::checkConnection() {
    bool present = (detectAddress() != 0);

    if (_connected && !present) {
        _connected = false;
        Serial.println("[OLED] Display disconnected");
    } else if (!_connected && present) {
        _address = detectAddress();
        if (_display.begin(SSD1306_SWITCHCAPVCC, _address)) {
            _connected = true;
            _display.clearDisplay();
            _display.setTextColor(SSD1306_WHITE);
            _display.setTextSize(1);
            setBrightness(1);
            _display.setCursor(0, 0);
            _display.println("PushTo ESP32");
            _display.println("Reconnected!");
            _display.display();
            Serial.println("[OLED] Display reconnected!");
        }
    }
}

String OLEDDisplay::getDiagnostics() {
    return _diagnostics;
}

uint8_t OLEDDisplay::detectAddress() {
    for (uint8_t addr : {(uint8_t)0x3C, (uint8_t)0x3D}) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            return addr;
        }
    }
    return 0;
}

bool OLEDDisplay::isConnected() {
    return _connected;
}

void OLEDDisplay::setMode(DisplayMode mode) {
    _mode = mode;
}

DisplayMode OLEDDisplay::getMode() {
    return _mode;
}

void OLEDDisplay::setWiFiInfo(const String& ssid, const String& password, const String& url) {
    _wifiSSID = ssid;
    _wifiPassword = password;
    _wifiURL = url;
}

void OLEDDisplay::setBrightness(uint8_t value) {
    if (!_connected) return;
    // SSD1306 SETCONTRAST = 0x81, followed by a value byte 0..255.
    _display.ssd1306_command(SSD1306_SETCONTRAST);
    _display.ssd1306_command(value);
    // Engage internal dim mode below ~16 for an extra-low floor.
    _display.dim(value < 16);
}

void OLEDDisplay::update() {
    if (!_connected) return;

    _display.clearDisplay();
    _display.setTextSize(1);

    if (_mode == DISPLAY_SETUP) {
        drawSetupMode();
    } else {
        drawObserveMode();
    }

    // Sensor status bar (always at bottom)
    _display.setCursor(0, 56);
    _display.print("Az:");
    _display.print(_sensors->isAS5600Connected() ? "OK" : "--");
    _display.print("  Alt:");
    _display.print(_sensors->isMPU6050Connected() ? "OK" : "--");

    _display.display();
}

void OLEDDisplay::drawSetupMode() {
    TelescopePosition pos = _sensors->getPosition();

    // Alt/Az at top
    _display.setCursor(0, 0);
    _display.print("Alt ");
    if (pos.altitude >= 0) _display.print("+");
    _display.print(pos.altitude, 1);
    _display.print((char)247);

    _display.setCursor(64, 0);
    _display.print("Az ");
    _display.print(pos.azimuth, 1);
    _display.print((char)247);

    // Divider
    _display.drawLine(0, 11, OLED_WIDTH - 1, 11, SSD1306_WHITE);

    // WiFi info
    _display.setCursor(0, 15);
    _display.print("WiFi: ");
    _display.print(_wifiSSID);

    _display.setCursor(0, 27);
    _display.print("Pass: ");
    _display.print(_wifiPassword);

    _display.setCursor(0, 39);
    _display.print(_wifiURL);

    // Divider above status
    _display.drawLine(0, 52, OLED_WIDTH - 1, 52, SSD1306_WHITE);
}

void OLEDDisplay::drawObserveMode() {
    TelescopePosition pos = _sensors->getPosition();
    EquatorialCoords eq = _coordinates->getCurrentPosition();

    char raBuf[16];
    char decBuf[16];
    formatRA(eq.ra, raBuf, sizeof(raBuf));
    formatDec(eq.dec, decBuf, sizeof(decBuf));

    // Alt/Az at top
    _display.setCursor(0, 0);
    _display.print("Alt ");
    if (pos.altitude >= 0) _display.print("+");
    _display.print(pos.altitude, 2);
    _display.print((char)247);

    _display.setCursor(0, 12);
    _display.print("Az  ");
    _display.print(pos.azimuth, 2);
    _display.print((char)247);

    // Divider
    _display.drawLine(0, 24, OLED_WIDTH - 1, 24, SSD1306_WHITE);

    // RA/Dec
    _display.setCursor(0, 28);
    _display.print("RA  ");
    _display.print(raBuf);

    _display.setCursor(0, 40);
    _display.print("Dec ");
    _display.print(decBuf);

    // Divider above status
    _display.drawLine(0, 52, OLED_WIDTH - 1, 52, SSD1306_WHITE);
}

// Format RA as HHh MMm SSs
void OLEDDisplay::formatRA(double ra, char* buf, size_t len) {
    if (ra < 0) ra += 24.0;
    int h = (int)ra;
    int m = (int)((ra - h) * 60.0);
    int s = (int)((ra - h - m / 60.0) * 3600.0) % 60;
    snprintf(buf, len, "%02dh%02dm%02ds", h, m, s);
}

// Format Dec as +/-DDd MM' SS"
void OLEDDisplay::formatDec(double dec, char* buf, size_t len) {
    char sign = dec >= 0 ? '+' : '-';
    dec = fabs(dec);
    int d = (int)dec;
    int m = (int)((dec - d) * 60.0);
    int s = (int)((dec - d - m / 60.0) * 3600.0) % 60;
    snprintf(buf, len, "%c%02d%c%02d'%02d\"", sign, d, (char)247, m, s);
}
