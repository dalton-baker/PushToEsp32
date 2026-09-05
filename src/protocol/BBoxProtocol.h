#ifndef BBOX_PROTOCOL_H
#define BBOX_PROTOCOL_H

#include <Arduino.h>
#include "ByteTransport.h"
#include "../telescope/TelescopeState.h"

// Running tally of protocol activity, for diagnostics.
struct BBoxStats {
    uint32_t positionRequests;    // 'Q' commands served
    uint32_t resolutionRequests;  // 'H' commands served
    uint32_t ignoredBytes;        // Bytes that were not a command we implement
    uint32_t discardedBytes;      // Extra commands dropped to stay in step
    uint32_t bytesSent;
    char lastCommand;             // 0 if none yet
    uint32_t lastCommandMs;       // millis() of the last recognised command
};

// SkySafari "Basic Encoder System" (BBox / Tangent-compatible) handler.
//
// Wire format, as used by SkySafari and confirmed against working
// Tangent-compatible digital setting circles:
//
//   'Q'  ->  "+0AAAA\t+0BBBB\r"   azimuth count, tab, altitude count, CR.
//                                  Each field is a sign plus five digits,
//                                  six characters in total.
//   'H'  ->  "RRRR RRRR\r"        the encoder resolution of each axis, as
//                                  PLAIN space-separated integers.
//
// The two replies deliberately do NOT share a number format, and the H
// separator matters. Sending Q's padded signed fields in reply to H makes
// SkySafari's auto steps-per-revolution detection report nonsense, and a hyphen
// separator is read as a minus sign on the second value, which this protocol
// takes to mean "reversed axis". Both were observed against SkySafari on real
// hardware; the space separator is the unambiguous form.
//
// Anything else is discarded: SkySafari is known to emit stray bytes when a
// connection opens, and a byte-transparent bridge will happily forward them.
//
// EXACTLY ONE reply is sent per received chunk. SkySafari periodically emits a
// burst of identical commands in a single write (twelve 'Q's has been observed
// on real hardware) but reads only one reply. Answering every byte leaves it
// permanently N-1 replies behind, which looks like stale pointing and makes the
// resolution query return a position frame. Serving the first command and
// dropping the rest is what keeps request and reply in step.
//
// The handler owns no hardware and no transport. It reads a TelescopeState
// snapshot and writes to a ByteTransport, so the same object works unchanged
// over BLE, USB serial or anything else.
class BBoxProtocol {
public:
    BBoxProtocol(TelescopeState* state, ByteTransport* transport);

    // Push received bytes in. May emit responses via the transport.
    void feed(const uint8_t* data, size_t length);

    // Forget accumulated activity, e.g. when a new client connects.
    void resetStats();

    BBoxStats getStats() const;

private:
    TelescopeState* _state;
    ByteTransport* _transport;
    BBoxStats _stats;

    void handleCommand(char command);
    void sendPosition();
    void sendResolution();
    void sendResponse(const char* response, int length, const char* what);
};

#endif // BBOX_PROTOCOL_H
