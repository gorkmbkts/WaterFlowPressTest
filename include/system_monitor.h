#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H

#include <Arduino.h>

/**
 * System health monitoring and diagnostics
 */
class SystemMonitor {
private:
    uint32_t boot_time;
    uint32_t last_heap_check;
    uint32_t min_free_heap;
    uint32_t max_stack_usage;
    
    // Performance counters
    uint32_t sensor_task_cycles;
    uint32_t ui_task_cycles;
    uint32_t missed_deadlines;
    uint32_t sd_errors;
    uint32_t sensor_errors;
    
    // System state
    bool system_healthy;
    float cpu_usage_percent;
    
public:
    SystemMonitor();
    
    // Initialization
    void initialize();
    
    // Health monitoring
    void updateHealthStatus();
    bool isSystemHealthy() { return system_healthy; }
    
    // Memory monitoring
    void checkMemoryUsage();
    uint32_t getMinFreeHeap() { return min_free_heap; }
    uint32_t getCurrentFreeHeap();
    float getHeapFragmentation();
    
    // Performance monitoring
    void recordTaskCycle(bool is_sensor_task);
    void recordMissedDeadline();
    void recordError(const char* component);
    
    // Statistics
    uint32_t getUptime();
    float getCPUUsage() { return cpu_usage_percent; }
    uint32_t getErrorCount(const char* component = nullptr);
    
    // Diagnostics
    void printSystemStatus();
    void printMemoryStatus();
    void printTaskStatus();
    
    // Watchdog support
    void feedWatchdog();
    void enableWatchdog(uint32_t timeout_ms);
    
    // Recovery
    bool performSystemRecovery();
    void resetErrorCounters();
};

// Global instance
extern SystemMonitor systemMonitor;

#endif // SYSTEM_MONITOR_H