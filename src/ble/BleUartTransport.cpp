#include "BleUartTransport.h"

// Advertising interval, in 0.625 ms units. 250-500 ms is responsive enough to
// find and connect to at the start of an observing session without the radio
// duty cycle of a fast-advertising beacon.
#define BLE_ADV_MIN_INTERVAL 400
#define BLE_ADV_MAX_INTERVAL 800

// Connection interval, in 1.25 ms units, and supervision timeout in 10 ms
// units. SkySafari polls a handful of times per second, so a 30-50 ms interval
// keeps replies prompt while leaving the radio idle most of the time.
#define BLE_CONN_MIN_INTERVAL 24
#define BLE_CONN_MAX_INTERVAL 40
#define BLE_CONN_LATENCY 0
#define BLE_CONN_TIMEOUT 400

// ATT overhead: a notification can carry MTU minus three bytes.
#define BLE_ATT_OVERHEAD 3
#define BLE_DEFAULT_MTU 23

namespace {

class ServerCallbacks : public NimBLEServerCallbacks {
public:
    explicit ServerCallbacks(BleUartTransport* owner) : _owner(owner) {}

    void onConnect(NimBLEServer* server, ble_gap_conn_desc* desc) override {
        server->updateConnParams(desc->conn_handle,
                                 BLE_CONN_MIN_INTERVAL,
                                 BLE_CONN_MAX_INTERVAL,
                                 BLE_CONN_LATENCY,
                                 BLE_CONN_TIMEOUT);
        _owner->onClientConnected(desc->conn_handle);
    }

    void onDisconnect(NimBLEServer* server) override {
        (void)server;
        _owner->onClientDisconnected();
    }

    void onMTUChange(uint16_t mtu, ble_gap_conn_desc* desc) override {
        (void)desc;
        _owner->onMtuChanged(mtu);
    }

private:
    BleUartTransport* _owner;
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
public:
    explicit RxCallbacks(BleUartTransport* owner) : _owner(owner) {}

    // Runs on the NimBLE host task. Queue the bytes and return immediately;
    // all protocol work happens on the protocol task.
    void onWrite(NimBLECharacteristic* characteristic) override {
        std::string value = characteristic->getValue();
        if (!value.empty()) {
            _owner->enqueueReceived((const uint8_t*)value.data(), value.length());
        }
    }

private:
    BleUartTransport* _owner;
};

} // namespace

BleUartTransport::BleUartTransport()
    : _server(NULL),
      _txCharacteristic(NULL),
      _rxBuffer(NULL),
      _connected(false),
      _advertising(false),
      _mtu(BLE_DEFAULT_MTU),
      _connectCount(0),
      _disconnectCount(0),
      _bytesReceived(0),
      _bytesSent(0),
      _rxOverflows(0) {}

