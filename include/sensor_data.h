#ifndef SENSOR_DATA_H
#define SENSOR_DATA_H

#include <Arduino.h>

// Structure to hold a single sensor reading
struct SensorReading {
    uint32_t timestamp;           // Unix timestamp
    uint32_t timestamp_us;        // Microsecond timestamp for precision
    float flow_rate;              // Instantaneous flow rate (L/s)
    float flow_frequency;         // Flow sensor frequency (Hz)
    uint32_t pulse_count;         // Raw pulse count
    float pressure_voltage;       // Raw pressure sensor voltage
    float water_height;           // Calculated water height (cm)
    uint16_t adc_raw;            // Raw ADC reading
};

// Structure for sensor statistics
struct SensorStats {
    float mean;
    float median;
    float min;
    float max;
    float std_dev;
    float percentile_10;
    float percentile_90;
    uint32_t sample_count;
    uint32_t last_update;
};

// Structure for flow analytics
struct FlowAnalytics {
    float instantaneous;          // Current flow rate (A)
    float healthy_baseline;       // Healthy pump flow (B) - 90th percentile
    float difference_percent;     // Percentage difference (C)
    float minimum_healthy;        // Minimum healthy flow (D) - 10th percentile
    float mean;                   // Average flow (E) - μ
    float median;                 // Median flow (F) - η
    SensorStats stats;
    bool pump_detected;           // Is pump currently running
};

// Structure for pressure/height analytics
struct PressureAnalytics {
    float instantaneous;          // Current height (G)
    float empty_baseline;         // Empty tank height (H) - θ
    float full_height;            // Full tank height (I) - Σ
    float difference_percent;     // Percentage difference (J)
    float signal_quality;         // Signal noise metric (K)
    SensorStats stats;
    float calibration_offset;     // Calibration adjustment
    float density_factor;         // Liquid density adjustment
};

// Structure for system configuration
struct SystemConfig {
    float pressure_v_min;         // Calibration: voltage at empty
    float pressure_v_max;         // Calibration: voltage at full
    float pressure_height_max;    // Maximum height range
    float density_factor;         // Liquid density correction
    uint16_t log_interval_ms;     // Logging interval
    uint8_t sensor_sample_rate;   // Sensor sampling rate
    bool auto_calibration;        // Enable auto-calibration
};

// Circular buffer for sensor readings
template<typename T, size_t N>
class CircularBuffer {
private:
    T buffer[N];
    size_t head;
    size_t tail;
    size_t count;
    bool full;

public:
    CircularBuffer() : head(0), tail(0), count(0), full(false) {}
    
    void push(const T& item) {
        buffer[head] = item;
        if (full) {
            tail = (tail + 1) % N;
        }
        head = (head + 1) % N;
        full = (head == tail);
        if (!full) {
            count++;
        }
    }
    
    bool pop(T& item) {
        if (empty()) {
            return false;
        }
        item = buffer[tail];
        full = false;
        tail = (tail + 1) % N;
        count--;
        return true;
    }
    
    const T& operator[](size_t index) const {
        return buffer[(tail + index) % N];
    }
    
    size_t size() const { return count; }
    bool empty() const { return (!full && (head == tail)); }
    bool is_full() const { return full; }
    void clear() { head = tail = count = 0; full = false; }
    
    // Iterator support for statistics calculation
    const T* begin() const { return &buffer[tail]; }
    const T* end() const { return &buffer[(tail + count) % N]; }
};

// Type definitions for our circular buffers
using FlowBuffer = CircularBuffer<float, FLOW_WINDOW_SIZE>;
using PressureBuffer = CircularBuffer<float, PRESSURE_WINDOW_SIZE>;
using EventBuffer = CircularBuffer<SensorReading, EVENT_BUFFER_SIZE>;

#endif // SENSOR_DATA_H