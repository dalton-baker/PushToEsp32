/**
 * PushTo ESP32 - Telescope Push-To System
 * 
 * Hardware:
 * - XIAO ESP32-S3
 * - AS5600 magnetic encoder (azimuth) @ 0x36
 * - MPU6050 accelerometer (altitude) @ 0x68
 * - I2C: SDA=GPIO5, SCL=GPIO6
 * 
 * Features:
 * - FreeRTOS dual-core: sensor reading (core 0), WiFi/LX200 (core 1)
 * - 100Hz sensor polling, 10Hz position updates
 * - Rolling average (16 samples) for altitude
 * - Captive portal for configuration
 * - LX200 protocol over TCP
 * - 2-star alignment
 */

#include <Arduino.h>
#include <WiFi.h>
#include "config/Config.h"
#include "sensors/SensorManager.h"
#include "astronomy/Coordinates.h"
#include "lx200/LX200Server.h"
#include "web/CaptivePortal.h"
#include "display/OLEDDisplay.h"

// I2C Configuration
#define I2C_SDA 3   // GPIO3 (D2)
#define I2C_SCL 2   // GPIO2 (D1)
#define I2C_FREQ 100000  // 100kHz (standard I2C speed)

// Task timing
#define SENSOR_READ_RATE_MS 10   // 100Hz
#define POSITION_UPDATE_RATE_MS 100  // 10Hz

// Global objects
Config config;
SensorManager sensors;
Coordinates coordinates(&config, &sensors);
LX200Server lx200(&coordinates, &config);
CaptivePortal portal(&config, &sensors, &coordinates);
OLEDDisplay oled(&sensors, &coordinates);

// Task handles
TaskHandle_t displayTaskHandle = NULL;
TaskHandle_t sensorTaskHandle = NULL;
TaskHandle_t networkTaskHandle = NULL;


// Shared data protection
SemaphoreHandle_t positionMutex;

// Status LED (built-in LED on XIAO ESP32-S3)
#define LED_PIN LED_BUILTIN

/**
 * Sensor Reading Task - Core 0
 * Reads sensors at high frequency (100Hz)
 * Performs rolling average on altitude
 */
