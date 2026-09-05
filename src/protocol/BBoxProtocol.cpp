#include "BBoxProtocol.h"

// Position fields: a sign character followed by five digits, six in total.
#define BBOX_POSITION_FORMAT "%+06ld\t%+06ld\r"

// Resolution reply: plain integers, space separated, NOT the padded signed form
// used above. Two separators have been ruled out on real hardware:
//   "+04096\t+04096\r"  padded signed fields -> auto-detect reads nonsense
//   "4096-4096\r\n"      hyphen is read as a MINUS on the second value, which
//                         in this protocol means "reversed axis"
// A space cannot be mistaken for a sign, which is what the Tangent
// documentation shows: "10000 -10000<CR>", the minus there being a genuine
// reversed-axis marker rather than a separator.
#define BBOX_RESOLUTION_FORMAT "%ld %ld\r"
#define BBOX_RESPONSE_MAX 32

// Temporary wire tracing. Enabled with -DPUSHTO_TRACE=1 while diagnosing how
// SkySafari associates replies with requests; not for normal operation, since
// it logs on every poll.
#if PUSHTO_TRACE
static void traceBytes(const char* dir, const char* data, int length) {
    Serial.printf("[wire %8lu] %s \"", (unsigned long)millis(), dir);
    for (int i = 0; i < length; i++) {
        unsigned char c = (unsigned char)data[i];
        if (c == '\t')      Serial.print("\\t");
        else if (c == '\r') Serial.print("\\r");
        else if (c == '\n') Serial.print("\\n");
        else if (c >= 32 && c < 127) Serial.print((char)c);
        else Serial.printf("\\x%02X", c);
    }
    Serial.printf("\" (%d bytes)\n", length);
}
#endif

BBoxProtocol::BBoxProtocol(TelescopeState* state, ByteTransport* transport)
    : _state(state), _transport(transport) {
    resetStats();
}

void BBoxProtocol::resetStats() {
    memset(&_stats, 0, sizeof(_stats));
}

BBoxStats BBoxProtocol::getStats() const {
    return _stats;
}

void BBoxProtocol::feed(const uint8_t* data, size_t length) {
#if PUSHTO_TRACE
    traceBytes("RX", (const char*)data, (int)length);
#endif
    // Serve at most one command per chunk. SkySafari sends bursts of identical
    // commands in a single write and reads only one reply for the burst;
    // answering each byte desynchronises request from reply permanently.
    for (size_t i = 0; i < length; i++) {
        char c = (char)data[i];

        // Line endings and padding are framing, not input. SkySafari pads
        // commands inconsistently; silently absorbing this keeps the error
        // counter meaningful.
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t' || c == '\0') {
            continue;
        }

        handleCommand(c);

        size_t remaining = length - i - 1;
        if (remaining > 0) {
            _stats.discardedBytes += remaining;
            log_d("[BBox] Dropped %u trailing byte(s) to stay in step",
                  (unsigned)remaining);
        }
        return;
    }
}

void BBoxProtocol::handleCommand(char command) {
    switch (command) {
        case 'Q':
        case 'q':
            _stats.positionRequests++;
            _stats.lastCommand = 'Q';
            _stats.lastCommandMs = millis();
            sendPosition();
            break;

        case 'H':
        case 'h':
            _stats.resolutionRequests++;
            _stats.lastCommand = 'H';
            _stats.lastCommandMs = millis();
            sendResolution();
            break;

        default:
            _stats.ignoredBytes++;
            log_d("[BBox] Ignoring unsupported byte 0x%02X ('%c')",
                  (uint8_t)command,
                  (command >= 32 && command < 127) ? command : '.');
            break;
    }
}

void BBoxProtocol::sendPosition() {
    TelescopeSnapshot snapshot = _state->get();

    char response[BBOX_RESPONSE_MAX];
    int written = snprintf(response, sizeof(response), BBOX_POSITION_FORMAT,
                           (long)snapshot.azimuth.count,
                           (long)snapshot.altitude.count);
    sendResponse(response, written, "position");
}

void BBoxProtocol::sendResolution() {
    long resolution = (long)TelescopeState::countsPerRevolution();

    char response[BBOX_RESPONSE_MAX];
    int written = snprintf(response, sizeof(response), BBOX_RESOLUTION_FORMAT,
                           resolution, resolution);
    sendResponse(response, written, "resolution");
}

void BBoxProtocol::sendResponse(const char* response, int length, const char* what) {
    if (length <= 0 || length >= BBOX_RESPONSE_MAX) {
        log_e("[BBox] Failed to format %s response", what);
        return;
    }

#if PUSHTO_TRACE
    traceBytes(strcmp(what, "resolution") == 0 ? "TX(H)" : "TX(Q)", response, length);
#endif

    size_t sent = _transport->write((const uint8_t*)response, (size_t)length);
    _stats.bytesSent += sent;

    log_d("[BBox] %s -> %d bytes, %u sent", what, length, (unsigned)sent);
}
