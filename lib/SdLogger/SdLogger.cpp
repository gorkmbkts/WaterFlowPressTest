#include "SdLogger.h"

#include <SD.h>
#include <SPI.h>
#include <algorithm>
#include <vector>

#include "../ConfigService/ConfigService.h"

namespace {
constexpr uint64_t FOUR_GB = 4ULL * 1024ULL * 1024ULL * 1024ULL;
}

bool SdLogger::begin(uint8_t csPin, SPIClass& spi, ConfigService* config) {
    Serial.println("[SdLogger] Begin initialization...");
    _csPin = csPin;
    _spi = &spi;
    _config = config;
    Serial.printf("[SdLogger] CS Pin: %d\n", _csPin);
    
    // SPI ayarlarını manuel olarak yap
    _spi->begin();
    _spi->setFrequency(1000000); // 1MHz ile başla (daha güvenli)
    delay(500); // SD kart için bekleme
    
    Serial.println("[SdLogger] SPI initialized with 1MHz");
    
    // Birden fazla deneme yap
    for (int attempt = 1; attempt <= 5; attempt++) {
        Serial.printf("[SdLogger] SD.begin attempt %d/5...\n", attempt);
        _sdReady = SD.begin(_csPin, *_spi, 1000000); // SPI referansı ve frekans belirt
        
        if (_sdReady) {
            Serial.println("[SdLogger] ✅ SD.begin SUCCESS!");
            ensureDirectories();
            Serial.println("[SdLogger] Directories ensured");
            return true;
        } else {
            Serial.printf("[SdLogger] ❌ Attempt %d failed\n", attempt);
            delay(1000); // Denemeler arası bekleme
        }
    }
    
    Serial.println("[SdLogger] ❌ All attempts failed!");
    return false;
}

bool SdLogger::ensureMount() {
    if (!_sdReady) {
        Serial.println("[SdLogger] Attempting to mount SD...");
        _sdReady = SD.begin(_csPin);
        Serial.printf("[SdLogger] Mount result: %s\n", _sdReady ? "SUCCESS" : "FAILED");
        if (_sdReady) {
            ensureDirectories();
            Serial.println("[SdLogger] Mount successful, directories ready");
        }
    }
    return _sdReady;
}

void SdLogger::ensureDirectories() {
    if (!_sdReady) {
        return;
    }
    if (!SD.exists("/logs")) {
        SD.mkdir("/logs");
    }
    if (!SD.exists("/events")) {
        SD.mkdir("/events");
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
    _logFile = SD.open(path, FILE_APPEND);
    if (!_logFile) {
        _sdReady = false;
        return;
    }
    if (_logFile.size() == 0) {
        writeCsvHeader(_logFile);
    }
}

void SdLogger::writeCsvHeader(File& file) {
    file.print(F("timestamp,iso8601,pulses,flow_lps,flow_baseline_lps,flow_diff_pct,flow_min_healthy_lps,flow_mean_lps,flow_median_lps,flow_std_lps,flow_min_lps,flow_max_lps,"));
    file.print(F("flow_pulse_mean_us,flow_pulse_median_us,flow_pulse_std_us,flow_pulse_cv,flow_period_count"));
    for (size_t i = 0; i < utils::MAX_FLOW_PERIOD_SAMPLES; ++i) {
        file.print(F(",flow_period_us_"));
        file.print(i);
    }
    file.println(F(",tank_height_cm,tank_empty_cm,tank_full_cm,tank_diff_pct,tank_noise_pct,tank_mean_cm,tank_median_cm,tank_std_cm,tank_min_cm,tank_max_cm,level_voltage_inst,level_voltage_avg,level_voltage_median,level_voltage_trimmed,level_voltage_std,level_voltage_ema,level_current_ma,level_depth_mm,level_height_raw_cm,level_height_filtered_cm,level_velocity_mm_s,density_factor"));
}

void SdLogger::writeLogLine(File& file, const utils::SensorMetrics& metrics) {
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
    file.print(metrics.flowPulseMeanUs, 3);
    file.print(',');
    file.print(metrics.flowPulseMedianUs, 3);
    file.print(',');
    file.print(metrics.flowPulseStdUs, 3);
    file.print(',');
    file.print(metrics.flowPulseCv, 2);
    file.print(',');
    file.print(static_cast<uint32_t>(metrics.flowPeriodCount));
    for (size_t i = 0; i < utils::MAX_FLOW_PERIOD_SAMPLES; ++i) {
        file.print(',');
        if (i < metrics.flowPeriodCount) {
            file.print(metrics.flowRecentPeriods[i]);
        }
    }
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
    file.print(metrics.levelAverageVoltage, 4);
    file.print(',');
    file.print(metrics.levelMedianVoltage, 4);
    file.print(',');
    file.print(metrics.levelTrimmedVoltage, 4);
    file.print(',');
    file.print(metrics.levelStdDevVoltage, 4);
    file.print(',');
    file.print(metrics.levelEmaVoltage, 4);
    file.print(',');
    file.print(metrics.levelCurrentMa, 3);
    file.print(',');
    file.print(metrics.levelDepthMm, 3);
    file.print(',');
    file.print(metrics.levelRawHeightCm, 3);
    file.print(',');
    file.print(metrics.levelFilteredHeightCm, 3);
    file.print(',');
    file.print(metrics.levelAlphaBetaVelocity, 3);
    file.print(',');
    file.println(metrics.densityFactor, 3);
}

void SdLogger::ensureFreeSpace() {
    if (!_sdReady) {
        return;
    }
    
    // SD.h ile free space kontrolü daha basit
    uint64_t totalBytes = SD.totalBytes();
    uint64_t usedBytes = SD.usedBytes();
    uint64_t freeBytes = totalBytes - usedBytes;
    
    if (freeBytes >= FOUR_GB) {
        return;
    }

    File dir = SD.open("/logs");
    if (!dir) {
        return;
    }
    
    std::vector<String> logFiles;
    File file = dir.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            logFiles.emplace_back(file.name());
        }
        file.close();
        file = dir.openNextFile();
    }
    dir.close();
    
    std::sort(logFiles.begin(), logFiles.end());
    for (const auto& name : logFiles) {
        usedBytes = SD.usedBytes();
        freeBytes = totalBytes - usedBytes;
        if (freeBytes >= FOUR_GB) {
            break;
        }
        String fullPath = String("/logs/") + name;
        trimLogFile(fullPath);
    }
}

