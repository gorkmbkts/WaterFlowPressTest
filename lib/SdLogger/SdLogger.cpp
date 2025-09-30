#include "SdLogger.h"

#include <SPI.h>
#include <algorithm>
#include <vector>

#include "../ConfigService/ConfigService.h"

namespace {
constexpr uint64_t FOUR_GB = 4ULL * 1024ULL * 1024ULL * 1024ULL;
}

bool SdLogger::begin(uint8_t csPin, SPIClass& spi, ConfigService* config) {
    _csPin = csPin;
    _spi = &spi;
    _config = config;
    _spi->begin();
    _sdReady = _sd.begin(SdSpiConfig(_csPin, DEDICATED_SPI, SPI_FULL_SPEED, _spi));
    if (_sdReady) {
        ensureDirectories();
    }
    return _sdReady;
}

bool SdLogger::ensureMount() {
    if (!_sdReady) {
        _sdReady = _sd.begin(SdSpiConfig(_csPin, DEDICATED_SPI, SPI_FULL_SPEED, _spi));
        if (_sdReady) {
            ensureDirectories();
        }
    }
    return _sdReady;
}

void SdLogger::ensureDirectories() {
    if (!_sdReady) {
        return;
    }
    if (!_sd.exists("/logs")) {
        _sd.mkdir("/logs");
    }
    if (!_sd.exists("/events")) {
        _sd.mkdir("/events");
    }
}

void SdLogger::ensureDailyLog(time_t timestamp) {
    if (!_sdReady) {
        return;
    }
    struct tm timeinfo;
    localtime_r(&timestamp, &timeinfo);
    char path[32];
    strftime(path, sizeof(path), "/logs/%Y-%m-%d.csv", &timeinfo);
    if (_currentLogPath == path && _logFile) {
        return;
    }
    if (_logFile) {
        _logFile.close();
    }
    _currentLogPath = path;
    _logFile = _sd.open(path, O_RDWR | O_CREAT | O_AT_END);
    if (!_logFile) {
        _sdReady = false;
        return;
    }
    if (_logFile.size() == 0) {
        writeCsvHeader(_logFile);
    }
}

void SdLogger::writeCsvHeader(FsFile& file) {
    file.println(F("timestamp,iso8601,pulses,flow_lps,flow_baseline_lps,flow_diff_pct,flow_min_healthy_lps,flow_mean_lps,flow_median_lps,flow_std_lps,flow_min_lps,flow_max_lps,tank_height_cm,tank_empty_cm,tank_full_cm,tank_diff_pct,tank_noise_pct,tank_mean_cm,tank_median_cm,tank_std_cm,tank_min_cm,tank_max_cm,level_voltage,ema_voltage,density_factor"));
}

void SdLogger::writeLogLine(FsFile& file, const utils::SensorMetrics& metrics) {
    struct tm timeinfo;
    localtime_r(&metrics.timestamp, &timeinfo);
    char iso[25];
    strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%S", &timeinfo);
    file.print(metrics.timestamp);
    file.print(',');
    file.print(iso);
    file.print(',');
    file.print(metrics.pulseCount);
    file.print(',');
    file.print(metrics.flowLps, 4);
    file.print(',');
    file.print(metrics.flowBaselineLps, 4);
    file.print(',');
    file.print(metrics.flowDiffPercent, 2);
    file.print(',');
    file.print(metrics.flowMinHealthyLps, 4);
    file.print(',');
    file.print(metrics.flowMeanLps, 4);
    file.print(',');
    file.print(metrics.flowMedianLps, 4);
    file.print(',');
    file.print(metrics.flowStdDevLps, 4);
    file.print(',');
    file.print(metrics.flowMinLps, 4);
    file.print(',');
    file.print(metrics.flowMaxLps, 4);
    file.print(',');
    file.print(metrics.tankHeightCm, 3);
    file.print(',');
    file.print(metrics.tankEmptyEstimateCm, 3);
    file.print(',');
    file.print(metrics.tankFullEstimateCm, 3);
    file.print(',');
    file.print(metrics.tankDiffPercent, 2);
    file.print(',');
    file.print(metrics.tankNoisePercent, 2);
    file.print(',');
    file.print(metrics.tankMeanCm, 3);
    file.print(',');
    file.print(metrics.tankMedianCm, 3);
    file.print(',');
    file.print(metrics.tankStdDevCm, 3);
    file.print(',');
    file.print(metrics.tankMinObservedCm, 3);
    file.print(',');
    file.print(metrics.tankMaxObservedCm, 3);
    file.print(',');
    file.print(metrics.levelVoltage, 4);
    file.print(',');
    file.print(metrics.emaVoltage, 4);
    file.print(',');
    file.println(metrics.densityFactor, 3);
}

