/**
 * Project Kalkan - Water Flow and Pressure Monitoring System
 * ESP32 dual-core implementation with FreeRTOS
 * 
 * Hardware:
 * - YF-DN50 Hall-effect flow sensor on GPIO25
 * - Gravity KIT0139 pressure sensor on GPIO36 (ADC1)
 * - 16x2 I2C LCD with custom Greek characters
 * - Analog joystick on GPIO32/33
 * - Two buttons on GPIO14/27
 * - SD card on VSPI (CS=5)
 */

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <WiFi.h>

#include "config.h"
#include "sensor_data.h"
#include "sensor_manager.h"
#include "lcd_manager.h"
#include "input_manager.h"
#include "sd_manager.h"
#include "time_manager.h"
#include "system_monitor.h"

// Global instances
SensorManager sensorManager;
LCDManager lcdManager;
InputManager inputManager;
SDManager sdManager;
TimeManager timeManager;  
SystemMonitor systemMonitor;

// Global state
UIScreen current_screen = SCREEN_BOOT;
bool system_initialized = false;
bool time_setting_complete = false;

// Task handles
TaskHandle_t sensorTaskHandle = NULL;
TaskHandle_t uiTaskHandle = NULL;

// Function prototypes
void initializeSystem();
void handleUINavigation();
void handleTimeSetScreen();
void handleDateSetScreen();
void handleMainScreen();
void handleCalibrationScreen();
bool setupTimeFromUser();

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000); // Wait up to 3s for serial
    
#ifdef DEBUG_MODE
    Serial.println(F("=== Project Kalkan Starting ==="));
    Serial.printf("ESP32 Chip: %s Rev %d\n", ESP.getChipModel(), ESP.getChipRevision());
    Serial.printf("CPU Freq: %d MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
#endif

    // Disable WiFi to save power and reduce interference
    WiFi.mode(WIFI_OFF);
    btStop();
    
    // Initialize all subsystems
    initializeSystem();
    
    // Create dual-core tasks
    xTaskCreatePinnedToCore(
        SensorManager::sensorTask,   // Task function
        "SensorTask",                // Name
        8192,                        // Stack size
        &sensorManager,              // Parameter
        3,                           // Priority (high)
        &sensorTaskHandle,           // Handle
        0                            // Core 0
    );
    
    xTaskCreatePinnedToCore(
        LCDManager::uiTask,          // Task function
        "UITask",                    // Name
        8192,                        // Stack size
        &lcdManager,                 // Parameter
        2,                           // Priority (medium)
        &uiTaskHandle,               // Handle
        1                            // Core 1
    );
    
#ifdef DEBUG_MODE
    Serial.println(F("Tasks created. System ready."));
#endif
}

void loop() {
    // Main loop handles high-level coordination
    static uint32_t last_status_print = 0;
    static uint32_t last_health_check = 0;
    
    // Handle user input and screen navigation
    handleUINavigation();
    
    // Periodic system health monitoring
    if (millis() - last_health_check > 10000) { // Every 10 seconds
        systemMonitor.updateHealthStatus();
        if (!systemMonitor.isSystemHealthy()) {
            DEBUG_PRINTLN(F("System health warning detected"));
            systemMonitor.performSystemRecovery();
        }
        last_health_check = millis();
    }
    
    // Print debug status every 30 seconds
#ifdef DEBUG_MODE
    if (millis() - last_status_print > 30000) {
        systemMonitor.printSystemStatus();
        if (millis() - last_status_print > 120000) { // Print memory every 2 minutes
            systemMonitor.printMemoryStatus();
            last_status_print = millis();
        }
        last_status_print = millis();
    }
#endif
    
    // Let other tasks run
    vTaskDelay(pdMS_TO_TICKS(50));
}

