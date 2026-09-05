#ifndef DIAGNOSTIC_DISPLAY_H
#define DIAGNOSTIC_DISPLAY_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include "../telescope/TelescopeState.h"
#include "../protocol/BBoxProtocol.h"
#include "../ble/BleUartTransport.h"

#define OLED_WIDTH 128
#define OLED_HEIGHT 64

// How long each diagnostic page stays up before rotating.
#define DISPLAY_PAGE_DURATION_MS 4000

// Consecutive failed probes before the display is treated as unplugged.
#define DISPLAY_ABSENT_THRESHOLD 2

enum DiagnosticPage {
    PAGE_AXES = 0,   // Angles and the encoder counts being reported
    PAGE_MAGNET,     // AS5600 magnet health and sensor presence
    PAGE_LINK,       // BLE link state and BBox protocol activity
    PAGE_COUNT
};

// Optional plug-in diagnostic dashboard.
//
// This is a pure consumer: it reads snapshots produced elsewhere and never
// computes, owns or mutates telescope state. Nothing in the firmware depends
// on it, and every entry point is safe to call with no display attached.
class DiagnosticDisplay {
public:
    DiagnosticDisplay();

    // Probe for a display. Returns false if none is attached, which is a
    // normal, non-fatal outcome.
    bool begin();

    // Re-probe for a hot-plugged display. Call at a low rate.
    void poll();

    // Redraw. A no-op when no display is attached.
    void render(const TelescopeSnapshot& snapshot,
                const BleLinkStatus& link,
                const BBoxStats& protocol);

    bool isPresent() const;

    // Deferred boot message, since USB CDC misses very early serial output.
    String describe() const;

private:
    Adafruit_SSD1306 _display;
    bool _present;
    uint8_t _address;
    String _diagnostics;
    uint8_t _missCount;
    uint8_t _page;
    uint32_t _lastPageChangeMs;

    uint8_t detectAddress();
    bool initPanel();
    void applyNightBrightness();

    void drawAxes(const TelescopeSnapshot& snapshot);
    void drawMagnet(const TelescopeSnapshot& snapshot);
    void drawLink(const BleLinkStatus& link, const BBoxStats& protocol);
    void drawStatusBar(const TelescopeSnapshot& snapshot, const BleLinkStatus& link);
};

#endif // DIAGNOSTIC_DISPLAY_H
