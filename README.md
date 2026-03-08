# PushTo ESP32 - Telescope Push-To System

A complete telescope push-to system using ESP32-S3 with digital encoders and accelerometer for accurate alt-azimuth position tracking.

## 🔭 Features

- **Dual-Core FreeRTOS Architecture**
  - Core 0: High-speed sensor reading (100Hz)
  - Core 1: Network handling and LX200 protocol (10Hz)

- **Accurate Position Tracking**
  - AS5600 12-bit magnetic encoder for azimuth
  - MPU6050 accelerometer for altitude measurement
  - 16-sample rolling average for smooth altitude readings

- **LX200 Protocol Support**
  - Compatible with Stellarium, SkySafari, and other planetarium software
  - TCP connection over WiFi
  - Push-to only (GoTo commands acknowledged but not executed)

- **Web-Based Configuration**
  - WiFi captive portal for easy setup
  - Site configuration (latitude, longitude, timezone)
  - 2-star alignment procedure
  - Real-time diagnostics and sensor status

## 📋 Hardware Requirements

### Components
- **XIAO ESP32-S3** - Main microcontroller
- **AS5600** - 12-bit magnetic angle sensor (azimuth)
- **MPU6050** - 6-axis accelerometer/gyroscope (altitude)
- **Cat5 cable** - Twisted pair wiring for I2C signals

### Wiring

```
ESP32-S3 Pin | Function | Connected To
-------------|----------|-------------
GPIO5 (A4)   | I2C SDA  | AS5600 SDA, MPU6050 SDA
GPIO6 (A5)   | I2C SCL  | AS5600 SCL, MPU6050 SCL
3.3V         | Power    | AS5600 VCC, MPU6050 VCC
GND          | Ground   | AS5600 GND, MPU6050 GND
```

### I2C Addresses
- **AS5600**: 0x36
- **MPU6050**: 0x68 (AD0 pin to GND)

### I2C Configuration
- **Clock frequency**: 50kHz (configurable 10-50kHz)
- **DIR pin** on AS5600: Tie to GND or 3V3 to set rotation direction

## 🚀 Getting Started

### 1. Install Development Environment

