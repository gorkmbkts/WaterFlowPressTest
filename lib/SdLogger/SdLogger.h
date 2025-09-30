#pragma once

#include <Arduino.h>
#include <SdFat.h>
#include <deque>

#include <Utils/Utils.h>

class ConfigService;

class SdLogger {
  public:
    bool begin(uint8_t csPin, SPIClass& spi, ConfigService* config);
    void update();
    void log(const utils::SensorMetrics& metrics);
    void requestEventSnapshot();
    bool isReady() const { return _sdReady; }
    bool hasEventActive() const { return _eventActive; }

  private:
    struct LogEntry {
        time_t timestamp;
        utils::SensorMetrics metrics;
    };

    bool ensureMount();
    void ensureDirectories();
    void ensureDailyLog(time_t timestamp);
    void ensureFreeSpace();
    void writeCsvHeader(FsFile& file);
    void writeLogLine(FsFile& file, const utils::SensorMetrics& metrics);
    void startEventFile(time_t timestamp);
    void closeEventFile();
    void flushFiles();
    void syncBufferLimit();

    SdFat32 _sd;
    FsFile _logFile;
    FsFile _eventFile;
    String _currentLogPath;
    bool _sdReady = false;
    bool _eventRequested = false;
    bool _eventActive = false;
    time_t _eventEndTime = 0;
    uint8_t _csPin = 5;
    SPIClass* _spi = nullptr;
    ConfigService* _config = nullptr;
    std::deque<LogEntry> _buffer;
    size_t _maxBufferEntries = 1200;  // 20 minutes at 1 Hz
};

