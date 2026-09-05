# PushTo ESP32 - Telescope Push-To Sensor

A telescope push-to sensor built on the XIAO ESP32-S3. It measures where an
alt-az mount is pointing and reports the two axes over BLE using SkySafari's
Basic Encoder System (BBox) protocol.

The guiding rule of this firmware:

> **The ESP32 measures the telescope. SkySafari understands the sky.**

The device does no astronomy. It holds no site coordinates, no clock, no star
catalog and no alignment model. It reports encoder-equivalent axis counts, and
SkySafari owns the pointing model.

## Architecture

```
   AS5600 (az)      MPU6050 (alt)
        |                 |
        +--------+--------+
                 |
          SensorManager          owns I2C hardware, produces samples + health
                 |
          TelescopeState         normalized axis state, counts, sensor health
                 |
        +--------+------------------------+
        |                                 |
   BBoxProtocol                   DiagnosticDisplay  (optional, plug-in only)
        |
   BleUartTransport               Nordic UART Service
        |
   Android BLE <-> TCP bridge
        |
   SkySafari (Basic Encoder System)
```

Layering rules the code holds to:

- Sensor drivers know nothing about protocols or transports.
- `BBoxProtocol` talks to an abstract `ByteTransport`, never to BLE directly,
  so another protocol or another transport can be added without restructuring.
- `TelescopeState` is the single point of truth and is transport-agnostic.
- Nothing depends on the display. It is a leaf that only reads snapshots.

There is no Wi-Fi. The Wi-Fi, lwIP and HTTP stacks are not merely disabled,
they are not linked into the firmware at all.

## Hardware

Unchanged from the original build.

### Components
- **XIAO ESP32-S3** - main microcontroller (BLE only; the S3 has no Bluetooth Classic)
- **AS5600** - 12-bit magnetic angle sensor (azimuth)
- **MPU6050** - 6-axis accelerometer (altitude)
- **SSD1306 128x64 OLED** - *optional*, diagnostics only
- **Cat5 cable** - twisted pair wiring for I2C signals

### Wiring

```
ESP32-S3 Pin | Function | Connected To
-------------|----------|-------------
GPIO3 (D2)   | I2C SDA  | AS5600 SDA, MPU6050 SDA
GPIO2 (D1)   | I2C SCL  | AS5600 SCL, MPU6050 SCL
3.3V         | Power    | AS5600 VCC, MPU6050 VCC
GND          | Ground   | AS5600 GND, MPU6050 GND
```

### I2C Addresses
- **AS5600**: 0x36
- **MPU6050**: 0x68 (AD0 pin to GND)
- **SSD1306**: 0x3C or 0x3D (auto-detected, optional)

I2C runs at 100kHz, kept deliberately low for the long Cat5 sensor run.
The **DIR pin** on the AS5600 sets rotation direction; tie it to GND or 3V3.

## Build

```bash
pio run                    # build
pio run --target upload    # flash
pio device monitor         # serial diagnostics at 115200
```

No filesystem image is used and nothing needs uploading beyond the firmware.

## Connecting SkySafari

SkySafari has no generic BLE-serial transport, so a bridge app relays bytes
between the ESP32's BLE UART and a local TCP socket on the phone.

