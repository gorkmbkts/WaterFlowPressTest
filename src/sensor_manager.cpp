#include "sensor_manager.h"
#include "time_manager.h"
#include <algorithm>
#include <cmath>

// Static member definitions
volatile uint32_t SensorManager::pulse_count = 0;
volatile uint32_t SensorManager::last_pulse_time = 0;
volatile uint32_t SensorManager::pulse_intervals[10] = {0};
volatile uint8_t SensorManager::interval_index = 0;

SensorManager::SensorManager() {
    data_mutex = xSemaphoreCreateMutex();
    sensor_queue = xQueueCreate(10, sizeof(SensorReading));
    
    // Initialize configuration with defaults
    config.pressure_v_min = PRESSURE_V_MIN;
    config.pressure_v_max = PRESSURE_V_MAX;
    config.pressure_height_max = PRESSURE_HEIGHT_MAX;
    config.density_factor = 1.0f;
    config.log_interval_ms = LOG_INTERVAL_MS;
    config.sensor_sample_rate = SENSOR_TASK_FREQ;
    config.auto_calibration = true;
    
    // Initialize analytics
    memset(&flow_analytics, 0, sizeof(flow_analytics));
    memset(&pressure_analytics, 0, sizeof(pressure_analytics));
    
    pressure_analytics.density_factor = 1.0f;
    pressure_analytics.calibration_offset = 0.0f;
}

SensorManager::~SensorManager() {
    if (data_mutex) vSemaphoreDelete(data_mutex);
    if (sensor_queue) vQueueDelete(sensor_queue);
}

bool SensorManager::initialize() {
    DEBUG_PRINTLN(F("Initializing sensor manager..."));
    
    initializeADC();
    initializeFlowSensor();
    
    // Test ADC reading
    float test_voltage = readPressureSensor();
    if (test_voltage < 0) {
        DEBUG_PRINTLN(F("ERROR: ADC test failed"));
        return false;
    }
    
    DEBUG_PRINTLN(F("Sensor manager initialized"));
    return true;
}

void SensorManager::initializeADC() {
    // Configure ADC1 for pressure sensor
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11); // GPIO36, 0-3.3V range
    
    // Characterize ADC
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);
    
    DEBUG_PRINTLN(F("ADC initialized"));
}

void SensorManager::initializeFlowSensor() {
    pinMode(FLOW_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(FLOW_PIN), flowPulseISR, RISING);
    
    DEBUG_PRINTLN(F("Flow sensor initialized"));
}

void IRAM_ATTR SensorManager::flowPulseISR() {
    uint32_t current_time = micros();
    
    if (last_pulse_time > 0) {
        uint32_t interval = current_time - last_pulse_time;
        pulse_intervals[interval_index] = interval;
        interval_index = (interval_index + 1) % 10;
    }
    
    last_pulse_time = current_time;
    pulse_count++;
}

float SensorManager::readPressureSensor() {
    uint32_t total = 0;
    int valid_readings = 0;
    
    // Take multiple samples for noise reduction
    for (int i = 0; i < ADC_SAMPLES; i++) {
        uint32_t raw = adc1_get_raw(ADC1_CHANNEL_0);
        if (raw > 0 && raw < 4095) {
            total += raw;
            valid_readings++;
        }
        delayMicroseconds(100);
    }
    
    if (valid_readings == 0) {
        return -1.0f; // Error
    }
    
    uint32_t average_raw = total / valid_readings;
    uint32_t voltage_mv = esp_adc_cal_raw_to_voltage(average_raw, &adc_chars);
    
    return voltage_mv / 1000.0f; // Convert to volts
}

float SensorManager::calculateFlowRate() {
    // Calculate frequency from recent pulse intervals
    if (interval_index == 0 && pulse_intervals[0] == 0) {
        return 0.0f; // No pulses recorded
    }
    
    // Find median interval for noise resistance
    uint32_t intervals[10];
    int count = 0;
    
    for (int i = 0; i < 10; i++) {
        if (pulse_intervals[i] > 0) {
            intervals[count++] = pulse_intervals[i];
        }
    }
    
    if (count == 0) return 0.0f;
    
    // Sort for median calculation
    std::sort(intervals, intervals + count);
    
    uint32_t median_interval = intervals[count / 2];
    if (median_interval == 0) return 0.0f;
    
    float frequency = 1000000.0f / median_interval; // Hz
    return frequency * FLOW_CONVERSION; // Convert to L/s
}

void SensorManager::sensorTask(void* parameter) {
    SensorManager* manager = (SensorManager*)parameter;
    manager->runSensorTask();
}