void sensorTask(void* parameter) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(SENSOR_READ_RATE_MS);
    unsigned long lastCheck = 0;
    
    Serial.println("[Sensor Task] Started on core " + String(xPortGetCoreID()));
    
    while (true) {
        // Read sensors
        if (xSemaphoreTake(positionMutex, portMAX_DELAY)) {
            sensors.readSensors();

            // Check for sensor connect/disconnect every 2 seconds
            if (millis() - lastCheck > 2000) {
                lastCheck = millis();
                sensors.checkConnections();
            }

            xSemaphoreGive(positionMutex);
        }
        
        // Wait for next cycle
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

/**
 * Display Task - Core 1
 * Updates the OLED at 10Hz, checks for reconnection every 2s
 */
void displayTask(void* parameter) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(100); // 10Hz
    unsigned long lastCheck = 0;

    Serial.println("[Display Task] Started on core " + String(xPortGetCoreID()));

    while (true) {
        if (xSemaphoreTake(positionMutex, pdMS_TO_TICKS(5))) {
            // Check for OLED connect/disconnect every 2 seconds
            if (millis() - lastCheck > 2000) {
                lastCheck = millis();
                oled.checkConnection();
            }
            oled.update();
            xSemaphoreGive(positionMutex);
        }
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

/**
 * Network Task - Core 1
 * Handles WiFi, web server, and LX200 protocol
 * Updates at lower frequency (10Hz)
 */
void networkTask(void* parameter) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(POSITION_UPDATE_RATE_MS);
    
    Serial.println("[Network Task] Started on core " + String(xPortGetCoreID()));
    
    while (true) {
        // Handle captive portal
        portal.handle();
        
        // Handle LX200 clients
        lx200.handle();
        
        // Broadcast position data to WebSocket clients (if any)
        if (portal.hasWebSocketClients()) {
            portal.broadcastPosition();
        }
        
        // Blink LED if client connected
        static bool ledState = false;
        if (lx200.isClientConnected()) {
            ledState = !ledState;
            digitalWrite(LED_PIN, ledState);
        } else {
            digitalWrite(LED_PIN, HIGH); // On when no client
        }
        
        // Wait for next cycle
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void setup() {
    Serial.begin(115200);
    // Wait for USB CDC serial connection (up to 3 seconds)
    unsigned long serialWaitStart = millis();
    while (!Serial && millis() - serialWaitStart < 3000) {
        delay(10);
    }
    delay(200);
    
    Serial.println("\n\n========================================");
    Serial.println("   PushTo ESP32 - Telescope System");
    Serial.println("========================================\n");
    
    // Initialize LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
    
    // Initialize configuration
    Serial.println("[Setup] Initializing configuration...");
    config.begin();
    
    // Initialize sensors
    Serial.println("[Setup] Initializing sensors...");
    if (!sensors.begin(I2C_SDA, I2C_SCL, I2C_FREQ)) {
        Serial.println("[Setup] WARNING: Sensor initialization failed!");
        Serial.println("[Setup] Check I2C connections and addresses");
    } else {
        Serial.println("[Setup] Sensors initialized successfully");
    }
    
    // Print diagnostics
    Serial.println("\n" + sensors.getDiagnostics());
    
    // Initialize WiFi and web portal
    Serial.println("[Setup] Starting WiFi access point...");
    portal.begin();
    Serial.println("[Setup] Connect to WiFi: PushTo-Setup");
    Serial.println("[Setup] Portal IP: " + portal.getAPIP());
    
    // Initialize OLED display (optional hardware)
    Serial.println("[Setup] Checking for OLED display...");
    oled.begin();

    // Initialize LX200 server
    Serial.println("[Setup] Starting LX200 server...");
    lx200.begin();
    
    // Perform alignment if configured
    if (config.isAligned()) {
        Serial.println("[Setup] Performing alignment...");
        coordinates.performAlignment();
    }
    
    // Create mutex for position data
    positionMutex = xSemaphoreCreateMutex();
    
    // Create sensor task on core 0
    xTaskCreatePinnedToCore(
        sensorTask,           // Task function
        "SensorTask",         // Task name
        4096,                 // Stack size
        NULL,                 // Parameters
        2,                    // Priority (high)
        &sensorTaskHandle,    // Task handle
        0                     // Core 0
    );
    
    // Create network task on core 1
    xTaskCreatePinnedToCore(
        networkTask,          // Task function
        "NetworkTask",        // Task name
        8192,                 // Stack size
        NULL,                 // Parameters
        1,                    // Priority (normal)
        &networkTaskHandle,   // Task handle
        1                     // Core 1
    );

    // Create display task on core 1 (always runs, handles connect/disconnect)
    xTaskCreatePinnedToCore(
        displayTask,          // Task function
        "DisplayTask",        // Task name
        4096,                 // Stack size
        NULL,                 // Parameters
        1,                    // Priority (normal)
        &displayTaskHandle,   // Task handle
        1                     // Core 1
    );

    Serial.println("\n[Setup] System initialized!");
    Serial.println("[Setup] Sensor task running on core 0 @ 100Hz");
    Serial.println("[Setup] Network task running on core 1 @ 10Hz");
    Serial.println("[Setup] Display task running on core 1 @ 10Hz");
    Serial.println("[Setup] Hot-plug: sensors & display checked every 2s");
    Serial.println("========================================\n");
}

void loop() {
    // Main loop is not used - everything runs in tasks
    // Keep this alive for watchdog
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Optional: Print status every 10 seconds
    static unsigned long lastStatus = 0;
    static bool firstStatus = true;
    if (millis() - lastStatus > 10000) {
        lastStatus = millis();
        
        // On first status tick, reprint setup diagnostics (USB CDC misses early logs)
        if (firstStatus) {
            firstStatus = false;
            Serial.println("\n=== Startup Diagnostics (reprinted) ===");
            Serial.println(sensors.getDiagnostics());
            Serial.print(oled.getDiagnostics());
            Serial.println("========================================");
        }

        TelescopePosition pos = sensors.getPosition();
        EquatorialCoords eq = coordinates.getCurrentPosition();
        
        Serial.println("\n--- Status Update ---");
        Serial.printf("Position: Az=%.2f° Alt=%.2f°\n", pos.azimuth, pos.altitude);
        Serial.printf("RA/Dec: %.4fh %.4f°\n", eq.ra, eq.dec);
        Serial.printf("LX200 clients: %d\n", lx200.getClientCount());
        Serial.printf("AS5600: %s  MPU6050: %s  OLED: %s\n",
            sensors.isAS5600Connected() ? "OK" : "---",
            sensors.isMPU6050Connected() ? "OK" : "---",
            oled.isConnected() ? "OK" : "---");
        Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
        Serial.println("--------------------\n");
    }
}
