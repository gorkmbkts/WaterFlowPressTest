#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <Arduino.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>
#include "config.h"
#include "sensor_data.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

class SensorManager {
private:
    // Hardware calibration
    esp_adc_cal_characteristics_t adc_chars;
    
    // Flow sensor interrupt variables
    static volatile uint32_t pulse_count;
    static volatile uint32_t last_pulse_time;
    static volatile uint32_t pulse_intervals[10]; // For frequency calculation
    static volatile uint8_t interval_index;
    
    // Sensor buffers
    FlowBuffer flow_buffer;
    PressureBuffer pressure_buffer;
    EventBuffer event_buffer;
    
    // Analytics data
    FlowAnalytics flow_analytics;
    PressureAnalytics pressure_analytics;
    SystemConfig config;
    
    // Thread safety
    SemaphoreHandle_t data_mutex;
    QueueHandle_t sensor_queue;
    
    // Private methods
    void initializeADC();
    void initializeFlowSensor();
    float readPressureSensor();
    float calculateFlowRate();
    void updateFlowAnalytics();
    void updatePressureAnalytics();
    void computeRunningStats(const FlowBuffer& buffer, SensorStats& stats);
    void computeRunningStats(const PressureBuffer& buffer, SensorStats& stats);
    float calculateMedian(float* values, size_t count);
    float calculatePercentile(float* values, size_t count, float percentile);
    
    // Interrupt handler
    static void IRAM_ATTR flowPulseISR();
    
public:
    SensorManager();
    ~SensorManager();
    
    // Initialization
    bool initialize();
    
    // Task functions
    static void sensorTask(void* parameter);
    void runSensorTask();
    
    // Data access (thread-safe)
    bool getLatestReading(SensorReading& reading);
    FlowAnalytics getFlowAnalytics();
    PressureAnalytics getPressureAnalytics();
    
    // Configuration
    void updateConfig(const SystemConfig& new_config);
    SystemConfig getConfig();
    
    // Calibration
    void calibratePressureSensor(float actual_height_cm);
    void setDensityFactor(float density);
    
    // Event logging support
    bool getEventBuffer(SensorReading* buffer, size_t& count);
    void markEvent();
    
    // Statistics
    void resetStatistics();
    void updateBaselines();
    
    // Debug
    void printDebugInfo();
};

// Global instance declaration
extern SensorManager sensorManager;

#endif // SENSOR_MANAGER_H