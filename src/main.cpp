/**
 * PushTo ESP32 - Telescope Push-To Sensor
 *
 * The ESP32 measures the telescope. SkySafari understands the sky.
 *
 * Hardware:
 * - XIAO ESP32-S3
 * - AS5600 magnetic encoder (azimuth) @ 0x36
 * - MPU6050 accelerometer (altitude) @ 0x68
 * - Optional SSD1306 OLED (diagnostics only) @ 0x3C/0x3D
 * - I2C: SDA=GPIO3 (D2), SCL=GPIO2 (D1)
 *
 * Data flow:
 *   AS5600 + MPU6050 -> SensorManager -> TelescopeState
 *                                          |-> BBoxProtocol -> BLE UART
 *                                          |                     -> Android BLE/TCP bridge
 *                                          |                        -> SkySafari
 *                                          '-> DiagnosticDisplay (optional)
 *
 * There is no Wi-Fi. The radio is only ever brought up as BLE, and no
 * astronomy - alignment, site, time, RA/Dec - happens on this device.
 */

#include <Arduino.h>
#include "sensors/SensorManager.h"
#include "telescope/TelescopeState.h"
#include "protocol/BBoxProtocol.h"
#include "ble/BleUartTransport.h"
#include "display/DiagnosticDisplay.h"

// I2C configuration
#define I2C_SDA 3   // GPIO3 (D2)
#define I2C_SCL 2   // GPIO2 (D1)
#define I2C_FREQ 100000  // 100kHz - kept low for the long Cat5 sensor run

// BLE identity
#define BLE_DEVICE_NAME "PushTo-ESP32"

// CPU clock. 80MHz is the lowest speed that still leaves the APB bus at its
// full 80MHz, so I2C and UART timings are byte-for-byte what they were at
// 240MHz. Going below 80 drags APB down with the CPU and shifts the sensor bus
// timing, which is not worth a few extra milliamps on a long Cat5 run.
#define CPU_FREQ_MHZ 80

// Task timing
#define SENSOR_SAMPLE_PERIOD_MS 10    // 100Hz axis sampling
#define SENSOR_HEALTH_PERIOD_MS 2000  // Hot-plug / magnet health polling
#define DISPLAY_RENDER_PERIOD_MS 500  // 2Hz; a full frame is ~90ms of I2C at 100kHz
#define DISPLAY_PROBE_PERIOD_MS 2000  // Hot-plug polling for the OLED
#define PROTOCOL_WAIT_MS 100          // Max block waiting on inbound BLE bytes

#define PROTOCOL_CHUNK_BYTES 64

#define STATUS_LOG_PERIOD_MS 10000

// Status LED (built-in LED on XIAO ESP32-S3, active low)
#define LED_PIN LED_BUILTIN

// Core objects, wired bottom-up: sensors -> state -> protocol -> transport.
SensorManager sensors;
TelescopeState telescopeState;
BleUartTransport bleUart;
BBoxProtocol bbox(&telescopeState, &bleUart);
DiagnosticDisplay diagnosticDisplay;

TaskHandle_t sensorTaskHandle = NULL;
TaskHandle_t protocolTaskHandle = NULL;
TaskHandle_t displayTaskHandle = NULL;

// Guards the shared I2C bus between the sampling task and the optional display.
SemaphoreHandle_t i2cMutex = NULL;

/**
 * Sensor Task - Core 0
 * Samples both axes at 100Hz and publishes them into TelescopeState. Runs
 * whether or not anything is connected over BLE.
 */
void sensorTask(void* parameter) {
    (void)parameter;
    TickType_t lastWake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(SENSOR_SAMPLE_PERIOD_MS);
    uint32_t lastHealthCheck = 0;

    Serial.printf("[Sensors] Sampling task started on core %d\n", xPortGetCoreID());

    while (true) {
        bool healthDue = (millis() - lastHealthCheck) >= SENSOR_HEALTH_PERIOD_MS;

        if (xSemaphoreTake(i2cMutex, portMAX_DELAY) == pdTRUE) {
            sensors.update();
            if (healthDue) {
                lastHealthCheck = millis();
                sensors.refreshHealth();
            }
            xSemaphoreGive(i2cMutex);
        }

        telescopeState.publish(sensors.getSample(), sensors.getHealth());

        vTaskDelayUntil(&lastWake, period);
    }
}

/**
 * Protocol Task - Core 1
 * Drains bytes received over BLE and hands them to the BBox handler, which
 * replies through the same transport. Blocks when idle, so it costs nothing
 * while no client is talking.
 */