Install [Visual Studio Code](https://code.visualstudio.com/) and the [PlatformIO extension](https://platformio.org/install/ide?install=vscode).

### 2. Clone and Build

```bash
# Open this folder in VS Code
# PlatformIO will automatically install dependencies

# Build the project
pio run

# Upload to ESP32-S3
pio run --target upload

# Monitor serial output
pio device monitor
```

### 3. Initial Setup

1. **Connect to WiFi**
   - After first boot, ESP32 creates a WiFi access point
   - SSID: `PushTo-Setup`
   - Password: `telescope`

2. **Configure Site**
   - Connect to the WiFi network
   - Browser should automatically open the configuration portal
   - If not, navigate to `192.168.4.1`
   - Enter your latitude, longitude, and timezone

3. **Perform Alignment**
   - Point telescope at a known bright star
   - Enter the star's RA/Dec coordinates in the alignment page
   - Click "Set Star 1"
   - Repeat for a second star (ideally 60-90° away)
   - System will compute alignment correction

### 4. Connect Planetarium Software

Configure your planetarium software to connect via LX200 protocol:

**Stellarium:**
- Tools → Configuration → Plugins → Telescope Control
- Add telescope: LX200/Meade
- Connection: TCP/IP
- Host: `192.168.4.1` (or ESP32 IP address)
- Port: `4030`

**SkySafari:**
- Settings → Telescope → Setup
- Type: Meade LX200 Classic
- Connection: WiFi
- IP Address: `192.168.4.1`
- Port: `4030`

## 📁 Project Structure

```
PushToEsp32/
├── platformio.ini              # PlatformIO configuration
├── src/
│   ├── main.cpp               # Main program with FreeRTOS tasks
│   ├── config/
│   │   ├── Config.h           # Configuration management
│   │   └── Config.cpp
│   ├── sensors/
│   │   ├── AS5600.h           # AS5600 encoder driver
│   │   ├── AS5600.cpp
│   │   ├── SensorManager.h    # Unified sensor interface
│   │   └── SensorManager.cpp
│   ├── astronomy/
│   │   ├── Coordinates.h      # Coordinate transformations
│   │   └── Coordinates.cpp
│   ├── lx200/
│   │   ├── LX200Server.h      # LX200 protocol server
│   │   └── LX200Server.cpp
│   └── web/
│       ├── CaptivePortal.h    # Web configuration interface
│       └── CaptivePortal.cpp
└── README.md
```

## 🔧 Configuration

### I2C Settings

Edit [main.cpp](src/main.cpp) to change I2C configuration:

```cpp
#define I2C_SDA 5         // SDA pin
#define I2C_SCL 6         // SCL pin
#define I2C_FREQ 50000    // Clock frequency (Hz)
```

### Sensor Polling Rates

```cpp
#define SENSOR_READ_RATE_MS 10          // 100Hz sensor reading
#define POSITION_UPDATE_RATE_MS 100      // 10Hz position updates
```

### Altitude Averaging

Change the number of samples in [SensorManager.h](src/sensors/SensorManager.h):

```cpp
#define AVERAGE_SAMPLES 16    // Rolling average window
```

## 📡 LX200 Protocol Commands

Supported commands:

| Command | Description | Response |
|---------|-------------|----------|
| `:GR#` | Get Right Ascension | `HH:MM:SS#` |
| `:GD#` | Get Declination | `±DD*MM:SS#` |
| `:GA#` | Get Altitude | `±DD*MM:SS#` |
| `:GZ#` | Get Azimuth | `DDD*MM:SS#` |
| `:Sr HH:MM:SS#` | Set Target RA | `1` |
| `:Sd ±DD*MM:SS#` | Set Target Dec | `1` |
| `:MS#` | Slew to Target | `0` (acknowledged, no action) |
| `:CM#` | Sync to Target | `Coordinates matched.#` |
| `:GVP#` | Get Product Name | `PushTo-ESP32#` |
| `:Gt#` | Get Site Latitude | `±DD*MM:SS#` |
| `:Gg#` | Get Site Longitude | `±DDD*MM:SS#` |

## 🐛 Troubleshooting

### Sensors Not Detected

1. **Check I2C connections**
   - Verify SDA and SCL are connected correctly
   - Check for loose connections
   - Measure voltage: Should be 3.3V on VCC

2. **Verify addresses**
   ```cpp
   // In main.cpp setup(), check I2C scanner:
   Wire.begin(I2C_SDA, I2C_SCL);
   Wire.setClock(I2C_FREQ);
   
   for (uint8_t addr = 1; addr < 127; addr++) {
       Wire.beginTransmission(addr);
       if (Wire.endTransmission() == 0) {
           Serial.printf("Found device at 0x%02X\n", addr);
       }
   }
   ```

3. **AS5600 Magnet Issues**
   - Check diagnostics page for magnet status
   - Magnet should be 1-3mm from sensor
   - AGC should be 128 ± 50 (optimal)

### Connection Issues

- **Can't find WiFi network**: Wait 30 seconds after boot
- **Captive portal doesn't open**: Manually navigate to `192.168.4.1`
- **Stellarium won't connect**: Check firewall settings, verify port 4030

### Position Accuracy

- **Altitude drifts**: Re-calibrate altitude offset in diagnostics page
- **Azimuth jumps**: Check magnet alignment and AS5600 mounting
- **RA/Dec incorrect**: Verify site coordinates and perform 2-star alignment

## 🔄 Calibration

### Altitude Calibration

Point telescope horizontally (0° altitude) and run:
```cpp
sensors.calibrateAltitude();
```
Or use the web interface diagnostics page.

### Azimuth Zero Point

Point telescope to true north (or any known azimuth) and run:
```cpp
sensors.setAzimuthZero();
```

## 📊 Performance

- **Sensor reading**: 100Hz (10ms update rate)
- **Position output**: 10Hz (100ms update rate)
- **LX200 response time**: < 50ms typical
- **Altitude precision**: ~0.1° (with 16-sample averaging)
- **Azimuth precision**: ~0.09° (12-bit resolution)

## 🛠️ Future Enhancements

- [ ] Full 2-star alignment matrix computation
- [ ] Periodic error correction (PEC)
- [ ] Gyroscope fusion for smoother tracking
- [ ] Bluetooth connectivity option
- [ ] Battery power monitoring
- [ ] Field rotation calculation
- [ ] Object database and guided tours

## 📝 License

MIT License - Feel free to modify and use for your own projects!

## 🤝 Contributing

Contributions welcome! Please open an issue or submit a pull request.

## 📧 Support

For questions or issues, please use the GitHub issue tracker.

---

**Happy observing! 🌟**