void initializeSystem() {
    DEBUG_PRINTLN(F("Initializing system components..."));
    
    // Initialize system monitor first
    systemMonitor.initialize();
    
    // Initialize time manager first
    timeManager.initialize();
    
    // Initialize LCD and show boot screen
    if (!lcdManager.initialize()) {
        Serial.println(F("ERROR: LCD initialization failed"));
    } else {
        lcdManager.showBootScreen();
    }
    
    // Initialize input manager
    if (!inputManager.initialize()) {
        Serial.println(F("ERROR: Input initialization failed"));
        lcdManager.showError("Input Error");
        systemMonitor.recordError("INPUT");
    }
    
    // Initialize sensor manager
    if (!sensorManager.initialize()) {
        Serial.println(F("ERROR: Sensor initialization failed"));
        lcdManager.showError("Sensor Error");
        systemMonitor.recordError("SENSOR");
    }
    
    // Initialize SD card
    if (!sdManager.initialize()) {
        Serial.println(F("WARNING: SD card initialization failed"));
        lcdManager.showMessage("SD Card Error", "Logging disabled", 3000);
        systemMonitor.recordError("SD");
    } else {
        // Start continuous logging
        sdManager.startLogging();
    }
    
    system_initialized = true;
    DEBUG_PRINTLN(F("System initialization complete"));
    
    // Print initial system status
    systemMonitor.printSystemStatus();
    systemMonitor.printMemoryStatus();
}

void handleUINavigation() {
    static uint32_t boot_start_time = millis();
    
    InputEvent event = inputManager.update();
    
    switch (current_screen) {
        case SCREEN_BOOT:
            if (millis() - boot_start_time > BOOT_DISPLAY_MS) {
                current_screen = SCREEN_TIME_SET;
                lcdManager.setScreen(SCREEN_TIME_SET);
            }
            break;
            
        case SCREEN_TIME_SET:
            handleTimeSetScreen();
            break;
            
        case SCREEN_DATE_SET:
            handleDateSetScreen();
            break;
            
        case SCREEN_MAIN:
        case SCREEN_FLOW_STATS:
        case SCREEN_PRESSURE_STATS:
            handleMainScreen();
            break;
            
        case SCREEN_CALIBRATION:
            handleCalibrationScreen();
            break;
    }
    
    // Handle global events
    if (event == EVENT_BOTH_BUTTONS_HOLD && 
        inputManager.getBothButtonsHoldTime() >= CALIBRATION_HOLD_MS) {
        if (current_screen != SCREEN_CALIBRATION) {
            current_screen = SCREEN_CALIBRATION;
            lcdManager.setScreen(SCREEN_CALIBRATION);
        }
    }
    
    if (event == EVENT_BUTTON1_PRESS && current_screen == SCREEN_MAIN) {
        // Trigger event logging
        sensorManager.markEvent();
        lcdManager.showMessage("Event Logged", nullptr, 1000);
    }
}

void handleTimeSetScreen() {
    static int hour = 12, minute = 0, cursor_pos = 0;
    static uint32_t last_update = 0;
    
    InputEvent event = inputManager.update();
    JoystickState joy = inputManager.getJoystickState();
    bool changed = false;
    
    // Handle cursor movement
    if (event == EVENT_JOYSTICK_LEFT && cursor_pos > 0) {
        cursor_pos--;
        changed = true;
    } else if (event == EVENT_JOYSTICK_RIGHT) {
        cursor_pos++;
        if (cursor_pos > 3) { // Past minute units
            // Set time and move to date screen
            timeManager.setTime(hour, minute);
            current_screen = SCREEN_DATE_SET;
            lcdManager.setScreen(SCREEN_DATE_SET);
            return;
        }
        changed = true;
    }
    
    // Handle value changes
    if (event == EVENT_JOYSTICK_UP || event == EVENT_JOYSTICK_DOWN) {
        int delta = (event == EVENT_JOYSTICK_UP) ? 1 : -1;
        if (joy.acceleration_active) {
            delta *= (int)JOYSTICK_ACCEL_FACTOR;
        }
        
        switch (cursor_pos) {
            case 0: // Hour tens
                hour += delta * 10;
                if (hour < 0) hour = 20;
                if (hour > 23) hour = hour % 10;
                break;
            case 1: // Hour units
                hour += delta;
                if (hour < 0) hour = 23;
                if (hour > 23) hour = 0;
                break;
            case 2: // Minute tens
                minute += delta * 10;
                if (minute < 0) minute = 50;
                if (minute > 59) minute = minute % 10;
                break;
            case 3: // Minute units
                minute += delta;
                if (minute < 0) minute = 59;
                if (minute > 59) minute = 0;
                break;
        }
        changed = true;
    }
    
    if (changed) {
        lcdManager.updateTimeSet(hour, minute, cursor_pos);
        last_update = millis();
    }
}

