#include "DiagnosticDisplay.h"

DiagnosticDisplay::DiagnosticDisplay()
    : _display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1),
      _present(false),
      _address(0),
      _missCount(0),
      _page(PAGE_AXES),
      _lastPageChangeMs(0) {}

uint8_t DiagnosticDisplay::detectAddress() {
    for (uint8_t addr : {(uint8_t)0x3C, (uint8_t)0x3D}) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            return addr;
        }
    }
    return 0;
}

bool DiagnosticDisplay::initPanel() {
    if (!_display.begin(SSD1306_SWITCHCAPVCC, _address)) {
        return false;
    }

    _display.clearDisplay();
    _display.setTextColor(SSD1306_WHITE);
    _display.setTextSize(1);
    applyNightBrightness();
    _display.setCursor(0, 0);
    _display.println("PushTo ESP32");
    _display.println("Diagnostics");
    _display.display();
    return true;
}

bool DiagnosticDisplay::begin() {
    _diagnostics = "";

    _address = detectAddress();
    if (_address == 0) {
        _diagnostics += "[Display] No SSD1306 at 0x3C or 0x3D - running headless\n";
        _present = false;
        Serial.print(_diagnostics);
        return false;
    }

    _diagnostics += "[Display] Found SSD1306 at 0x" + String(_address, HEX) + "\n";

    if (!initPanel()) {
        _diagnostics += "[Display] SSD1306 driver init failed - running headless\n";
        _present = false;
        Serial.print(_diagnostics);
        return false;
    }

    _present = true;
    _diagnostics += "[Display] Diagnostic display ready\n";
    Serial.print(_diagnostics);
    return true;
}

void DiagnosticDisplay::poll() {
    uint8_t found = detectAddress();

    if (found != 0) {
        _missCount = 0;
        if (!_present) {
            _address = found;
            if (initPanel()) {
                _present = true;
                Serial.println("[Display] Display plugged in");
            }
        }
    } else if (_present && ++_missCount >= DISPLAY_ABSENT_THRESHOLD) {
        // Debounced for the same reason as the sensors: a single glitched
        // probe should not blank a display that is still plugged in.
        _present = false;
        Serial.println("[Display] Display unplugged");
    }
}

bool DiagnosticDisplay::isPresent() const {
    return _present;
}

String DiagnosticDisplay::describe() const {
    return _diagnostics;
}

void DiagnosticDisplay::applyNightBrightness() {
    // Dimmest usable setting: this sits next to a dark-adapted eye.
    _display.ssd1306_command(SSD1306_SETCONTRAST);
    _display.ssd1306_command(1);
    _display.dim(true);
    _display.ssd1306_command(0xD9); // Pre-charge period
    _display.ssd1306_command(0x11); // Phase 1/2 = 1 DCLK each (minimum)
    _display.ssd1306_command(0xDB); // VCOMH deselect level
    _display.ssd1306_command(0x00); // ~0.65 x VCC (lowest available)
}

void DiagnosticDisplay::render(const TelescopeSnapshot& snapshot,
                               const BleLinkStatus& link,
                               const BBoxStats& protocol) {
    if (!_present) return;

    uint32_t now = millis();
    if (now - _lastPageChangeMs >= DISPLAY_PAGE_DURATION_MS) {
        _lastPageChangeMs = now;
        _page = (_page + 1) % PAGE_COUNT;
    }

    _display.clearDisplay();
    _display.setTextSize(1);

    switch (_page) {
        case PAGE_MAGNET: drawMagnet(snapshot); break;
        case PAGE_LINK:   drawLink(link, protocol); break;
        case PAGE_AXES:
        default:          drawAxes(snapshot); break;
    }

    drawStatusBar(snapshot, link);
    _display.display();
}