void SensorManager::runSensorTask() {
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t task_period = pdMS_TO_TICKS(1000 / SENSOR_TASK_FREQ);
    
    DEBUG_PRINTLN(F("Sensor task started on core 0"));
    
    while (true) {
        SensorReading reading;
        
        // Get timestamps
        reading.timestamp = timeManager.getUnixTime();
        reading.timestamp_us = micros();
        
        // Read sensors
        reading.pressure_voltage = readPressureSensor();
        reading.flow_rate = calculateFlowRate();
        reading.flow_frequency = reading.flow_rate / FLOW_CONVERSION;
        reading.pulse_count = pulse_count;
        
        // Convert pressure to height
        if (reading.pressure_voltage >= 0) {
            float normalized = (reading.pressure_voltage - config.pressure_v_min) / 
                              (config.pressure_v_max - config.pressure_v_min);
            normalized = constrain(normalized, 0.0f, 1.0f);
            reading.water_height = normalized * config.pressure_height_max * 
                                  pressure_analytics.density_factor + 
                                  pressure_analytics.calibration_offset;
        } else {
            reading.water_height = -1.0f; // Error indicator
        }
        
        // Update buffers and analytics
        if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            flow_buffer.push(reading.flow_rate);
            pressure_buffer.push(reading.water_height);
            event_buffer.push(reading);
            
            updateFlowAnalytics();
            updatePressureAnalytics();
            
            xSemaphoreGive(data_mutex);
        }
        
        // Send to queue for other tasks
        xQueueSend(sensor_queue, &reading, 0);
        
        vTaskDelayUntil(&last_wake_time, task_period);
    }
}

void SensorManager::updateFlowAnalytics() {
    if (flow_buffer.size() < 5) return; // Need minimum samples
    
    // Copy data for statistics calculation
    float values[FLOW_WINDOW_SIZE];
    size_t count = 0;
    
    for (size_t i = 0; i < flow_buffer.size(); i++) {
        values[count++] = flow_buffer[i];
    }
    
    if (count == 0) return;
    
    // Sort for percentile calculations
    std::sort(values, values + count);
    
    // Update basic statistics
    flow_analytics.instantaneous = values[count - 1]; // Latest reading
    flow_analytics.mean = 0;
    for (size_t i = 0; i < count; i++) {
        flow_analytics.mean += values[i];
    }
    flow_analytics.mean /= count;
    
    flow_analytics.median = calculateMedian(values, count);
    flow_analytics.minimum_healthy = calculatePercentile(values, count, 0.1f);
    flow_analytics.healthy_baseline = calculatePercentile(values, count, 0.9f);
    
    // Calculate percentage difference
    if (flow_analytics.healthy_baseline > 0) {
        flow_analytics.difference_percent = 
            ((flow_analytics.instantaneous - flow_analytics.healthy_baseline) / 
             flow_analytics.healthy_baseline) * 100.0f;
    }
    
    // Update detailed statistics
    computeRunningStats(flow_buffer, flow_analytics.stats);
    
    // Pump detection (simple threshold-based)
    flow_analytics.pump_detected = flow_analytics.instantaneous > (flow_analytics.minimum_healthy * 1.5f);
}

void SensorManager::updatePressureAnalytics() {
    if (pressure_buffer.size() < 5) return;
    
    float values[PRESSURE_WINDOW_SIZE];
    size_t count = 0;
    
    for (size_t i = 0; i < pressure_buffer.size(); i++) {
        if (pressure_buffer[i] >= 0) { // Valid readings only
            values[count++] = pressure_buffer[i];
        }
    }
    
    if (count == 0) return;
    
    std::sort(values, values + count);
    
    pressure_analytics.instantaneous = values[count - 1];
    
    // Calculate noise metric (coefficient of variation)
    float mean = 0, variance = 0;
    for (size_t i = 0; i < count; i++) {
        mean += values[i];
    }
    mean /= count;
    
    for (size_t i = 0; i < count; i++) {
        float diff = values[i] - mean;
        variance += diff * diff;
    }
    variance /= count;
    
    if (mean > 0) {
        pressure_analytics.signal_quality = (sqrtf(variance) / mean) * 100.0f;
    }
    
    // Update baselines (simplified - would need more sophisticated logic in production)
    static float min_stable = 1000.0f;
    static float max_stable = 0.0f;
    
    if (pressure_analytics.signal_quality < 5.0f) { // Low noise
        min_stable = std::min(min_stable, mean);
        max_stable = std::max(max_stable, mean);
        
        pressure_analytics.empty_baseline = min_stable;
        pressure_analytics.full_height = max_stable;
    }
    
    // Calculate percentage difference from full height
    if (pressure_analytics.full_height > 0) {
        pressure_analytics.difference_percent = 
            ((pressure_analytics.instantaneous - pressure_analytics.full_height) / 
             pressure_analytics.full_height) * 100.0f;
    }
    
    computeRunningStats(pressure_buffer, pressure_analytics.stats);
}