void handleDateSetScreen() {
    static int day = 1, month = 1, year = 2025, cursor_pos = 0;
    static uint32_t last_update = 0;
    
    InputEvent event = inputManager.update();
    JoystickState joy = inputManager.getJoystickState();
    bool changed = false;
    
    // Handle cursor movement
    if (event == EVENT_JOYSTICK_LEFT && cursor_pos > 0) {
        cursor_pos--;
        changed = true;
    } else if (event == EVENT_JOYSTICK_RIGHT) {
        cursor_pos++;
        if (cursor_pos > 2) { // Past year
            // Set date and move to main screen
            timeManager.setDate(year, month, day);
            time_setting_complete = true;
            current_screen = SCREEN_MAIN;
            lcdManager.setScreen(SCREEN_MAIN);
            return;
        }
        changed = true;
    }
    
    // Handle value changes
    if (event == EVENT_JOYSTICK_UP || event == EVENT_JOYSTICK_DOWN) {
        int delta = (event == EVENT_JOYSTICK_UP) ? 1 : -1;
        if (joy.acceleration_active) {
            delta *= (int)JOYSTICK_ACCEL_FACTOR;
        }
        
        switch (cursor_pos) {
            case 0: // Day
                day += delta;
                if (day < 1) day = 31;
                if (day > 31) day = 1;
                break;
            case 1: // Month
                month += delta;
                if (month < 1) month = 12;
                if (month > 12) month = 1;
                break;
            case 2: // Year
                year += delta;
                if (year < 2020) year = 2030;
                if (year > 2030) year = 2020;
                break;
        }
        changed = true;
    }
    
    if (changed) {
        lcdManager.updateDateSet(day, month, year, cursor_pos);
        last_update = millis();
    }
}

void handleMainScreen() {
    InputEvent event = inputManager.update();
    
    // Handle screen switching with joystick X-axis
    if (event == EVENT_SCREEN_CHANGE_LEFT) {
        switch (current_screen) {
            case SCREEN_MAIN:
                current_screen = SCREEN_PRESSURE_STATS;
                break;
            case SCREEN_FLOW_STATS:
                current_screen = SCREEN_MAIN;
                break;
            case SCREEN_PRESSURE_STATS:
                current_screen = SCREEN_FLOW_STATS;
                break;
            default:
                break;
        }
        lcdManager.setScreen(current_screen);
    } else if (event == EVENT_SCREEN_CHANGE_RIGHT) {
        switch (current_screen) {
            case SCREEN_MAIN:
                current_screen = SCREEN_FLOW_STATS;
                break;
            case SCREEN_FLOW_STATS:
                current_screen = SCREEN_PRESSURE_STATS;
                break;
            case SCREEN_PRESSURE_STATS:
                current_screen = SCREEN_MAIN;
                break;
            default:
                break;
        }
        lcdManager.setScreen(current_screen);
    }
}

void handleCalibrationScreen() {
    static float calibration_value = 0.0f;
    static int cursor_pos = 0;
    static uint32_t last_update = 0;
    
    InputEvent event = inputManager.update();
    
    // Exit calibration with both buttons
    if (event == EVENT_BOTH_BUTTONS_HOLD) {
        sensorManager.calibratePressureSensor(calibration_value);
        lcdManager.showMessage("Calibrated", nullptr, 2000);
        current_screen = SCREEN_MAIN;
        lcdManager.setScreen(SCREEN_MAIN);
        return;
    }
    
    // Handle calibration value editing (similar to time/date)
    // Implementation details would be similar to time setting
    // For now, just exit after a timeout
    static uint32_t calibration_start = millis();
    if (millis() - calibration_start > 30000) { // 30 second timeout
        current_screen = SCREEN_MAIN;
        lcdManager.setScreen(SCREEN_MAIN);
    }
}
