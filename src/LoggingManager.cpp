#include "LoggingManager.h"

#include <FS.h>

#include "Config.h"

namespace {
String buildDailyPath(time_t now) {
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  char buffer[32];
  strftime(buffer, sizeof(buffer), "/logs/%Y-%m-%d.csv", &timeinfo);
  return String(buffer);
}

String buildEventPath(time_t now) {
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  char buffer[48];
  strftime(buffer, sizeof(buffer), "/events/event_%Y-%m-%dT%H-%M-%S.csv", &timeinfo);
  return String(buffer);
}

String toIso8601(time_t now) {
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  char buffer[32];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(buffer);
}

}  // namespace

void LoggingManager::begin() {
  spi_.begin(config::SD_SCK, config::SD_MISO, config::SD_MOSI, config::SD_CS);
  ensureFilesystem();
}

bool LoggingManager::ensureFilesystem() {
  if (sd_.cardBegin(&spi_, config::SD_CS, SD_SCK_MHZ(20))) {
    if (!sd_.begin(&spi_, config::SD_CS, SD_SCK_MHZ(20))) {
      return false;
    }
  } else if (!sd_.begin(config::SD_CS, SD_SCK_MHZ(20))) {
    return false;
  }

  if (!sd_.exists("/logs")) {
    sd_.mkdir("/logs");
  }
  if (!sd_.exists("/events")) {
    sd_.mkdir("/events");
  }
  return true;
}

void LoggingManager::update(time_t now, const LogRecord& record) {
  ramBuffer_.push(record);
  uint32_t nowMs = millis();
  if (nowMs - lastLogMs_ < logIntervalMs_) {
    return;
  }
  lastLogMs_ = nowMs;

  if (!ensureDailyFile(now)) {
    return;
  }
  writeRecord(dailyFile_, record);
  maintainStorage();
  if (eventActive_) {
    feedEvent(record);
  }
}

bool LoggingManager::ensureDailyFile(time_t now) {
  String expected = buildDailyPath(now);
  if (currentDailyPath_ != expected) {
    if (dailyFile_) {
      dailyFile_.close();
    }
    currentDailyPath_ = expected;
    dailyFile_ = sd_.open(currentDailyPath_.c_str(), O_RDWR | O_CREAT | O_AT_END);
    if (!dailyFile_) {
      return false;
    }
    if (dailyFile_.fileSize() == 0) {
      dailyFile_.println(F("timestamp,iso8601,pulses,freq_hz,level_v,flow_lps,flow_baseline,flow_diff_pct,flow_min,flow_mean,flow_median,level_cm,level_baseline_cm,level_full_cm,level_diff_pct,level_noise"));
    }
  }
  return true;
}

bool LoggingManager::writeRecord(File& file, const LogRecord& record) {
  if (!file) {
    return false;
  }
  String line;
  line.reserve(196);
  line += String(record.timestamp);
  line += ',';
  line += record.iso8601;
  line += ',';
  line += String(record.pulseCount);
  line += ',';
  line += String(record.pulseFrequency, 4);
  line += ',';
  line += String(record.levelVoltage, 4);
  line += ',';
  line += String(record.flow.instantaneousLps, 4);
  line += ',';
  line += String(record.flow.baselineLps, 4);
  line += ',';
  line += String(record.flow.differencePct, 2);
  line += ',';
  line += String(record.flow.minimumHealthyLps, 4);
  line += ',';
  line += String(record.flow.meanLps, 4);
  line += ',';
  line += String(record.flow.medianLps, 4);
  line += ',';
  line += String(record.level.instantaneousCm, 2);
  line += ',';
  line += String(record.level.baselineCm, 2);
  line += ',';
  line += String(record.level.fullTankCm, 2);
  line += ',';
  line += String(record.level.differencePct, 2);
  line += ',';
  line += String(record.level.noiseMetric, 2);
  return file.println(line.c_str());
}

void LoggingManager::maintainStorage() {
  uint64_t freeBytes = sd_.freeClusterCount();
  freeBytes *= sd_.bytesPerCluster();
  if (freeBytes >= config::SD_MIN_FREE_BYTES) {
    return;
  }

  File logsDir = sd_.open("/logs");
  if (!logsDir) {
    return;
  }
  File oldest;
  while (freeBytes < config::SD_MIN_FREE_BYTES && (oldest = logsDir.openNextFile())) {
    String path = String("/logs/") + oldest.name();
    uint64_t size = oldest.fileSize();
    oldest.close();
    sd_.remove(path.c_str());
    freeBytes += size;
  }
  logsDir.close();
}

void LoggingManager::triggerEvent(const LogRecord& record) {
  if (eventActive_) {
    return;
  }
  refreshEventFileName(record.timestamp);
  eventFile_ = sd_.open(currentEventPath_.c_str(), O_RDWR | O_CREAT | O_TRUNC);
  if (!eventFile_) {
    return;
  }
  eventFile_.println(F("timestamp,iso8601,pulses,freq_hz,level_v,flow_lps,flow_baseline,flow_diff_pct,flow_min,flow_mean,flow_median,level_cm,level_baseline_cm,level_full_cm,level_diff_pct,level_noise"));
  ramBuffer_.forEachOldestFirst([this](const LogRecord& past) { writeRecord(eventFile_, past); });
  eventActive_ = true;
  eventEndMs_ = millis() + (60UL * 60UL * 1000UL);
  feedEvent(record);
}

void LoggingManager::feedEvent(const LogRecord& record) {
  if (!eventActive_) {
    return;
  }
  writeRecord(eventFile_, record);
  if (millis() > eventEndMs_) {
    eventActive_ = false;
    if (eventFile_) {
      eventFile_.flush();
      eventFile_.close();
    }
  }
}

void LoggingManager::loop() {
  if (eventActive_ && eventFile_) {
    eventFile_.flush();
  }
  if (dailyFile_) {
    dailyFile_.flush();
  }
}

void LoggingManager::refreshEventFileName(time_t now) {
  currentEventPath_ = buildEventPath(now);
}

void LoggingManager::flushRamBufferToFile(File& file) {
  ramBuffer_.forEachOldestFirst([&](const LogRecord& record) { writeRecord(file, record); });
}

