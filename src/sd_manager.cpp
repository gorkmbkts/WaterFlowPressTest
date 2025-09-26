#include "sd_manager.h"
#include "time_manager.h"
#include "sensor_manager.h"
#include <SPI.h>

// External references
extern TimeManager timeManager;
extern SensorManager sensorManager;

SDManager::SDManager() {
    status = SD_NOT_FOUND;
    total_space = 0;
    free_space = 0;
    sd_initialized = false;
    logging_active = false;
    event_logging_active = false;
    
    last_space_check = 0;
    last_log_write = 0;
    last_flush = 0;
    
    event_start_time = 0;
    event_duration_ms = 60000; // Default 60 minutes
    event_snapshot_written = false;
    
    buffer_pos = 0;
    memset(log_buffer, 0, sizeof(log_buffer));
    
    sd_mutex = xSemaphoreCreateMutex();
}

SDManager::~SDManager() {
    if (sd_mutex) vSemaphoreDelete(sd_mutex);
}

bool SDManager::initialize() {
    DEBUG_PRINTLN(F("Initializing SD card..."));
    
    if (!initializeSD()) {
        DEBUG_PRINTLN(F("SD card initialization failed"));
        return false;
    }
    
    if (!createDirectories()) {
        DEBUG_PRINTLN(F("Failed to create directories"));
        return false;
    }
    
    updateSpaceInfo();
    
    DEBUG_PRINTLN(F("SD card initialized successfully"));
    return true;
}

bool SDManager::initializeSD() {
    // Configure SPI for SD card
    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    
    if (!sd.begin(SD_CS_PIN, SD_SCK_MHZ(25))) {
        status = SD_MOUNT_FAILED;
        return false;
    }
    
    sd_initialized = true;
    status = SD_OK;
    
    // Get card info
    cid_t cid;
    if (sd.card()->readCID(&cid)) {
        DEBUG_PRINT(F("SD Card: "));
        DEBUG_PRINT((char*)cid.pnm);
        DEBUG_PRINT(F(" "));
        DEBUG_PRINT(cid.psn());
        DEBUG_PRINTLN(F(""));
    }
    
    return true;
}

bool SDManager::createDirectories() {
    if (!sd.exists("logs") && !sd.mkdir("logs")) {
        DEBUG_PRINTLN(F("Failed to create logs directory"));
        return false;
    }
    
    if (!sd.exists("events") && !sd.mkdir("events")) {
        DEBUG_PRINTLN(F("Failed to create events directory"));
        return false;
    }
    
    return true;
}

bool SDManager::startLogging() {
    if (!isAvailable()) {
        return false;
    }
    
    if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return false;
    }
    
    current_log_filename = generateLogFilename();
    bool success = openLogFile(current_log_filename.c_str(), LOG_CONTINUOUS);
    
    if (success) {
        logging_active = true;
        DEBUG_PRINT(F("Started logging to: "));
        DEBUG_PRINTLN(current_log_filename);
    }
    
    xSemaphoreGive(sd_mutex);
    return success;
}

bool SDManager::stopLogging() {
    if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return false;
    }
    
    if (logging_active) {
        flushBuffer(LOG_CONTINUOUS);
        closeLogFile(LOG_CONTINUOUS);
        logging_active = false;
    }
    
    xSemaphoreGive(sd_mutex);
    return true;
}

bool SDManager::logReading(const SensorReading& reading, const FlowAnalytics& flow, const PressureAnalytics& pressure) {
    if (!isAvailable() || !logging_active) {
        return false;
    }
    
    if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    
    bool success = writeLogEntry(current_log_file, reading, true);
    
    // Also write to event file if event logging is active
    if (event_logging_active && success) {
        writeLogEntry(current_event_file, reading, true);
        
        // Check if event duration has expired
        if (millis() - event_start_time > event_duration_ms) {
            stopEventLogging();
        }
    }
    
    // Periodic maintenance
    if (millis() - last_space_check > 60000) { // Check every minute
        updateSpaceInfo();
        if (free_space < MIN_FREE_SPACE_BYTES) {
            status = SD_LOW_SPACE;
            cleanupOldFiles();
        }
        last_space_check = millis();
    }
    
    xSemaphoreGive(sd_mutex);
    return success;
}