// Page 1: what each axis reads, and what SkySafari is being told.
void DiagnosticDisplay::drawAxes(const TelescopeSnapshot& snapshot) {
    _display.setCursor(0, 0);
    _display.print("AXES        cpr ");
    _display.print(TelescopeState::countsPerRevolution());
    _display.drawLine(0, 9, OLED_WIDTH - 1, 9, SSD1306_WHITE);

    _display.setCursor(0, 13);
    _display.print("AZ  ");
    if (snapshot.azimuth.valid) {
        _display.print(snapshot.azimuth.degrees, 2);
        _display.print((char)247);
    } else {
        _display.print("--");
    }

    _display.setCursor(0, 23);
    _display.printf(" raw %5d  cnt %5d",
                    (int)snapshot.azimuth.rawValue,
                    (int)snapshot.azimuth.count);

    _display.setCursor(0, 34);
    _display.print("ALT ");
    if (snapshot.altitude.valid) {
        if (snapshot.altitude.degrees >= 0) _display.print("+");
        _display.print(snapshot.altitude.degrees, 2);
        _display.print((char)247);
    } else {
        _display.print("--");
    }

    _display.setCursor(0, 44);
    _display.printf(" raw %6.1f cnt %5d",
                    snapshot.altitude.rawValue,
                    (int)snapshot.altitude.count);
}

// Page 2: is the magnetic encoder actually seeing its magnet?
void DiagnosticDisplay::drawMagnet(const TelescopeSnapshot& snapshot) {
    const SensorHealth& health = snapshot.health;

    _display.setCursor(0, 0);
    _display.print("MAGNET / SENSORS");
    _display.drawLine(0, 9, OLED_WIDTH - 1, 9, SSD1306_WHITE);

    _display.setCursor(0, 13);
    _display.print("Detected: ");
    _display.print(health.magnetDetected ? "YES" : "NO");

    _display.setCursor(0, 23);
    _display.print("Gap: ");
    if (health.magnetTooWeak) {
        _display.print("TOO FAR");
    } else if (health.magnetTooStrong) {
        _display.print("TOO CLOSE");
    } else if (health.magnetDetected) {
        _display.print("OK");
    } else {
        _display.print("--");
    }

    _display.setCursor(0, 33);
    _display.printf("AGC %3u  MAG %5u", health.magnetAgc, health.magnetMagnitude);

    _display.setCursor(0, 43);
    _display.printf("AS5600 %s  MPU %s",
                    health.azimuthSensorPresent ? "OK" : "--",
                    health.altitudeSensorPresent ? "OK" : "--");
}

// Page 3: the link to the phone, and what it has been asking for.
void DiagnosticDisplay::drawLink(const BleLinkStatus& link, const BBoxStats& protocol) {
    _display.setCursor(0, 0);
    _display.print("BLE / BBOX");
    _display.drawLine(0, 9, OLED_WIDTH - 1, 9, SSD1306_WHITE);

    _display.setCursor(0, 13);
    _display.print("Link: ");
    if (link.connected) {
        _display.printf("CONN mtu %u", link.mtu);
    } else if (link.advertising) {
        _display.print("ADVERTISING");
    } else {
        _display.print("IDLE");
    }

    _display.setCursor(0, 23);
    _display.printf("Q %-6lu H %lu",
                    (unsigned long)protocol.positionRequests,
                    (unsigned long)protocol.resolutionRequests);

    _display.setCursor(0, 33);
    _display.printf("Bad %-4lu Drop %lu",
                    (unsigned long)protocol.ignoredBytes,
                    (unsigned long)link.rxOverflows);

    _display.setCursor(0, 43);
    if (protocol.lastCommand != 0) {
        _display.printf("Last %c %lus ago",
                        protocol.lastCommand,
                        (unsigned long)((millis() - protocol.lastCommandMs) / 1000));
    } else {
        _display.print("No requests yet");
    }
}

void DiagnosticDisplay::drawStatusBar(const TelescopeSnapshot& snapshot,
                                      const BleLinkStatus& link) {
    _display.drawLine(0, 53, OLED_WIDTH - 1, 53, SSD1306_WHITE);

    _display.setCursor(0, 56);
    _display.printf("AZ%s AL%s BT%s",
                    snapshot.azimuth.valid ? "+" : "-",
                    snapshot.altitude.valid ? "+" : "-",
                    link.connected ? "+" : (link.advertising ? "~" : "-"));

    // Page indicator, right aligned.
    _display.setCursor(OLED_WIDTH - 23, 56);
    _display.printf("%u/%u", (unsigned)(_page + 1), (unsigned)PAGE_COUNT);
}
