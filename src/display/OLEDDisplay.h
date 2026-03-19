#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include "../sensors/SensorManager.h"
#include "../astronomy/Coordinates.h"

#define OLED_WIDTH 128
#define OLED_HEIGHT 64

class OLEDDisplay {
public:
    OLEDDisplay(SensorManager* sensors, Coordinates* coordinates);

    // Detect and initialize the display.
    bool begin();

    // Returns true if a display was detected and working.
    bool isConnected();

    // Hot-plug: check connection, reinitialize if reconnected
    void checkConnection();

    // Get a diagnostic string (for deferred serial printing)
    String getDiagnostics();

    // Refresh the display with current coordinate data.
    void update();

private:
    Adafruit_SSD1306 _display;
    SensorManager* _sensors;
    Coordinates* _coordinates;
    bool _connected;
    uint8_t _address;
    String _diagnostics;

    // Try to detect an SSD1306 at common addresses
    uint8_t detectAddress();

    void formatRA(double ra, char* buf, size_t len);
    void formatDec(double dec, char* buf, size_t len);
};

#endif // OLED_DISPLAY_H