bool BleUartTransport::begin(const char* deviceName) {
    Serial.println("[BLE] Initializing NimBLE...");

    _rxBuffer = xStreamBufferCreate(BLE_UART_RX_BUFFER_BYTES, 1);
    if (_rxBuffer == NULL) {
        Serial.println("[BLE] FAILED: could not allocate RX buffer");
        return false;
    }

    _deviceName = deviceName;
    NimBLEDevice::init(_deviceName);

    // Modest transmit power: plenty for a phone sitting at the eyepiece, and
    // noticeably cheaper than running the PA flat out.
    NimBLEDevice::setPower(ESP_PWR_LVL_P3);

    _server = NimBLEDevice::createServer();
    if (_server == NULL) {
        Serial.println("[BLE] FAILED: could not create GATT server");
        return false;
    }
    _server->setCallbacks(new ServerCallbacks(this));

    NimBLEService* service = _server->createService(BLE_UART_SERVICE_UUID);
    if (service == NULL) {
        Serial.println("[BLE] FAILED: could not create UART service");
        return false;
    }

    _txCharacteristic = service->createCharacteristic(
        BLE_UART_TX_UUID,
        NIMBLE_PROPERTY::NOTIFY);

    NimBLECharacteristic* rxCharacteristic = service->createCharacteristic(
        BLE_UART_RX_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    rxCharacteristic->setCallbacks(new RxCallbacks(this));

    service->start();

    startAdvertising();

    Serial.printf("[BLE] Ready as \"%s\"\n", deviceName);
    Serial.printf("[BLE] Address: %s\n", NimBLEDevice::getAddress().toString().c_str());
    Serial.printf("[BLE] Nordic UART service %s\n", BLE_UART_SERVICE_UUID);
    // The name lives in the scan response, not the advertisement, because the
    // 128-bit service UUID and the name together overflow one 31-byte packet.
    // A passive scan therefore shows an unnamed device; match it by the UUID.
    Serial.println("[BLE] Name is in the scan response; passive scans show no name");
    return true;
}

void BleUartTransport::startAdvertising() {
    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();

    // The 128-bit service UUID and the device name together overflow a single
    // 31-byte advertisement, so the name goes in the scan response.
    NimBLEAdvertisementData advertisementData;
    advertisementData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
    advertisementData.setCompleteServices(NimBLEUUID(BLE_UART_SERVICE_UUID));

    NimBLEAdvertisementData scanResponseData;
    scanResponseData.setName(_deviceName);

    advertising->setAdvertisementData(advertisementData);
    advertising->setScanResponseData(scanResponseData);
    advertising->setMinInterval(BLE_ADV_MIN_INTERVAL);
    advertising->setMaxInterval(BLE_ADV_MAX_INTERVAL);

    _advertising = advertising->start();
    if (_advertising) {
        Serial.println("[BLE] Advertising started");
    } else {
        Serial.println("[BLE] WARNING: advertising failed to start");
    }
}

void BleUartTransport::onClientConnected(uint16_t connHandle) {
    _connected = true;
    _advertising = false;
    _connectCount++;
    Serial.printf("[BLE] Client connected (handle %u)\n", connHandle);
}

void BleUartTransport::onClientDisconnected() {
    _connected = false;
    _mtu = BLE_DEFAULT_MTU;
    _disconnectCount++;
    Serial.println("[BLE] Client disconnected");
    startAdvertising();
}

void BleUartTransport::onMtuChanged(uint16_t mtu) {
    _mtu = mtu;
    Serial.printf("[BLE] MTU negotiated: %u\n", mtu);
}

void BleUartTransport::enqueueReceived(const uint8_t* data, size_t length) {
    if (_rxBuffer == NULL) return;

    size_t queued = xStreamBufferSend(_rxBuffer, data, length, 0);
    _bytesReceived += queued;

    if (queued < length) {
        _rxOverflows++;
        log_w("[BLE] RX buffer full, dropped %u byte(s)", (unsigned)(length - queued));
    }
}

size_t BleUartTransport::readBytes(uint8_t* buffer, size_t maxLength, TickType_t timeout) {
    if (_rxBuffer == NULL) return 0;
    return xStreamBufferReceive(_rxBuffer, buffer, maxLength, timeout);
}

size_t BleUartTransport::write(const uint8_t* data, size_t length) {
    if (!_connected || _txCharacteristic == NULL) {
        return 0;
    }

    size_t chunkLimit = (_mtu > BLE_ATT_OVERHEAD) ? (size_t)(_mtu - BLE_ATT_OVERHEAD)
                                                  : (BLE_DEFAULT_MTU - BLE_ATT_OVERHEAD);
    size_t sent = 0;

    while (sent < length) {
        size_t chunk = length - sent;
        if (chunk > chunkLimit) chunk = chunkLimit;

        _txCharacteristic->setValue(data + sent, chunk);
        _txCharacteristic->notify();
        sent += chunk;
    }

    _bytesSent += sent;
    return sent;
}

bool BleUartTransport::isConnected() const {
    return _connected;
}

BleLinkStatus BleUartTransport::getStatus() const {
    BleLinkStatus status;
    status.connected = _connected;
    status.advertising = _advertising;
    status.mtu = _mtu;
    status.connectCount = _connectCount;
    status.disconnectCount = _disconnectCount;
    status.bytesReceived = _bytesReceived;
    status.bytesSent = _bytesSent;
    status.rxOverflows = _rxOverflows;
    return status;
}