void SdLogger::trimLogFile(const String& path) {
    File source = SD.open(path.c_str(), FILE_READ);
    if (!source) {
        return;
    }
    
    size_t size = source.size();
    const size_t minimumSize = 512UL * 1024UL;  // only trim files larger than 512 KB
    if (size < minimumSize) {
        source.close();
        return;
    }

    size_t keepBytes = size / 2;
    size_t offset = size - keepBytes;
    if (!source.seek(offset)) {
        source.close();
        return;
    }

    // Skip to next newline to align with row boundary
    while (source.available()) {
        char ch = static_cast<char>(source.read());
        if (ch == '\n') {
            break;
        }
    }

    String tempPath = path + ".tmp";
    File temp = SD.open(tempPath.c_str(), FILE_WRITE);
    if (!temp) {
        source.close();
        return;
    }

    writeCsvHeader(temp);

    char buffer[256];
    while (source.available()) {
        int bytesRead = source.readBytes(buffer, sizeof(buffer));
        if (bytesRead <= 0) {
            break;
        }
        temp.write((uint8_t*)buffer, bytesRead);
    }
    temp.flush();
    temp.close();
    source.close();

    SD.remove(path.c_str());
    SD.rename(tempPath.c_str(), path.c_str());
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
    size_t entrySize = sizeof(LogEntry);
    if (entrySize == 0) {
        entrySize = 1;
    }
    size_t heapLimit = (ESP.getFreeHeap() / 2) / entrySize;
    if (heapLimit < 60) {
        heapLimit = 60;
    }
    if (_maxBufferEntries > heapLimit) {
        _maxBufferEntries = heapLimit;
    }
}

