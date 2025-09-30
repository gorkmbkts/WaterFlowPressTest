#pragma once

#include <SdFat.h>
#include <SPI.h>
#include <array>

#include "Config.h"
#include "SensorData.h"

class LoggingManager {
 public:
  explicit LoggingManager(SdFat& sd) : sd_(sd) {}

  void begin();
  void update(time_t now, const LogRecord& record);
  void triggerEvent(const LogRecord& record);
  void feedEvent(const LogRecord& record);
  void loop();
  void setLogInterval(uint32_t intervalMs) { logIntervalMs_ = intervalMs; }
  uint32_t logInterval() const { return logIntervalMs_; }

 private:
  bool ensureFilesystem();
  bool ensureDailyFile(time_t now);
  bool writeRecord(File& file, const LogRecord& record);
  void maintainStorage();
  void refreshEventFileName(time_t now);
  void flushRamBufferToFile(File& file);

  struct RamBuffer {
    std::array<LogRecord, config::RAM_LOG_CAPACITY> buffer;
    size_t head = 0;
    size_t count = 0;

    void push(const LogRecord& record) {
      buffer[head] = record;
      head = (head + 1) % buffer.size();
      if (count < buffer.size()) {
        count++;
      }
    }

    template <typename Callback>
    void forEachOldestFirst(Callback cb) const {
      size_t valid = count;
      size_t start = (head + buffer.size() - count) % buffer.size();
      for (size_t i = 0; i < valid; ++i) {
        size_t idx = (start + i) % buffer.size();
        cb(buffer[idx]);
      }
    }
  };

  SdFat& sd_;
  SPIClass spi_{VSPI};
  File dailyFile_;
  File eventFile_;
  String currentDailyPath_;
  String currentEventPath_;
  bool eventActive_ = false;
  uint32_t logIntervalMs_ = config::DEFAULT_LOG_INTERVAL_MS;
  uint32_t lastLogMs_ = 0;
  uint32_t eventEndMs_ = 0;
  RamBuffer ramBuffer_;
};