void protocolTask(void* parameter) {
    (void)parameter;
    uint8_t buffer[PROTOCOL_CHUNK_BYTES];

    Serial.printf("[BBox] Protocol task started on core %d\n", xPortGetCoreID());

    while (true) {
        size_t received = bleUart.readBytes(buffer, sizeof(buffer),
                                            pdMS_TO_TICKS(PROTOCOL_WAIT_MS));
        if (received > 0) {
            bbox.feed(buffer, received);
        }
    }
}

/**
 * Display Task - Core 1
 * Drives the optional diagnostic OLED. If no display is attached this task
 * does nothing but probe the bus occasionally; nothing else waits on it.
 */
void displayTask(void* parameter) {
    (void)parameter;
    TickType_t lastWake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(DISPLAY_RENDER_PERIOD_MS);
    uint32_t lastProbe = 0;

    Serial.printf("[Display] Display task started on core %d\n", xPortGetCoreID());

    while (true) {
        // Snapshot first, so the I2C bus is held only for the actual drawing.
        TelescopeSnapshot snapshot = telescopeState.get();
        BleLinkStatus link = bleUart.getStatus();
        BBoxStats protocol = bbox.getStats();

        bool probeDue = (millis() - lastProbe) >= DISPLAY_PROBE_PERIOD_MS;

        if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (probeDue) {
                lastProbe = millis();
                diagnosticDisplay.poll();
            }
            diagnosticDisplay.render(snapshot, link, protocol);
            xSemaphoreGive(i2cMutex);
        }

        vTaskDelayUntil(&lastWake, period);
    }
}

void setup() {
    // Before anything is initialized against the old clock. Serial is USB CDC
    // here, so it has no baud rate to invalidate.
    setCpuFrequencyMhz(CPU_FREQ_MHZ);

    Serial.begin(115200);
    // Wait briefly for the USB CDC serial connection.
    unsigned long serialWaitStart = millis();
    while (!Serial && millis() - serialWaitStart < 3000) {
        delay(10);
    }
    delay(200);

    Serial.println("\n\n========================================");
    Serial.println("   PushTo ESP32 - Telescope Sensor");
    Serial.println("   BLE UART + SkySafari Basic Encoder");
    Serial.println("========================================\n");

    Serial.printf("[Setup] CPU %u MHz, APB %u Hz\n",
                  getCpuFrequencyMhz(), getApbFrequency());

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH); // Active low: start dark

    i2cMutex = xSemaphoreCreateMutex();
    telescopeState.begin();

    Serial.println("[Setup] Initializing sensors...");
    if (!sensors.begin(I2C_SDA, I2C_SCL, I2C_FREQ)) {
        Serial.println("[Setup] WARNING: no telescope sensors responded");
        Serial.println("[Setup] Check I2C wiring; sampling will keep retrying");
    }
    Serial.println();
    Serial.print(sensors.describe());

    // Publish an initial snapshot so consumers never see zeroed state.
    telescopeState.publish(sensors.getSample(), sensors.getHealth());

    Serial.println("\n[Setup] Starting BLE...");
    if (!bleUart.begin(BLE_DEVICE_NAME)) {
        Serial.println("[Setup] ERROR: BLE failed to start - no telescope link");
    }

    Serial.printf("[Setup] Reporting %ld counts/rev on both axes\n",
                  (long)TelescopeState::countsPerRevolution());
    Serial.println("[Setup] Configure SkySafari as a Basic Encoder System with");
    Serial.println("[Setup] that value for both Az and Alt steps per revolution.");

    // Optional hardware: absence here is expected, not an error.
    Serial.println("\n[Setup] Checking for diagnostic display...");
    diagnosticDisplay.begin();

    xTaskCreatePinnedToCore(sensorTask, "SensorTask", 4096, NULL, 3, &sensorTaskHandle, 0);
    xTaskCreatePinnedToCore(protocolTask, "ProtocolTask", 4096, NULL, 2, &protocolTaskHandle, 1);
    xTaskCreatePinnedToCore(displayTask, "DisplayTask", 4096, NULL, 1, &displayTaskHandle, 1);

    Serial.println("\n[Setup] System initialized");
    Serial.printf("[Setup] Sensors  core 0 @ %d Hz\n", 1000 / SENSOR_SAMPLE_PERIOD_MS);
    Serial.println("[Setup] Protocol core 1, event driven");
    Serial.printf("[Setup] Display  core 1 @ %d Hz (optional)\n", 1000 / DISPLAY_RENDER_PERIOD_MS);
    Serial.println("========================================\n");
}