void SdLogger::log(const utils::SensorMetrics& metrics) {
    // SD kart güvenli kaldırma modundayken log yapma
    if (_safeToRemove) {
        // Sadece buffer'a ekle, SD'ye yazma
        syncBufferLimit();
        LogEntry entry{metrics.timestamp, metrics};
        _buffer.push_back(entry);
        while (_buffer.size() > _maxBufferEntries) {
            _buffer.pop_front();
        }
        return;
    }
    
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
    _eventFile = SD.open(name, FILE_WRITE);
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
    // Güvenli kaldırma modundayken 3 aşamalı kontrol
    if (_safeToRemove) {
        unsigned long now = millis();
        unsigned long elapsed = now - _safeRemovalTime;
        
        // AŞAMA 1: İlk 15 saniye hiç kart algılamaya çalışma
        if (elapsed < 15000) {
            static unsigned long lastWaitMessage = 0;
            if (now - lastWaitMessage > 5000) {
                unsigned long remaining = (15000 - elapsed) / 1000;
                Serial.printf("⏰ Kart algılaması durduruldu, %lu saniye kaldı...\n", remaining);
                lastWaitMessage = now;
            }
            return;
        }
        
        // AŞAMA 2 & 3: 15 saniye sonra kart durumunu kontrol et
        static unsigned long lastCheckTime = 0;
        static bool cardWasRemoved = false;
        
        // 2 saniyede bir kontrol et
        if (now - lastCheckTime > 2000) {
            lastCheckTime = now;
            
            // SPI'yi yeniden başlat ve kart var mı test et  
            _spi->end();
            delay(100);
            _spi->begin();
            delay(100);
            
            bool cardDetected = SD.begin(_csPin);
            
            if (!cardDetected && !cardWasRemoved) {
                // Kart ilk defa çıkarıldı
                cardWasRemoved = true;
                Serial.println("🎉 SD kart başarıyla fiziksel olarak çıkarıldı!");
                Serial.println("💡 Kartı yeniden taktığınızda algılanacak...");
            } else if (cardDetected && cardWasRemoved) {
                // Kart geri takıldı
                Serial.println("✅ SD kart yeniden takıldı ve algılandı!");
                _sdReady = true;
                _safeToRemove = false;
                _safeRemovalTime = 0;
                cardWasRemoved = false;
                ensureDirectories();
                
                if (_sdReadyCallback) {
                    Serial.println("📢 SD hazır callback çağrılıyor...");
                    _sdReadyCallback();
                } else {
                    Serial.println("❌ SD hazır callback null!");
                }
            } else if (cardDetected && !cardWasRemoved) {
                // Kart hâlâ takılı, kullanıcı henüz çıkarmamış
                Serial.println("💭 SD kart hâlâ takılı, fiziksel olarak çıkarmanızı bekliyoruz...");
            }
        }
        return;
    }
    
    // SD kart bağlantısını kontrol et (sadece normal modda)
    if (!_sdReady) {
        Serial.println("🔍 SD kart yeniden bağlanmaya çalışılıyor...");
        
        // SD.begin() çağrılmadan önce SPI'yi yeniden başlat
        _spi->end();
        delay(100);
        _spi->begin();
        delay(100);
        
        bool reconnected = SD.begin(_csPin);
        if (reconnected) {
            Serial.println("✅ SD kart yeniden algılandı!");
            _sdReady = true;
            ensureDirectories();
            
            // SD kart hazır callback'ini çağır
            if (_sdReadyCallback) {
                Serial.println("📢 SD hazır callback çağrılıyor...");
                _sdReadyCallback();
            } else {
                Serial.println("❌ SD hazır callback null!");
            }
        } else {
            // Sessizce tekrar dene, spam yapmamak için
            static unsigned long lastRetry = 0;
            if (millis() - lastRetry > 2000) {
                Serial.println("🔄 SD kart algılanamadı, 2 saniye sonra tekrar denenecek...");
                lastRetry = millis();
            }
        }
        return; // SD hazır değilse diğer işlemleri yapma
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
        _logFile.flush();
    }
    if (_eventActive) {
        _eventFile.flush();
    }
}

void SdLogger::requestEventSnapshot() {
    _eventRequested = true;
}

void SdLogger::setSdReadyCallback(SdReadyCallback callback) {
    _sdReadyCallback = callback;
}

void SdLogger::prepareForRemoval() {
    Serial.printf("🔧 prepareForRemoval() çağrıldı, _sdReady: %s\n", _sdReady ? "true" : "false");
    
    if (!_sdReady) {
        Serial.println("⚠️  SD hazır değil, sadece flag ayarlanıyor");
        _safeToRemove = true;
        _safeRemovalTime = millis();
        return;
    }
    
    // Tüm dosyaları kapat ve sync et
    if (_logFile) {
        _logFile.flush();  // Buffer'ları boşalt
        _logFile.close();  // Dosyayı kapat
    }
    
    if (_eventFile) {
        _eventFile.flush(); // Buffer'ları boşalt
        _eventFile.close(); // Dosyayı kapat
    }
    
    // SD kartı güvenli unmount et
    Serial.println("💾 SD kartı güvenli unmount ediliyor...");
    delay(200);            // Dosya işlemlerinin tamamlanması için bekle
    SD.end();              // SD kart nesnesini kapat
    
    _sdReady = false;
    _safeToRemove = true;
    _safeRemovalTime = millis();  // Güvenli kaldırma zamanını kaydet
    Serial.println("✅ SD kart güvenli kaldırma moduna alındı");
    Serial.println("⏰ 15 saniye boyunca kart algılaması durdurulacak...");
    
    // Dosya durumlarını temizle
    _eventActive = false;
    _eventRequested = false;
    _currentLogPath = "";
}

