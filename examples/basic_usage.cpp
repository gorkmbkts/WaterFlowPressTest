/**
 * Basic usage example for Project Kalkan
 * 
 * This example shows how to initialize the system and access sensor data
 * without the full UI implementation.
 */

#include <Arduino.h>
#include "../include/config.h"
#include "../include/sensor_manager.h"
#include "../include/time_manager.h"

SensorManager sensorManager;
TimeManager timeManager;

void setup() {
    Serial.begin(115200);
    Serial.println("Project Kalkan - Basic Usage Example");
    
    // Initialize time manager
    timeManager.initialize();
    timeManager.setDateTime({2025, 1, 15, 14, 30, 0, 0});
    
    // Initialize sensor manager
    if (!sensorManager.initialize()) {
        Serial.println("ERROR: Sensor initialization failed");
        return;
    }
    
    Serial.println("System initialized successfully");
    Serial.println("Reading sensors every 5 seconds...");
}

void loop() {
    static uint32_t last_reading = 0;
    
    if (millis() - last_reading > 5000) {
        // Get sensor analytics
        FlowAnalytics flow = sensorManager.getFlowAnalytics();
        PressureAnalytics pressure = sensorManager.getPressureAnalytics();
        
        // Print flow data
        Serial.println("=== FLOW DATA ===");
        Serial.printf("Instantaneous: %.3f L/s\n", flow.instantaneous);
        Serial.printf("Mean: %.3f L/s\n", flow.mean);
        Serial.printf("Median: %.3f L/s\n", flow.median);
        Serial.printf("Baseline: %.3f L/s\n", flow.healthy_baseline);
        Serial.printf("Difference: %+.1f%%\n", flow.difference_percent);
        Serial.printf("Pump detected: %s\n", flow.pump_detected ? "YES" : "NO");
        
        // Print pressure data
        Serial.println("=== PRESSURE DATA ===");
        Serial.printf("Height: %.1f cm\n", pressure.instantaneous);
        Serial.printf("Empty baseline: %.1f cm\n", pressure.empty_baseline);
        Serial.printf("Full height: %.1f cm\n", pressure.full_height);
        Serial.printf("Signal quality: %.1f%%\n", pressure.signal_quality);
        
        // Print time
        char time_str[32];
        timeManager.formatDateTime(time_str, false);
        Serial.printf("Time: %s\n", time_str);
        
        Serial.println("========================");
        last_reading = millis();
    }
    
    delay(100);
}