void loop() {
    static uint32_t lastStatus = 0;
    static bool firstStatus = true;
    static bool ledState = false;

    vTaskDelay(pdMS_TO_TICKS(1000));

#if PUSHTO_TRACE
    // Raw gravity vector, for working out how the accelerometer is actually
    // mounted. Every candidate pitch formula is printed alongside so the right
    // one can be picked from measured data rather than assumed.
    {
        TelescopeSnapshot s = telescopeState.get();
        float ax = s.accelX, ay = s.accelY, az = s.accelZ;
        float mag = sqrtf(ax*ax + ay*ay + az*az);
        Serial.printf("[accel] ax=%7.3f ay=%7.3f az=%7.3f |g|=%6.3f%s | "
                      "X=%7.2f Y=%7.2f Z=%7.2f | shipped=%7.2f\n",
                      ax, ay, az, mag,
                      (mag < 9.4f || mag > 10.2f) ? " BAD!" : " ok",
                      atan2f(-ax, sqrtf(ay*ay + az*az)) * 180.0f / PI,
                      atan2f(-ay, sqrtf(ax*ax + az*az)) * 180.0f / PI,
                      atan2f(-az, sqrtf(ax*ax + ay*ay)) * 180.0f / PI,
                      s.altitude.degrees);
    }
#endif

    // Slow heartbeat while a client is attached, dark otherwise.
    if (bleUart.isConnected()) {
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState ? LOW : HIGH);
    } else {
        digitalWrite(LED_PIN, HIGH);
    }

    if (millis() - lastStatus < STATUS_LOG_PERIOD_MS) {
        return;
    }
    lastStatus = millis();

    // USB CDC misses the earliest boot output, so repeat it once.
    if (firstStatus) {
        firstStatus = false;
        Serial.println("\n=== Startup Diagnostics (reprinted) ===");
        Serial.print(sensors.describe());
        Serial.print(diagnosticDisplay.describe());
        Serial.println("========================================");
    }

    TelescopeSnapshot snapshot = telescopeState.get();
    BleLinkStatus link = bleUart.getStatus();
    BBoxStats protocol = bbox.getStats();

    Serial.println("\n--- Status ---");
    Serial.printf("Az %8.2f deg  raw %5d  count %5d  %s\n",
                  snapshot.azimuth.degrees, (int)snapshot.azimuth.rawValue,
                  (int)snapshot.azimuth.count, snapshot.azimuth.valid ? "OK" : "NO SENSOR");
    Serial.printf("Alt %7.2f deg  raw %6.2f count %5d  %s\n",
                  snapshot.altitude.degrees, snapshot.altitude.rawValue,
                  (int)snapshot.altitude.count, snapshot.altitude.valid ? "OK" : "NO SENSOR");
    Serial.printf("Gravity |g|=%.3f m/s^2 %s\n",
                  snapshot.gravityMagnitude,
                  (snapshot.gravityMagnitude > 9.3f && snapshot.gravityMagnitude < 10.3f)
                      ? "(ok)" : "(BAD - accelerometer misconfigured)");
    Serial.printf("Magnet: detected=%s weak=%s strong=%s agc=%u mag=%u\n",
                  snapshot.health.magnetDetected ? "yes" : "no",
                  snapshot.health.magnetTooWeak ? "yes" : "no",
                  snapshot.health.magnetTooStrong ? "yes" : "no",
                  snapshot.health.magnetAgc, snapshot.health.magnetMagnitude);
    Serial.printf("BLE: %s  conns=%lu  mtu=%u  rx=%lu tx=%lu drops=%lu\n",
                  link.connected ? "connected" : (link.advertising ? "advertising" : "idle"),
                  (unsigned long)link.connectCount, link.mtu,
                  (unsigned long)link.bytesReceived, (unsigned long)link.bytesSent,
                  (unsigned long)link.rxOverflows);
    Serial.printf("BBox: Q=%lu H=%lu ignored=%lu discarded=%lu\n",
                  (unsigned long)protocol.positionRequests,
                  (unsigned long)protocol.resolutionRequests,
                  (unsigned long)protocol.ignoredBytes,
                  (unsigned long)protocol.discardedBytes);
    Serial.printf("Display: %s   Free heap: %u bytes\n",
                  diagnosticDisplay.isPresent() ? "attached" : "not attached",
                  ESP.getFreeHeap());
    Serial.println("--------------\n");
}