1. **Pick up the BLE device** in an Android BLE-to-TCP bridge app.
   - Device name: `PushTo-ESP32`
   - Service: Nordic UART, `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
   - RX (phone writes): `...0002-...`  TX (notifies): `...0003-...`
2. **Point the bridge at a local TCP port** (4030 matches convention).
3. **In SkySafari**: Settings -> Telescope -> Setup
   - Scope Type: **Basic Encoder System**
   - Mount Type: **Alt-Az Push-To**
   - Connection: TCP/IP, the bridge's address and port
   - **Az and Alt steps per revolution: 4096** (both axes)

If SkySafari cannot reach `127.0.0.1`, the bridge's TCP server may not bind the
loopback interface. Bring up a Wi-Fi interface on the phone (join any network,
or enable its hotspot) and point SkySafari at that interface's IP instead.

## BBox protocol

Implemented in [BBoxProtocol.cpp](src/protocol/BBoxProtocol.cpp). Two commands,
each field a sign plus five digits, tab separated, CR terminated:

| Request | Response          | Meaning                        |
|---------|-------------------|--------------------------------|
| `Q`     | `+0AAAA\t+0BBBB\r` | Azimuth count, altitude count  |
| `H`     | `4096-4096\r\n`    | Steps per revolution, per axis |

The two replies use deliberately different number formats. `Q` uses padded
signed six-character fields; `H` uses plain integers. Replying to `H` in `Q`'s
format makes SkySafari's automatic steps-per-revolution detection report a
wildly wrong value, so if auto-detect misbehaves, check this first.

Any other byte is discarded and counted as ignored input; SkySafari is known to
emit stray bytes when a connection opens.

### Encoder counts

Both axes report **4096 counts per revolution**.

- **Azimuth** is the AS5600's native 12-bit resolution, so counts map 1:1 onto
  raw encoder ticks with no requantization.
- **Altitude** has no real encoder. The accelerometer angle is scaled onto the
  same synthetic scale, and negative altitudes wrap into the upper half of the
  range exactly as a real altitude encoder would report them.

Constant offsets on either axis are absorbed by SkySafari's own alignment, so
the firmware deliberately keeps no calibration offsets of its own.

## The diagnostic display

The OLED is entirely optional and normally not attached. The telescope behaves
identically with it unplugged: a missing display never blocks startup, never
retries, and nothing waits on it. It can be hot-plugged at any time and is
picked up within a couple of seconds.

It rotates through three pages every 4 seconds:

1. **AXES** - az/alt in degrees, raw sensor values, and the counts being reported
2. **MAGNET** - AS5600 magnet detection, gap (too far / too close), AGC,
   magnitude, and sensor presence
3. **BLE / BBOX** - link state, MTU, `Q` and `H` request counts, ignored bytes,
   dropped bytes, and time since the last request

A status bar shows axis and link health plus the page number on every page.

## Configuration

Timing and pins live at the top of [main.cpp](src/main.cpp):

```cpp
#define CPU_FREQ_MHZ 80               // Lowest speed that keeps APB at 80MHz
#define I2C_SDA 3                     // GPIO3 (D2)
#define I2C_SCL 2                     // GPIO2 (D1)
#define I2C_FREQ 100000               // 100kHz
#define SENSOR_SAMPLE_PERIOD_MS 10    // 100Hz axis sampling
#define DISPLAY_RENDER_PERIOD_MS 500  // 2Hz; a frame is ~90ms of I2C at 100kHz
```

The altitude rolling-average window is `AVERAGE_SAMPLES` in
[SensorManager.h](src/sensors/SensorManager.h). The reported resolution is
`TELESCOPE_COUNTS_PER_REV` in [TelescopeState.h](src/telescope/TelescopeState.h);
change it and SkySafari's steps-per-revolution setting must change to match.

## Tasks

| Task     | Core | Rate         | Job                                        |
|----------|------|--------------|--------------------------------------------|
| Sensor   | 0    | 100 Hz       | Sample both axes, publish to TelescopeState |
| Protocol | 1    | event driven | Drain BLE bytes, serve BBox requests        |
| Display  | 1    | 2 Hz         | Draw the optional diagnostic dashboard      |

Sampling runs whether or not anything is connected over BLE. The protocol task
blocks while idle, so it costs nothing when no client is talking. A shared
mutex serializes the I2C bus between the sampling task and the display.

## Power

- Wi-Fi is gone entirely, along with its stack.
- **CPU runs at 80MHz.** This is the lowest clock that still leaves the APB bus
  at its full 80MHz, so I2C and UART timings are identical to 240MHz operation.
  Anything below 80 drags APB down with the CPU and shifts the sensor bus
  timing. The clock is set before any peripheral is initialized against it.
- **The MPU6050's gyro and temperature sensor are parked.** Altitude comes from
  the accelerometer alone, and the gyro is roughly 3.5mA of the chip's 3.8mA.
- BLE advertises at 250-500 ms, not at beacon duty cycles.
- Connection interval is negotiated to 30-50 ms: prompt enough for SkySafari's
  polling, idle the rest of the time.
- TX power is set to 3 dBm rather than maximum.
- The display is normally unplugged. When absent its frame buffer is never
  allocated, so it costs neither RAM nor current.

Estimated draw is roughly **40-50mA at 3.3V**, from datasheet figures rather
than measurement. The SoC never sleeps, which sets the floor; going lower would
mean light sleep between BLE connection events, at some cost to link
reliability. Note that the board's LDO drops out as a LiPo sags toward 3.3V, so
usable cell capacity is roughly 85% of nominal.

## Diagnostics

Uncomment `-DPUSHTO_TRACE=1` in `platformio.ini` to log every protocol byte in
both directions plus the raw gravity vector at 1Hz:

```
[wire 41722] RX "QQQQQQQQQQQQ" (12 bytes)
[wire 41723] TX(Q) "+01223\t+00193\r" (14 bytes)
[accel] ax= 1.202 ay= 0.707 az=-10.888 |g|= 9.81 ok | ...
```

It is off by default because it logs on every poll. It is what identified both
the SkySafari command-burst desync and the uninitialised accelerometer.

The 10-second status block always reports the gravity magnitude. **It must stay
near 9.81 in every orientation** - that single number is the cheapest way to
catch a misconfigured accelerometer, which otherwise looks plausible while
stationary and only goes wrong when the tube moves.

## Troubleshooting

Serial output at 115200 covers boot, sensor init and failures, BLE init and
connect/disconnect, and invalid protocol input. Raise `CORE_DEBUG_LEVEL` to 4 in
`platformio.ini` to log every BBox request and response. A status block prints
every 10 seconds; per-sample logging is deliberately absent.

### Altitude is wrong but azimuth is fine
1. Check `Gravity |g|` in the status block. If it is not ~9.81, or changes as
   you tilt the tube, the accelerometer is misconfigured or its bias is wrong -
   fix that before suspecting anything else.
2. The board may not be a genuine MPU6050. Boot logs report `WHO_AM_I`; 0x68 is
   an MPU6050, 0x70 an MPU6500. Adafruit's library rejects anything but 0x68 and
   skips its own initialisation, which is why this firmware resets and
   configures the device itself.
3. `ACCEL_BIAS_*` in [SensorManager.h](src/sensors/SensorManager.h) is specific
   to one chip. See the comment there for how it was measured.

### Sensors not detected
1. Check SDA/SCL wiring and that VCC measures 3.3V.
2. Watch the serial log: sensors are re-probed every 2 seconds and reconnect
   on their own.
3. **AS5600 magnet**: check the MAGNET page or the serial status block. The
   magnet should be 1-3mm from the sensor, and AGC around 128.

### SkySafari will not connect
1. Confirm the bridge app is connected to `PushTo-ESP32` - the BLE/BBOX page
   and the onboard LED both show link state.
2. Confirm the bridge's TCP port matches SkySafari's, and see the loopback note
   above.
3. If `Q` counts stay at zero, the bytes are not reaching the ESP32; if they
   climb but SkySafari shows nothing, check steps-per-revolution is 4096.

## Future

The BLE layer is deliberately kept as a general transport rather than a
SkySafari-specific hack, so a Web Bluetooth configuration UI or a separate GATT
configuration service (calibration, battery, firmware info) can be added later
without touching the sensor or protocol layers.