bool SDManager::openLogFile(const char* filename, LogFileType type) {
    String full_path;
    
    if (type == LOG_CONTINUOUS) {
        full_path = "logs/" + String(filename);
    } else {
        full_path = "events/" + String(filename);
    }
    
    SdFile* file = (type == LOG_CONTINUOUS) ? &current_log_file : &current_event_file;
    
    if (!file->open(full_path.c_str(), O_WRITE | O_CREAT | O_APPEND)) {
        DEBUG_PRINT(F("Failed to open: "));
        DEBUG_PRINTLN(full_path);
        return false;
    }
    
    // Write header if file is empty
    if (file->fileSize() == 0) {
        writeLogHeader(*file);
    }
    
    return true;
}

void SDManager::closeLogFile(LogFileType type) {
    if (type == LOG_CONTINUOUS && current_log_file.isOpen()) {
        current_log_file.close();
    } else if (type == LOG_EVENT && current_event_file.isOpen()) {
        current_event_file.close();
    }
}

bool SDManager::writeLogHeader(SdFile& file) {
    const char* header = "Timestamp,DateTime,PulseCount,FlowFreq,FlowRate,PressureV,WaterHeight,"
                        "FlowMean,FlowMedian,FlowMin,FlowMax,FlowBaseline,"
                        "PressureMean,PressureMedian,PressureMin,PressureMax,SignalQuality\n";
    
    return file.write(header, strlen(header)) == strlen(header);
}

bool SDManager::writeLogEntry(SdFile& file, const SensorReading& reading, bool include_analytics) {
    char line[256];
    char timestamp_str[32];
    
    timeManager.formatISO8601(timestamp_str);
    
    if (include_analytics) {
        FlowAnalytics flow = sensorManager.getFlowAnalytics();
        PressureAnalytics pressure = sensorManager.getPressureAnalytics();
        
        snprintf(line, sizeof(line),
            "%u,%s,%u,%.3f,%.4f,%.3f,%.2f,%.4f,%.4f,%.4f,%.4f,%.4f,%.2f,%.2f,%.2f,%.2f,%.1f\n",
            reading.timestamp, timestamp_str, reading.pulse_count,
            reading.flow_frequency, reading.flow_rate, reading.pressure_voltage, reading.water_height,
            flow.mean, flow.median, flow.stats.min, flow.stats.max, flow.healthy_baseline,
            pressure.stats.mean, pressure.stats.median, pressure.stats.min, pressure.stats.max,
            pressure.signal_quality);
    } else {
        snprintf(line, sizeof(line),
            "%u,%s,%u,%.3f,%.4f,%.3f,%.2f\n",
            reading.timestamp, timestamp_str, reading.pulse_count,
            reading.flow_frequency, reading.flow_rate, reading.pressure_voltage, reading.water_height);
    }
    
    size_t len = strlen(line);
    bool success = (file.write(line, len) == len);
    
    // Flush periodically
    if (millis() - last_flush > 5000) { // Flush every 5 seconds
        file.flush();
        last_flush = millis();
    }
    
    return success;
}

String SDManager::generateLogFilename(bool is_event) {
    char filename[32];
    
    if (is_event) {
        timeManager.formatEventFilename(filename);
        strcat(filename, ".csv");
    } else {
        timeManager.formatFilename(filename);
        strcat(filename, ".csv");
    }
    
    return String(filename);
}

String SDManager::generateEventFilename() {
    return generateLogFilename(true);
}

bool SDManager::startEventLogging(const SensorReading* buffer, size_t buffer_count) {
    if (!isAvailable()) {
        return false;
    }
    
    if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return false;
    }
    
    current_event_filename = generateEventFilename();
    bool success = openLogFile(current_event_filename.c_str(), LOG_EVENT);
    
    if (success) {
        // Write buffered data first
        for (size_t i = 0; i < buffer_count; i++) {
            writeLogEntry(current_event_file, buffer[i], false);
        }
        
        event_logging_active = true;
        event_start_time = millis();
        event_snapshot_written = true;
        
        DEBUG_PRINT(F("Started event logging to: "));
        DEBUG_PRINTLN(current_event_filename);
    }
    
    xSemaphoreGive(sd_mutex);
    return success;
}

bool SDManager::continueEventLogging(const SensorReading& reading, const FlowAnalytics& flow, const PressureAnalytics& pressure) {
    // This is handled in logReading() method
    return event_logging_active;
}

bool SDManager::stopEventLogging() {
    if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return false;
    }
    
    if (event_logging_active) {
        flushBuffer(LOG_EVENT);
        closeLogFile(LOG_EVENT);
        event_logging_active = false;
        event_snapshot_written = false;
        
        DEBUG_PRINT(F("Stopped event logging: "));
        DEBUG_PRINTLN(current_event_filename);
    }
    
    xSemaphoreGive(sd_mutex);
    return true;
}