void SdLogger::ensureFreeSpace() {
    if (!_sdReady) {
        return;
    }
    FatVolume* vol = _sd.vol();
    if (!vol) {
        return;
    }
    uint64_t freeClusters = vol->freeClusterCount();
    uint64_t bytesPerCluster = static_cast<uint64_t>(vol->blocksPerCluster()) * 512ULL;
    uint64_t freeBytes = freeClusters * bytesPerCluster;
    if (freeBytes >= FOUR_GB) {
        return;
    }

    FsFile dir = _sd.open("/logs");
    if (!dir) {
        return;
    }
    std::vector<String> logFiles;
    FsFile file;
    while (file.openNext(&dir, O_RDONLY)) {
        if (file.isFile()) {
            char name[64];
            file.getName(name, sizeof(name));
            logFiles.emplace_back(name);
        }
        file.close();
    }
    dir.close();
    std::sort(logFiles.begin(), logFiles.end());
    for (const auto& name : logFiles) {
        if (freeBytes >= FOUR_GB) {
            break;
        }
        String fullPath = String("/logs/") + name;
        FsFile toDelete = _sd.open(fullPath.c_str(), O_RDWR);
        if (toDelete) {
            uint32_t clusters = toDelete.clusterCount();
            uint64_t reclaimed = static_cast<uint64_t>(clusters) * bytesPerCluster;
            toDelete.close();
            _sd.remove(fullPath.c_str());
            freeBytes += reclaimed;
        }
    }
}

void SdLogger::syncBufferLimit() {
    if (_config) {
        uint32_t interval = _config->loggingIntervalMs();
        if (interval == 0) {
            interval = 1000;
        }
        size_t entries = (20UL * 60UL * 1000UL) / interval;
        _maxBufferEntries = std::max<size_t>(entries, 60);
    }
}

void SdLogger::log(const utils::SensorMetrics& metrics) {
    if (!ensureMount()) {
        return;
    }
    ensureDailyLog(metrics.timestamp);
    ensureFreeSpace();
    if (!_logFile) {
        return;
    }
    syncBufferLimit();
    writeLogLine(_logFile, metrics);

    if (_eventActive) {
        writeLogLine(_eventFile, metrics);
    }

    LogEntry entry{metrics.timestamp, metrics};
    _buffer.push_back(entry);
    while (_buffer.size() > _maxBufferEntries) {
        _buffer.pop_front();
    }

    if (_eventRequested) {
        startEventFile(metrics.timestamp);
        _eventRequested = false;
    }

#ifdef PROJECT_KALKAN_DEBUG
    _logFile.flush();
    if (_eventActive) {
        _eventFile.flush();
    }
#endif
}

void SdLogger::startEventFile(time_t timestamp) {
    if (!_sdReady) {
        return;
    }
    struct tm timeinfo;
    localtime_r(&timestamp, &timeinfo);
    char name[48];
    strftime(name, sizeof(name), "/events/event_%Y-%m-%dT%H-%M-%S.csv", &timeinfo);
    if (_eventFile) {
        _eventFile.close();
    }
    _eventFile = _sd.open(name, O_RDWR | O_CREAT | O_TRUNC);
    if (!_eventFile) {
        return;
    }
    writeCsvHeader(_eventFile);
    for (const auto& entry : _buffer) {
        writeLogLine(_eventFile, entry.metrics);
    }
    _eventActive = true;
    _eventEndTime = timestamp + 60 * 60;  // 60 minutes
}

void SdLogger::closeEventFile() {
    if (_eventFile) {
        _eventFile.flush();
        _eventFile.close();
    }
    _eventActive = false;
    _eventEndTime = 0;
}

void SdLogger::update() {
    if (!_sdReady) {
        return;
    }
    ensureFreeSpace();
    if (_eventActive) {
        time_t now = time(nullptr);
        if (now >= _eventEndTime) {
            closeEventFile();
        }
    }
    flushFiles();
}

void SdLogger::flushFiles() {
    if (_logFile) {
        _logFile.sync();
    }
    if (_eventActive) {
        _eventFile.sync();
    }
}

void SdLogger::requestEventSnapshot() {
    _eventRequested = true;
}

