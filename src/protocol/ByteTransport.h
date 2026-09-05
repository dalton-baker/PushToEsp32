#ifndef BYTE_TRANSPORT_H
#define BYTE_TRANSPORT_H

#include <Arduino.h>

// A transparent, ordered byte sink.
//
// This is the seam between telescope protocols and whatever carries their
// bytes. BLE happens to be the only implementation today; a protocol handler
// must never need to know that.
class ByteTransport {
public:
    virtual ~ByteTransport() {}

    // Send bytes to the peer. Returns the number actually accepted.
    virtual size_t write(const uint8_t* data, size_t length) = 0;

    // True when a peer is attached and writes have somewhere to go.
    virtual bool isConnected() const = 0;
};

#endif // BYTE_TRANSPORT_H