void SDManager::updateSpaceInfo() {
    if (!sd_initialized) return;
    
    total_space = sd.vol()->clusterCount() * sd.vol()->blocksPerCluster() * 512ULL;
    free_space = sd.vol()->freeClusterCount() * sd.vol()->blocksPerCluster() * 512ULL;
}

bool SDManager::cleanupOldFiles() {
    DEBUG_PRINTLN(F("Cleaning up old log files..."));
    
    // Simple cleanup: delete oldest files in logs directory
    SdFile logs_dir;
    if (!logs_dir.open("logs", O_READ)) {
        return false;
    }
    
    char filename[64];
    SdFile file;
    
    // Find oldest file and delete it
    // This is a simplified implementation - production code would sort by date
    while (file.openNext(&logs_dir, O_READ)) {
        if (file.isFile()) {
            file.getName(filename, sizeof(filename));
            file.close();
            
            String full_path = "logs/" + String(filename);
            if (sd.remove(full_path.c_str())) {
                DEBUG_PRINT(F("Deleted: "));
                DEBUG_PRINTLN(filename);
                break;
            }
        }
    }
    
    logs_dir.close();
    updateSpaceInfo();
    
    return true;
}

bool SDManager::performMaintenance() {
    if (!isAvailable()) {
        return false;
    }
    
    updateSpaceInfo();
    
    if (free_space < MIN_FREE_SPACE_BYTES) {
        return cleanupOldFiles();
    }
    
    return true;
}

void SDManager::forceFlush() {
    if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (current_log_file.isOpen()) {
            current_log_file.flush();
        }
        if (current_event_file.isOpen()) {
            current_event_file.flush();
        }
        xSemaphoreGive(sd_mutex);
    }
}

const char* SDManager::getStatusString() {
    switch (status) {
        case SD_OK: return "OK";
        case SD_NOT_FOUND: return "Not Found";
        case SD_MOUNT_FAILED: return "Mount Failed";
        case SD_LOW_SPACE: return "Low Space";
        case SD_WRITE_ERROR: return "Write Error";
        case SD_READ_ERROR: return "Read Error";
        default: return "Unknown";
    }
}

void SDManager::handleError(SDStatus error) {
    status = error;
    
    switch (error) {
        case SD_WRITE_ERROR:
        case SD_READ_ERROR:
            // Try to reinitialize
            DEBUG_PRINTLN(F("SD error - attempting reinitialize"));
            reinitialize();
            break;
        case SD_LOW_SPACE:
            DEBUG_PRINTLN(F("SD low space - cleaning up"));
            cleanupOldFiles();
            break;
        default:
            break;
    }
}

bool SDManager::reinitialize() {
    sd_initialized = false;
    logging_active = false;
    event_logging_active = false;
    
    // Close any open files
    if (current_log_file.isOpen()) current_log_file.close();
    if (current_event_file.isOpen()) current_event_file.close();
    
    delay(1000); // Wait before retry
    
    return initialize();
}

void SDManager::printDebugInfo() {
#ifdef DEBUG_MODE
    Serial.printf("SD Status: %s\n", getStatusString());
    Serial.printf("Total: %.2f GB, Free: %.2f GB\n", 
        (float)total_space / (1024.0f * 1024.0f * 1024.0f),
        (float)free_space / (1024.0f * 1024.0f * 1024.0f));
    Serial.printf("Logging: %s, Event: %s\n", 
        logging_active ? "ON" : "OFF",
        event_logging_active ? "ON" : "OFF");
#endif
}

void SDManager::listDirectory(const char* path, int levels) {
#ifdef DEBUG_MODE
    SdFile dir;
    if (!dir.open(path, O_READ)) {
        Serial.printf("Failed to open directory: %s\n", path);
        return;
    }
    
    Serial.printf("Directory: %s\n", path);
    
    SdFile file;
    char filename[64];
    
    while (file.openNext(&dir, O_READ)) {
        file.getName(filename, sizeof(filename));
        
        if (file.isDirectory()) {
            Serial.printf("  DIR: %s\n", filename);
            if (levels > 0) {
                String subdir = String(path) + "/" + String(filename);
                listDirectory(subdir.c_str(), levels - 1);
            }
        } else {
            Serial.printf("  FILE: %s (%lu bytes)\n", filename, file.fileSize());
        }
        
        file.close();
    }
    
    dir.close();
#endif
}