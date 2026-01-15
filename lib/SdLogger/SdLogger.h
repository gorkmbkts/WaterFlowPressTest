#pragma once

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <deque>
#include <functional>

#include <../Utils/Utils.h>

class ConfigService;

class SdLogger {
  public:
    using SdReadyCallback = std::function<void()>;
    
    bool begin(uint8_t csPin, SPIClass& spi, ConfigService* config);
    void update();
    void log(const utils::SensorMetrics& metrics);
    void requestEventSnapshot();
    void prepareForRemoval();
    void setSdReadyCallback(SdReadyCallback callback);
    bool isReady() const { return _sdReady; }
    bool hasEventActive() const { return _eventActive; }
    bool isSafeToRemove() const { return _safeToRemove; }

  private:
    struct LogEntry {
        time_t timestamp;
        utils::SensorMetrics metrics;
    };

    bool ensureMount();
    void ensureDirectories();
    void ensureDailyLog(time_t timestamp);
    void ensureFreeSpace();
    void writeCsvHeader(File& file);
    void writeLogLine(File& file, const utils::SensorMetrics& metrics);
    void trimLogFile(const String& path);
    void startEventFile(time_t timestamp);
    void closeEventFile();
    void flushFiles();
    void syncBufferLimit();

    File _logFile;
    File _eventFile;
    String _currentLogPath;
    bool _sdReady = false;
    bool _eventRequested = false;
    bool _eventActive = false;
    bool _safeToRemove = false;
    unsigned long _safeRemovalTime = 0;  // Güvenli kaldırma zamanı
    time_t _eventEndTime = 0;
    uint8_t _csPin = 5;
    SPIClass* _spi = nullptr;
    ConfigService* _config = nullptr;
    SdReadyCallback _sdReadyCallback;
    std::deque<LogEntry> _buffer;
    size_t _maxBufferEntries = 1200;  // 20 minutes at 1 Hz
};

