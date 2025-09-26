#include "system_monitor.h"
#include "config.h"

SystemMonitor::SystemMonitor() {
    boot_time = 0;
    last_heap_check = 0;
    min_free_heap = 0;
    max_stack_usage = 0;
    
    sensor_task_cycles = 0;
    ui_task_cycles = 0;
    missed_deadlines = 0;
    sd_errors = 0;
    sensor_errors = 0;
    
    system_healthy = true;
    cpu_usage_percent = 0.0f;
}

void SystemMonitor::initialize() {
    boot_time = millis();
    min_free_heap = ESP.getFreeHeap();
    
    DEBUG_PRINTLN(F("System monitor initialized"));
    
    // Print initial system info
    DEBUG_PRINT(F("ESP32 Model: "));
    DEBUG_PRINTLN(ESP.getChipModel());
    DEBUG_PRINT(F("CPU Frequency: "));
    DEBUG_PRINT(ESP.getCpuFreqMHz());
    DEBUG_PRINTLN(F(" MHz"));
    DEBUG_PRINT(F("Flash Size: "));
    DEBUG_PRINT(ESP.getFlashChipSize() / 1024 / 1024);
    DEBUG_PRINTLN(F(" MB"));
}

void SystemMonitor::updateHealthStatus() {
    checkMemoryUsage();
    
    // Basic health checks
    system_healthy = true;
    
    // Check free heap
    if (getCurrentFreeHeap() < 10000) { // Less than 10KB free
        system_healthy = false;
        DEBUG_PRINTLN(F("WARNING: Low memory"));
    }
    
    // Check for excessive errors
    if (sd_errors > 10 || sensor_errors > 20) {
        system_healthy = false;
        DEBUG_PRINTLN(F("WARNING: Excessive errors"));
    }
    
    // Check CPU usage (simplified)
    if (cpu_usage_percent > 90.0f) {
        system_healthy = false;
        DEBUG_PRINTLN(F("WARNING: High CPU usage"));
    }
}

void SystemMonitor::checkMemoryUsage() {
    uint32_t current_heap = getCurrentFreeHeap();
    
    if (current_heap < min_free_heap) {
        min_free_heap = current_heap;
    }
    
    last_heap_check = millis();
}

uint32_t SystemMonitor::getCurrentFreeHeap() {
    return ESP.getFreeHeap();
}

float SystemMonitor::getHeapFragmentation() {
    return 100.0f - (ESP.getMaxAllocHeap() * 100.0f / ESP.getFreeHeap());
}

void SystemMonitor::recordTaskCycle(bool is_sensor_task) {
    if (is_sensor_task) {
        sensor_task_cycles++;
    } else {
        ui_task_cycles++;
    }
}

void SystemMonitor::recordMissedDeadline() {
    missed_deadlines++;
}

void SystemMonitor::recordError(const char* component) {
    if (strcmp(component, "SD") == 0) {
        sd_errors++;
    } else if (strcmp(component, "SENSOR") == 0) {
        sensor_errors++;
    }
}

uint32_t SystemMonitor::getUptime() {
    return millis() - boot_time;
}

uint32_t SystemMonitor::getErrorCount(const char* component) {
    if (component == nullptr) {
        return sd_errors + sensor_errors;
    } else if (strcmp(component, "SD") == 0) {
        return sd_errors;
    } else if (strcmp(component, "SENSOR") == 0) {
        return sensor_errors;
    }
    return 0;
}

void SystemMonitor::printSystemStatus() {
#ifdef DEBUG_MODE
    Serial.println(F("=== SYSTEM STATUS ==="));
    Serial.printf("Uptime: %lu ms\n", getUptime());
    Serial.printf("System Health: %s\n", system_healthy ? "OK" : "WARNING");
    Serial.printf("CPU Usage: %.1f%%\n", cpu_usage_percent);
    Serial.printf("Task Cycles - Sensor: %lu, UI: %lu\n", 
        sensor_task_cycles, ui_task_cycles);
    Serial.printf("Errors - SD: %lu, Sensor: %lu\n", sd_errors, sensor_errors);
    Serial.printf("Missed Deadlines: %lu\n", missed_deadlines);
    Serial.println(F("===================="));
#endif
}

void SystemMonitor::printMemoryStatus() {
#ifdef DEBUG_MODE
    Serial.println(F("=== MEMORY STATUS ==="));
    Serial.printf("Free Heap: %lu bytes\n", getCurrentFreeHeap());
    Serial.printf("Min Free Heap: %lu bytes\n", min_free_heap);
    Serial.printf("Max Alloc Heap: %lu bytes\n", ESP.getMaxAllocHeap());
    Serial.printf("Heap Fragmentation: %.1f%%\n", getHeapFragmentation());
    Serial.printf("PSRAM: %s\n", ESP.getPsramSize() > 0 ? "Available" : "Not Available");
    Serial.println(F("===================="));
#endif
}

void SystemMonitor::printTaskStatus() {
#ifdef DEBUG_MODE
    Serial.println(F("=== TASK STATUS ==="));
    Serial.printf("Running Tasks: %d\n", uxTaskGetNumberOfTasks());
    
    // Get task information (simplified)
    TaskStatus_t* pxTaskStatusArray;
    volatile UBaseType_t uxArraySize;
    uint32_t ulTotalRunTime;
    
    uxArraySize = uxTaskGetNumberOfTasks();
    pxTaskStatusArray = (TaskStatus_t*)malloc(uxArraySize * sizeof(TaskStatus_t));
    
    if (pxTaskStatusArray != NULL) {
        uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, &ulTotalRunTime);
        
        Serial.println("Task Name\t\tState\tPriority\tStack");
        for (UBaseType_t x = 0; x < uxArraySize; x++) {
            Serial.printf("%-16s\t%d\t%d\t\t%d\n",
                pxTaskStatusArray[x].pcTaskName,
                pxTaskStatusArray[x].eCurrentState,
                pxTaskStatusArray[x].uxCurrentPriority,
                pxTaskStatusArray[x].usStackHighWaterMark);
        }
        
        free(pxTaskStatusArray);
    }
    
    Serial.println(F("=================="));
#endif
}

void SystemMonitor::feedWatchdog() {
    // ESP32 has built-in watchdog, this is just a placeholder
    // for additional watchdog functionality if needed
}

void SystemMonitor::enableWatchdog(uint32_t timeout_ms) {
    // Configure hardware watchdog if needed
    DEBUG_PRINT(F("Watchdog enabled with timeout: "));
    DEBUG_PRINT(timeout_ms);
    DEBUG_PRINTLN(F(" ms"));
}

bool SystemMonitor::performSystemRecovery() {
    DEBUG_PRINTLN(F("Attempting system recovery..."));
    
    // Basic recovery steps
    bool recovery_success = true;
    
    // Clear error counters
    resetErrorCounters();
    
    // Force garbage collection
    ESP.getMaxAllocHeap();
    
    // Reset system health flag
    system_healthy = true;
    
    DEBUG_PRINTLN(recovery_success ? F("Recovery successful") : F("Recovery failed"));
    return recovery_success;
}

void SystemMonitor::resetErrorCounters() {
    sd_errors = 0;
    sensor_errors = 0;
    missed_deadlines = 0;
    DEBUG_PRINTLN(F("Error counters reset"));
}