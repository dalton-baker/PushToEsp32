#ifndef BLE_UART_TRANSPORT_H
#define BLE_UART_TRANSPORT_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>
#include "../protocol/ByteTransport.h"

// Nordic UART Service - the de-facto standard "BLE serial port", which is what
// generic Android BLE-to-TCP bridge apps look for.
#define BLE_UART_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_UART_RX_UUID      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // Client writes here
#define BLE_UART_TX_UUID      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // Notifications to client

#define BLE_UART_RX_BUFFER_BYTES 256

struct BleLinkStatus {
    bool connected;
    bool advertising;
    uint16_t mtu;
    uint32_t connectCount;
    uint32_t disconnectCount;
    uint32_t bytesReceived;
    uint32_t bytesSent;
    uint32_t rxOverflows;
};

// BLE serial transport. Received bytes are queued from the BLE callback into a
// stream buffer and drained by whichever task calls readBytes(), so no protocol
// or sensor work ever runs inside a BLE callback.
class BleUartTransport : public ByteTransport {
public:
    BleUartTransport();

    bool begin(const char* deviceName);

    // ByteTransport
    size_t write(const uint8_t* data, size_t length) override;
    bool isConnected() const override;

    // Block until bytes arrive or the timeout expires. Returns bytes read.
    size_t readBytes(uint8_t* buffer, size_t maxLength, TickType_t timeout);

    BleLinkStatus getStatus() const;

    // Called from NimBLE callbacks; not part of the public surface.
    void onClientConnected(uint16_t connHandle);
    void onClientDisconnected();
    void onMtuChanged(uint16_t mtu);
    void enqueueReceived(const uint8_t* data, size_t length);

private:
    NimBLEServer* _server;
    NimBLECharacteristic* _txCharacteristic;
    StreamBufferHandle_t _rxBuffer;
    std::string _deviceName;

    volatile bool _connected;
    volatile bool _advertising;
    volatile uint16_t _mtu;
    volatile uint32_t _connectCount;
    volatile uint32_t _disconnectCount;
    volatile uint32_t _bytesReceived;
    volatile uint32_t _bytesSent;
    volatile uint32_t _rxOverflows;

    void startAdvertising();
};

#endif // BLE_UART_TRANSPORT_H