void SensorManager::computeRunningStats(const FlowBuffer& buffer, SensorStats& stats) {
    if (buffer.size() == 0) return;
    
    float values[FLOW_WINDOW_SIZE];
    size_t count = 0;
    
    for (size_t i = 0; i < buffer.size(); i++) {
        values[count++] = buffer[i];
    }
    
    // Calculate mean
    stats.mean = 0;
    for (size_t i = 0; i < count; i++) {
        stats.mean += values[i];
    }
    stats.mean /= count;
    
    // Calculate variance and standard deviation
    float variance = 0;
    for (size_t i = 0; i < count; i++) {
        float diff = values[i] - stats.mean;
        variance += diff * diff;
    }
    variance /= count;
    stats.std_dev = sqrtf(variance);
    
    // Sort for min, max, median, percentiles
    std::sort(values, values + count);
    stats.min = values[0];
    stats.max = values[count - 1];
    stats.median = calculateMedian(values, count);
    stats.percentile_10 = calculatePercentile(values, count, 0.1f);
    stats.percentile_90 = calculatePercentile(values, count, 0.9f);
    stats.sample_count = count;
    stats.last_update = millis();
}

void SensorManager::computeRunningStats(const PressureBuffer& buffer, SensorStats& stats) {
    if (buffer.size() == 0) return;
    
    float values[PRESSURE_WINDOW_SIZE];
    size_t count = 0;
    
    for (size_t i = 0; i < buffer.size(); i++) {
        if (buffer[i] >= 0) { // Valid readings only
            values[count++] = buffer[i];
        }
    }
    
    if (count == 0) return;
    
    // Same calculation as flow buffer
    stats.mean = 0;
    for (size_t i = 0; i < count; i++) {
        stats.mean += values[i];
    }
    stats.mean /= count;
    
    float variance = 0;
    for (size_t i = 0; i < count; i++) {
        float diff = values[i] - stats.mean;
        variance += diff * diff;
    }
    variance /= count;
    stats.std_dev = sqrtf(variance);
    
    std::sort(values, values + count);
    stats.min = values[0];
    stats.max = values[count - 1];
    stats.median = calculateMedian(values, count);
    stats.percentile_10 = calculatePercentile(values, count, 0.1f);
    stats.percentile_90 = calculatePercentile(values, count, 0.9f);
    stats.sample_count = count;
    stats.last_update = millis();
}

float SensorManager::calculateMedian(float* values, size_t count) {
    if (count == 0) return 0.0f;
    if (count % 2 == 1) {
        return values[count / 2];
    } else {
        return (values[count / 2 - 1] + values[count / 2]) / 2.0f;
    }
}

float SensorManager::calculatePercentile(float* values, size_t count, float percentile) {
    if (count == 0) return 0.0f;
    float index = percentile * (count - 1);
    size_t lower = (size_t)index;
    size_t upper = lower + 1;
    
    if (upper >= count) return values[count - 1];
    
    float weight = index - lower;
    return values[lower] * (1.0f - weight) + values[upper] * weight;
}

bool SensorManager::getLatestReading(SensorReading& reading) {
    return xQueueReceive(sensor_queue, &reading, 0) == pdTRUE;
}

FlowAnalytics SensorManager::getFlowAnalytics() {
    FlowAnalytics result;
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        result = flow_analytics;
        xSemaphoreGive(data_mutex);
    }
    return result;
}

PressureAnalytics SensorManager::getPressureAnalytics() {
    PressureAnalytics result;
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        result = pressure_analytics;
        xSemaphoreGive(data_mutex);
    }
    return result;
}

void SensorManager::calibratePressureSensor(float actual_height_cm) {
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        float current_voltage = readPressureSensor();
        if (current_voltage >= 0) {
            // Calculate new calibration offset
            float calculated_height = ((current_voltage - config.pressure_v_min) / 
                                      (config.pressure_v_max - config.pressure_v_min)) * 
                                      config.pressure_height_max;
            pressure_analytics.calibration_offset = actual_height_cm - calculated_height;
            
            DEBUG_PRINT(F("Calibrated: offset = "));
            DEBUG_PRINTLN(pressure_analytics.calibration_offset);
        }
        xSemaphoreGive(data_mutex);
    }
}

void SensorManager::markEvent() {
    // Event marking is handled by SD manager when it requests the event buffer
    DEBUG_PRINTLN(F("Event marked"));
}

void SensorManager::printDebugInfo() {
#ifdef DEBUG_MODE
    FlowAnalytics flow = getFlowAnalytics();
    PressureAnalytics pressure = getPressureAnalytics();
    
    Serial.printf("Flow: %.3f L/s, Freq: %.2f Hz, Pump: %s\n", 
        flow.instantaneous, flow.instantaneous / FLOW_CONVERSION, 
        flow.pump_detected ? "ON" : "OFF");
    Serial.printf("Pressure: %.1f cm, Voltage: %.3f V, Quality: %.1f%%\n", 
        pressure.instantaneous, readPressureSensor(), pressure.signal_quality);
#endif
}