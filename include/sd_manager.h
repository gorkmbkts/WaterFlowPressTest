#ifndef SD_MANAGER_H
#define SD_MANAGER_H

#include <Arduino.h>
#include <SdFat.h>
#include <time.h>
#include "config.h"
#include "sensor_data.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// SD card status
enum SDStatus {
    SD_OK,
    SD_NOT_FOUND,
    SD_MOUNT_FAILED,
    SD_LOW_SPACE,
    SD_WRITE_ERROR,
    SD_READ_ERROR
};

// Log file types
enum LogFileType {
    LOG_CONTINUOUS,
    LOG_EVENT
};

class SDManager {
private:
    SdFat sd;
    SdFile current_log_file;
    SdFile current_event_file;
    
    // Status
    SDStatus status;
    uint64_t total_space;
    uint64_t free_space;
    bool sd_initialized;
    bool logging_active;
    bool event_logging_active;
    
    // File management
    String current_log_filename;
    String current_event_filename;
    uint32_t last_space_check;
    uint32_t last_log_write;
    
    // Event logging state
    uint32_t event_start_time;
    uint32_t event_duration_ms;
    bool event_snapshot_written;
    
    // Thread safety
    SemaphoreHandle_t sd_mutex;
    
    // Buffering for performance
    char log_buffer[512];
    size_t buffer_pos;
    uint32_t last_flush;
    
    // Private methods
    bool initializeSD();
    bool createDirectories();
    bool openLogFile(const char* filename, LogFileType type);
    void closeLogFile(LogFileType type);
    bool writeLogHeader(SdFile& file);
    bool writeLogEntry(SdFile& file, const SensorReading& reading, bool include_analytics = true);
    void flushBuffer(LogFileType type);
    
    // File management
    String generateLogFilename(bool is_event = false);
    String generateEventFilename();
    bool cleanupOldFiles();
    bool deleteOldestLogFile();
    uint64_t getDirectorySize(const char* path);
    void updateSpaceInfo();
    
    // CSV formatting
    void formatLogEntry(char* buffer, const SensorReading& reading, const FlowAnalytics& flow, const PressureAnalytics& pressure);
    void formatTimestamp(char* buffer, uint32_t timestamp);
    void formatISO8601(char* buffer, uint32_t timestamp);
    
public:
    SDManager();
    ~SDManager();
    
    // Initialization
    bool initialize();
    bool reinitialize(); // For error recovery
    
    // Status
    SDStatus getStatus() { return status; }
    bool isAvailable() { return sd_initialized && (status == SD_OK || status == SD_LOW_SPACE); }
    uint64_t getFreeSpace() { return free_space; }
    uint64_t getTotalSpace() { return total_space; }
    float getFreeSpaceGB() { return (float)free_space / (1024.0f * 1024.0f * 1024.0f); }
    
    // Logging operations
    bool startLogging();
    bool stopLogging();
    bool logReading(const SensorReading& reading, const FlowAnalytics& flow, const PressureAnalytics& pressure);
    
    // Event logging
    bool startEventLogging(const SensorReading* buffer, size_t buffer_count);
    bool continueEventLogging(const SensorReading& reading, const FlowAnalytics& flow, const PressureAnalytics& pressure);
    bool stopEventLogging();
    bool isEventLogging() { return event_logging_active; }
    
    // File operations
    bool listLogFiles(String* filenames, size_t max_count, size_t& count);
    bool readLogFile(const char* filename, char* buffer, size_t buffer_size, size_t& bytes_read);
    bool deleteLogFile(const char* filename);
    
    // Maintenance
    bool performMaintenance(); // Cleanup, space check, etc.
    void forceFlush();
    
    // Configuration
    void setLogInterval(uint16_t interval_ms);
    void setEventDuration(uint32_t duration_ms) { event_duration_ms = duration_ms; }
    
    // Error handling
    const char* getStatusString();
    void handleError(SDStatus error);
    
    // Debug
    void printDebugInfo();
    void listDirectory(const char* path, int levels = 1);
};

// Global instance declaration
extern SDManager sdManager;

#endif // SD_MANAGER_H