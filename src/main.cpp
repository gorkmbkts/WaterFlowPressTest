#include <Arduino.h>
#include <ArduinoJson.h>
#include <Bounce2.h>
#include <LiquidCrystal_I2C.h>
#include <SdFat.h>
#include <SPI.h>
#include <Wire.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <climits>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "Config.h"
#include "Calibration.h"
#include "SensorData.h"
#include "Statistics.h"

// -----------------------------------------------------------------------------
// Compile-time debug flag
#ifndef DEBUG_PROJECT_KALKAN
#define DEBUG_PROJECT_KALKAN 0
#endif

#if DEBUG_PROJECT_KALKAN
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINTF(...)
#endif

template <typename T>
T clampValue(T value, T minValue, T maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

// -----------------------------------------------------------------------------
// Global peripherals and RTOS handles
LiquidCrystal_I2C lcd(Config::LCD_I2C_ADDRESS, Config::LCD_COLS, Config::LCD_ROWS);
Bounce button1;
Bounce button2;
SdFat sd;
File logFile;

SPIClass spi(VSPI);
QueueHandle_t sensorQueue = nullptr;
SemaphoreHandle_t logMutex = nullptr;
portMUX_TYPE portMUX_GLOBAL = portMUX_INITIALIZER_UNLOCKED;
bool sdReady = false;

// -----------------------------------------------------------------------------
// Flow sensor ISR state
static volatile uint32_t g_pulseCount = 0;
static volatile uint32_t g_lastPulseMicros = 0;
static volatile bool g_flowSignalPresent = false;

void IRAM_ATTR flowISR() {
  uint32_t now = micros();
  g_lastPulseMicros = now;
  g_pulseCount++;
  g_flowSignalPresent = true;
}

// -----------------------------------------------------------------------------
// Helper data structures
struct LogRecord {
  SensorSample sample;
  uint64_t unixTime = 0;
  char isoTimestamp[25];
};

class CircularLogBuffer {
 public:
  void push(const LogRecord &record) {
    buffer_[head_] = record;
    head_ = (head_ + 1) % buffer_.size();
    if (size_ < buffer_.size()) {
      ++size_;
    } else {
      tail_ = (tail_ + 1) % buffer_.size();
    }
  }

  template <typename Callback>
  void forEach(Callback cb) const {
    size_t idx = tail_;
    for (size_t i = 0; i < size_; ++i) {
      cb(buffer_[idx]);
      idx = (idx + 1) % buffer_.size();
    }
  }

 private:
  std::array<LogRecord, Config::LOG_BUFFER_CAPACITY> buffer_{};
  size_t head_ = 0;
  size_t tail_ = 0;
  size_t size_ = 0;
};

CircularLogBuffer logBuffer;

// -----------------------------------------------------------------------------
// Analytics helpers
class FlowAnalytics {
 public:
  FlowMetrics push(float lps) {
    window_.push(lps);
    meanVar_.push(lps);
    minMax_.push(lps);

    if (lps > pumpOnThreshold_) {
      pumpOn_ = true;
      pumpOffCounter_ = 0;
      float percentile90 = window_.percentile(90.0f);
      float percentile10 = window_.percentile(10.0f);
      baselineLps_ = lerp(baselineLps_, percentile90, baselineAlpha_);
      minHealthyLps_ = lerp(minHealthyLps_, percentile10, baselineAlpha_);
    } else {
      pumpOffCounter_++;
      if (pumpOffCounter_ > pumpOffHold_) {
        pumpOn_ = false;
      }
    }

    FlowMetrics metrics;
    metrics.instantaneousLps = lps;
    metrics.baselineLps = baselineLps_;
    metrics.minHealthyLps = minHealthyLps_;
    metrics.meanLps = meanVar_.mean();
    metrics.medianLps = window_.median();
    metrics.minObservedLps = minMax_.min();
    metrics.maxObservedLps = minMax_.max();
    metrics.stddevLps = meanVar_.stddev();
    metrics.diffPercent = (baselineLps_ > 0.01f) ? ((lps - baselineLps_) / baselineLps_) * 100.0f : 0.0f;
    return metrics;
  }

  bool pumpOn() const { return pumpOn_; }

 private:
  static float lerp(float current, float target, float alpha) {
    if (current == 0.0f) return target;
    return current + alpha * (target - current);
  }

  RollingPercentiles<Config::FLOW_WINDOW_SIZE> window_;
  RunningMeanVariance<float> meanVar_;
  RunningMinMax minMax_;
  float baselineLps_ = 0.0f;
  float minHealthyLps_ = 0.0f;
  bool pumpOn_ = false;
  uint16_t pumpOffCounter_ = 0;
  const uint16_t pumpOffHold_ = 10;
  const float pumpOnThreshold_ = 0.05f;
  const float baselineAlpha_ = 0.05f;
};

class LevelAnalytics {
 public:
  LevelAnalytics() { minMax_.reset(); }

  LevelMetrics push(float levelCm, float emaLevel, float noisePercent) {
    window_.push(levelCm);
    meanVar_.push(levelCm);
    minMax_.push(levelCm);

    // detect calm surface for theta baseline
    float std = meanVar_.stddev();
    if (std < calmStdThreshold_) {
      thetaCm_ = lerp(thetaCm_, emaLevel, thetaAlpha_);
    }

    // update sigma (full tank) when stable high level
    if (std < calmStdThreshold_ * 2 && levelCm > sigmaCm_) {
      sigmaCm_ = lerp(sigmaCm_, levelCm, sigmaAlpha_);
    }

    LevelMetrics metrics;
    metrics.instantaneousCm = levelCm;
    metrics.thetaCm = thetaCm_;
    metrics.sigmaCm = sigmaCm_;
    metrics.diffPercent = (sigmaCm_ > 1.0f) ? ((levelCm - sigmaCm_) / sigmaCm_) * 100.0f : 0.0f;
    metrics.noisePercent = noisePercent;
    metrics.meanCm = meanVar_.mean();
    metrics.medianCm = window_.median();
    metrics.minCm = minMax_.min();
    metrics.maxCm = minMax_.max();
    metrics.stddevCm = meanVar_.stddev();
    return metrics;
  }

  float theta() const { return thetaCm_; }
  float sigma() const { return sigmaCm_; }
  float mean() const { return meanVar_.mean(); }
  float median() const { return window_.median(); }
  float min() const { return minMax_.min(); }
  float max() const { return minMax_.max(); }
  float stddev() const { return meanVar_.stddev(); }

 private:
  static float lerp(float current, float target, float alpha) {
    if (current == 0.0f) return target;
    return current + alpha * (target - current);
  }

  RollingPercentiles<Config::LEVEL_WINDOW_SIZE> window_;
  RunningMeanVariance<float> meanVar_;
  RunningMinMax minMax_;
  float thetaCm_ = 0.0f;
  float sigmaCm_ = Config::LEVEL_RANGE_CM;
  const float calmStdThreshold_ = 1.0f;  // cm
  const float thetaAlpha_ = 0.05f;
  const float sigmaAlpha_ = 0.02f;
};

// -----------------------------------------------------------------------------
// Calibration
CalibrationConfig calibration = {1.0f, Config::LEVEL_VOLTAGE_MIN, Config::LEVEL_VOLTAGE_MAX};

float voltageToHeightCm(float voltage) {
  float numerator = (voltage - calibration.zeroVoltage);
  float denominator = (calibration.fullVoltage - calibration.zeroVoltage);
  denominator = (denominator <= 0.0001f) ? 1.0f : denominator;
  float normalized = numerator / denominator;
  normalized = clampValue(normalized, 0.0f, 1.0f);
  float waterHeight = normalized * Config::LEVEL_RANGE_CM;
  return waterHeight * calibration.densityRatio;
}

float pulsesToFlowLps(uint32_t pulseCount, uint32_t elapsedMicros) {
  if (elapsedMicros == 0) return 0.0f;
  float frequency = static_cast<float>(pulseCount) / (static_cast<float>(elapsedMicros) / 1e6f);
  return frequency / 12.0f;
}

// -----------------------------------------------------------------------------
// Event logging state
struct EventSnapshot {
  bool active = false;
  uint64_t endTimestampMs = 0;
  File file;
};

EventSnapshot eventSnapshot;

// -----------------------------------------------------------------------------
// Utility functions
float readJoystickAxis(gpio_num_t pin) {
  int raw = analogRead(pin);
  float normalized = (static_cast<float>(raw) / 4095.0f) * 2.0f - 1.0f;
  if (fabs(normalized) < Config::JOYSTICK_DEADBAND) {
    return 0.0f;
  }
  return clampValue(normalized, -1.0f, 1.0f);
}

float applyAcceleration(float value) {
  if (fabs(value) > Config::JOYSTICK_ACCEL_THRESHOLD) {
    return value * Config::JOYSTICK_ACCEL_FACTOR;
  }
  return value;
}

void ensureDirectories() {
  if (!sdReady) return;
  if (!sd.exists(Config::LOG_DIRECTORY)) {
    sd.mkdir(Config::LOG_DIRECTORY);
  }
  if (!sd.exists(Config::EVENT_DIRECTORY)) {
    sd.mkdir(Config::EVENT_DIRECTORY);
  }
}

uint64_t freeSpaceBytes() {
  if (!sdReady) return 0;
  if (!sd.card() || !sd.vol()) return 0;
  if (!sd.card()->isBusy()) {
    uint32_t clusters = sd.vol()->freeClusterCount();
    uint32_t blocksPerCluster = sd.vol()->blocksPerCluster();
    return static_cast<uint64_t>(clusters) * blocksPerCluster * 512ULL;
  }
  return 0;
}

String currentDateString(const tm &timeinfo) {
  char buffer[11];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d", &timeinfo);
  return String(buffer);
}

String currentIso8601(const tm &timeinfo) {
  char buffer[25];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &timeinfo);
  return String(buffer);
}

String monthNameTr(int month) {
  static const char *months[] = {"", "Ocak", "Şubat", "Mart", "Nisan", "Mayıs", "Haziran", "Temmuz", "Ağustos", "Eylül", "Ekim", "Kasım", "Aralık"};
  if (month < 1 || month > 12) return "";
  return String(months[month]);
}

void setSystemTime(const tm &timeinfo) {
  time_t t = mktime(const_cast<tm *>(&timeinfo));
  struct timeval now = { .tv_sec = static_cast<time_t>(t), .tv_usec = 0 };
  settimeofday(&now, nullptr);
}

// -----------------------------------------------------------------------------
// SD logging helpers
void writeCsvLine(File &file, const LogRecord &record) {
  const SensorSample &s = record.sample;
  file.printf("%llu,%s,%lu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n", record.unixTime,
              record.isoTimestamp, static_cast<unsigned long>(s.rawPulseCount), s.rawVoltage,
              s.flow.instantaneousLps, s.flow.baselineLps, s.flow.diffPercent, s.flow.minHealthyLps,
              s.flow.meanLps, s.flow.medianLps, s.level.instantaneousCm, s.level.thetaCm,
              s.level.sigmaCm, s.level.diffPercent, s.level.noisePercent);
}

File openDailyLog(const tm &timeinfo) {
  String filename = String(Config::LOG_DIRECTORY) + "/" + currentDateString(timeinfo) + ".csv";
  File file = sd.open(filename.c_str(), O_RDWR | O_CREAT | O_AT_END);
  if (!file) {
    DEBUG_PRINTLN("Failed to open daily log file");
    return file;
  }
  if (file.size() == 0) {
    file.println("unix_ts,iso8601,pulses,level_voltage,flow_lps,flow_baseline,flow_diff_percent,flow_min_healthy,flow_mean,flow_median,level_cm,level_theta,level_sigma,level_diff_percent,level_noise_percent");
  }
  return file;
}

void startEventSnapshot(const LogRecord &latest) {
  if (!sdReady) return;
  if (eventSnapshot.active) return;
  time_t now = time(nullptr);
  tm timeinfo;
  localtime_r(&now, &timeinfo);
  char filename[64];
  strftime(filename, sizeof(filename), "/events/event_%Y-%m-%dT%H-%M-%S.csv", &timeinfo);
  eventSnapshot.file = sd.open(filename, O_RDWR | O_CREAT | O_TRUNC);
  if (!eventSnapshot.file) {
    DEBUG_PRINTLN("Failed to open event file");
    return;
  }
  eventSnapshot.file.println("unix_ts,iso8601,pulses,level_voltage,flow_lps,flow_baseline,flow_diff_percent,flow_min_healthy,flow_mean,flow_median,level_cm,level_theta,level_sigma,level_diff_percent,level_noise_percent");
  logBuffer.forEach([&](const LogRecord &rec) { writeCsvLine(eventSnapshot.file, rec); });
  writeCsvLine(eventSnapshot.file, latest);
  eventSnapshot.active = true;
  eventSnapshot.endTimestampMs = latest.sample.timestampMs + 60ULL * 60ULL * 1000ULL;
}

void stopEventSnapshot() {
  if (eventSnapshot.active) {
    eventSnapshot.file.close();
    eventSnapshot.active = false;
  }
}

void maintainStorage() {
  if (!sdReady) return;
  uint64_t freeBytes = freeSpaceBytes();
  if (freeBytes == 0) return;
  while (freeBytes < Config::SD_FREE_SPACE_THRESHOLD_BYTES) {
    File dir = sd.open(Config::LOG_DIRECTORY);
    if (!dir) break;
    time_t oldestTime = LONG_MAX;
    String oldestName;
    for (File entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      if (!entry.isDirectory()) {
        time_t created = entry.getCreationTime();
        if (created < oldestTime) {
          oldestTime = created;
          oldestName = entry.name();
        }
      }
      entry.close();
    }
    dir.close();
    if (oldestName.length() > 0) {
      String path = String(Config::LOG_DIRECTORY) + "/" + oldestName;
      sd.remove(path.c_str());
      freeBytes = freeSpaceBytes();
    } else {
      break;
    }
  }
}

// -----------------------------------------------------------------------------
// Sensor task
void sensorTask(void *param) {
  FlowAnalytics flowAnalytics;
  LevelAnalytics levelAnalytics;
  SensorSample latestSample;
  uint32_t lastPulseCount = 0;
  uint32_t lastMicros = micros();
  uint64_t lastLogMs = millis();
  std::array<float, 20> noiseBuffer{};
  size_t noiseIndex = 0;
  size_t noiseCount = 0;
  uint64_t lastLevelSampleMs = 0;

  TickType_t lastWake = xTaskGetTickCount();
  while (true) {
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(Config::FLOW_SAMPLE_PERIOD_MS));

    uint32_t currentCount;
    uint32_t lastPulseTime;
    bool signalPresent;
    portENTER_CRITICAL(&portMUX_GLOBAL);
    currentCount = g_pulseCount;
    lastPulseTime = g_lastPulseMicros;
    signalPresent = g_flowSignalPresent;
    g_flowSignalPresent = false;
    portEXIT_CRITICAL(&portMUX_GLOBAL);

    uint32_t pulseDelta = currentCount - lastPulseCount;
    uint32_t nowMicros = micros();
    uint32_t elapsedMicros = nowMicros - lastMicros;
    if (elapsedMicros == 0) elapsedMicros = 1;
    float flowLps = pulsesToFlowLps(pulseDelta, elapsedMicros);
    lastPulseCount = currentCount;
    lastMicros = nowMicros;

    // Level sensor sampling
    uint64_t nowMs = millis();
    if ((nowMs - lastLevelSampleMs) >= Config::LEVEL_SAMPLE_PERIOD_MS) {
      std::array<int, Config::ADC_OVERSAMPLE> samples{};
      for (uint8_t i = 0; i < Config::ADC_OVERSAMPLE; ++i) {
        samples[i] = analogRead(Config::LEVEL_SENSOR_PIN);
        delayMicroseconds(200);
      }
      std::sort(samples.begin(), samples.end());
      size_t usableCount = (samples.size() > 4) ? (samples.size() - 4) : samples.size();
      int sum = 0;
      size_t start = (samples.size() > 4) ? 2 : 0;
      size_t end = (samples.size() > 4) ? samples.size() - 2 : samples.size();
      for (size_t i = start; i < end; ++i) {
        sum += samples[i];
      }
      float average = static_cast<float>(sum) / static_cast<float>(usableCount);
      float voltage = average * (3.3f / 4095.0f);
      latestSample.rawVoltage = voltage;
      float levelCm = voltageToHeightCm(voltage);
      emaLevel_ = emaLevel_ + Config::EMA_ALPHA * (levelCm - emaLevel_);
      noiseBuffer[noiseIndex] = levelCm;
      noiseIndex = (noiseIndex + 1) % noiseBuffer.size();
      if (noiseCount < noiseBuffer.size()) noiseCount++;
      float noiseMean = 0.0f;
      for (size_t i = 0; i < noiseCount; ++i) {
        noiseMean += noiseBuffer[i];
      }
      noiseMean /= (noiseCount > 0) ? noiseCount : 1;
      float noiseVar = 0.0f;
      for (size_t i = 0; i < noiseCount; ++i) {
        float diff = noiseBuffer[i] - noiseMean;
        noiseVar += diff * diff;
      }
      noiseVar /= (noiseCount > 1) ? (noiseCount - 1) : 1;
      float noisePercent = (noiseMean > 0.001f) ? (sqrtf(noiseVar) / noiseMean) * 100.0f : 0.0f;

      LevelMetrics levelMetrics = levelAnalytics.push(levelCm, emaLevel_, noisePercent);

      FlowMetrics flowMetrics = flowAnalytics.push(flowLps);

      latestSample.timestampMs = nowMs;
      latestSample.flow = flowMetrics;
      latestSample.level = levelMetrics;
      latestSample.rawPulseCount = pulseDelta;
      latestSample.pumpRunning = flowAnalytics.pumpOn();
      lastLevelSampleMs = nowMs;

      if (sensorQueue) {
        SensorSample copy = latestSample;
        xQueueOverwrite(sensorQueue, &copy);
      }

      if (millis() - lastLogMs >= Config::LOG_INTERVAL_MS) {
        lastLogMs = millis();
        time_t now = time(nullptr);
        tm timeinfo;
        localtime_r(&now, &timeinfo);
        LogRecord record;
        record.sample = latestSample;
        record.unixTime = static_cast<uint64_t>(now);
        strftime(record.isoTimestamp, sizeof(record.isoTimestamp), "%Y-%m-%dT%H:%M:%S", &timeinfo);
        logBuffer.push(record);
        if (logMutex && xSemaphoreTake(logMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
          if (sdReady) {
            if (!logFile) {
              logFile = openDailyLog(timeinfo);
            }
            if (logFile) {
              writeCsvLine(logFile, record);
              logFile.flush();
            }
            if (eventSnapshot.active) {
              if (latestSample.timestampMs <= eventSnapshot.endTimestampMs) {
                writeCsvLine(eventSnapshot.file, record);
                eventSnapshot.file.flush();
              } else {
                stopEventSnapshot();
              }
            }
            maintainStorage();
          }
          xSemaphoreGive(logMutex);
        }
        uint32_t pulseGap = nowMicros - lastPulseTime;
        if (!signalPresent && pulseGap > 500000 && (millis() - latestSample.timestampMs) > 2000) {
          DEBUG_PRINTLN("Warning: No flow pulses detected");
        }
      }

#if DEBUG_PROJECT_KALKAN
      DEBUG_PRINTF("Flow %.3f L/s, Baseline %.3f L/s, Level %.2f cm\n", latestSample.flow.instantaneousLps,
                   latestSample.flow.baselineLps, latestSample.level.instantaneousCm);
#endif
    }
  }
}

// -----------------------------------------------------------------------------
// UI state management

enum class UiScreen { Boot, SetTime, SetDate, Main, LevelStats, FlowStats, Calibration };

struct TimeEditor {
  int hour = 12;
  int minute = 0;
  int day = 1;
  int month = 1;
  int year = 2025;
  int cursorIndex = 0;
};

struct CalibrationEditor {
  float depthCm = 0.0f;
  int cursorIndex = 0;
};

UiScreen currentScreen = UiScreen::Boot;
TimeEditor timeEditor;
CalibrationEditor calibrationEditor;
bool bootScreenShown = false;
uint64_t bootStartMs = 0;
SensorSample latestSensorSample;
float emaLevel_ = 0.0f;

std::array<String, 6> flowMessages;
std::array<String, 5> levelMessages;
size_t flowMessageIndex = 0;
size_t levelMessageIndex = 0;
uint32_t lastScrollMs = 0;

void updateFlowMessages(const FlowMetrics &flow) {
  char buffer[20];
  snprintf(buffer, sizeof(buffer), "Q=%.2f", flow.instantaneousLps);
  flowMessages[0] = buffer;
  snprintf(buffer, sizeof(buffer), "Q%c=%.2f", 0, flow.meanLps);
  flowMessages[4] = buffer;
  snprintf(buffer, sizeof(buffer), "Q%c=%.2f", 1, flow.medianLps);
  flowMessages[5] = buffer;
  snprintf(buffer, sizeof(buffer), "Qn=%.2f", flow.baselineLps);
  flowMessages[1] = buffer;
  snprintf(buffer, sizeof(buffer), "Qdif=%+.1f%%", flow.diffPercent);
  flowMessages[2] = buffer;
  snprintf(buffer, sizeof(buffer), "Qmin=%.2f", flow.minHealthyLps);
  flowMessages[3] = buffer;
}

void updateLevelMessages(const LevelMetrics &level) {
  char buffer[21];
  snprintf(buffer, sizeof(buffer), "h=%.1f cm", level.instantaneousCm);
  levelMessages[0] = buffer;
  snprintf(buffer, sizeof(buffer), "h%c=%.1f cm", 2, level.thetaCm);
  levelMessages[1] = buffer;
  snprintf(buffer, sizeof(buffer), "h%c=%.1f cm", 3, level.sigmaCm);
  levelMessages[2] = buffer;
  snprintf(buffer, sizeof(buffer), "hdif=%+.1f%%", level.diffPercent);
  levelMessages[3] = buffer;
  snprintf(buffer, sizeof(buffer), "signal:%0.1f%%", level.noisePercent);
  levelMessages[4] = buffer;
}

void lcdWriteRow(uint8_t row, const String &label, const String &value) {
  lcd.setCursor(0, row);
  lcd.print(label);
  lcd.print(":");
  lcd.print(value);
  size_t padding = 0;
  size_t used = label.length() + 1 + value.length();
  if (used < Config::LCD_COLS) {
    padding = Config::LCD_COLS - used;
  }
  for (size_t i = 0; i < padding; ++i) {
    lcd.print(' ');
  }
}

void displayBootScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Project Kalkan");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
}

void displayTimeScreen(bool isDate) {
  lcd.setCursor(0, 0);
  lcd.print(isDate ? "Tarihi Ayarla   " : "Zamanı Ayarla   ");
  lcd.setCursor(0, 1);
  char buffer[20];
  int cursorPos = 0;
  if (!isDate) {
    snprintf(buffer, sizeof(buffer), "%02d:%02d", timeEditor.hour, timeEditor.minute);
    static const int positions[] = {0, 1, 3, 4};
    int idx = clampValue(timeEditor.cursorIndex, 0, 3);
    cursorPos = positions[idx];
  } else {
    String monthStr = monthNameTr(timeEditor.month);
    snprintf(buffer, sizeof(buffer), "%02d %s %04d", timeEditor.day, monthStr.c_str(), timeEditor.year);
    int monthStart = 3;
    int yearStart = monthStart + monthStr.length() + 1;
    int positions[] = {0, monthStart, yearStart};
    int idx = clampValue(timeEditor.cursorIndex, 0, 2);
    cursorPos = positions[idx];
  }
  lcd.print(buffer);
  size_t padding = (Config::LCD_COLS > strlen(buffer)) ? (Config::LCD_COLS - strlen(buffer)) : 0;
  for (size_t i = 0; i < padding; ++i) {
    lcd.print(' ');
  }
  lcd.setCursor(cursorPos, 1);
  lcd.blink();
}

void handleTimeEditing(float joyX, float joyY) {
  bool editingDate = (currentScreen == UiScreen::SetDate);
  float accelY = applyAcceleration(joyY);
  if (!editingDate) {
    if (joyX > 0.5f) {
      if (timeEditor.cursorIndex < 3) {
        timeEditor.cursorIndex++;
      } else {
        currentScreen = UiScreen::SetDate;
        timeEditor.cursorIndex = 0;
        lcd.clear();
      }
      vTaskDelay(pdMS_TO_TICKS(150));
    } else if (joyX < -0.5f) {
      timeEditor.cursorIndex = std::max(0, timeEditor.cursorIndex - 1);
      vTaskDelay(pdMS_TO_TICKS(150));
    }
    if (accelY > 0.2f) {
      if (timeEditor.cursorIndex < 2) {
        timeEditor.hour = (timeEditor.hour + 1) % 24;
      } else {
        timeEditor.minute = (timeEditor.minute + 1) % 60;
      }
    } else if (accelY < -0.2f) {
      if (timeEditor.cursorIndex < 2) {
        timeEditor.hour = (timeEditor.hour + 23) % 24;
      } else {
        timeEditor.minute = (timeEditor.minute + 59) % 60;
      }
    }
  } else {
    const int maxIndex = 2;
    if (joyX > 0.5f) {
      if (timeEditor.cursorIndex < maxIndex) {
        timeEditor.cursorIndex++;
      } else {
        currentScreen = UiScreen::Main;
        lcd.noBlink();
        tm t = {};
        t.tm_year = timeEditor.year - 1900;
        t.tm_mon = timeEditor.month - 1;
        t.tm_mday = timeEditor.day;
        t.tm_hour = timeEditor.hour;
        t.tm_min = timeEditor.minute;
        setSystemTime(t);
        lcd.clear();
        return;
      }
      vTaskDelay(pdMS_TO_TICKS(150));
    } else if (joyX < -0.5f) {
      timeEditor.cursorIndex = std::max(0, timeEditor.cursorIndex - 1);
      vTaskDelay(pdMS_TO_TICKS(150));
    }
    if (accelY > 0.2f) {
      switch (timeEditor.cursorIndex) {
        case 0:
          timeEditor.day = std::min(31, timeEditor.day + 1);
          break;
        case 1:
          timeEditor.month = (timeEditor.month % 12) + 1;
          break;
        case 2:
          timeEditor.year++;
          break;
      }
    } else if (accelY < -0.2f) {
      switch (timeEditor.cursorIndex) {
        case 0:
          timeEditor.day = std::max(1, timeEditor.day - 1);
          break;
        case 1:
          timeEditor.month = (timeEditor.month == 1) ? 12 : timeEditor.month - 1;
          break;
        case 2:
          timeEditor.year = std::max(2020, timeEditor.year - 1);
          break;
      }
    }
  }
}

void displayMainScreen(const SensorSample &sample) {
  updateFlowMessages(sample.flow);
  updateLevelMessages(sample.level);
  uint32_t now = millis();
  if (now - lastScrollMs > 2000) {
    flowMessageIndex = (flowMessageIndex + 1) % flowMessages.size();
    levelMessageIndex = (levelMessageIndex + 1) % levelMessages.size();
    lastScrollMs = now;
  }
  lcdWriteRow(0, "FLOW", flowMessages[flowMessageIndex]);
  lcdWriteRow(1, "TANK", levelMessages[levelMessageIndex]);
}

void displayStatsScreen(const SensorSample &sample, bool flowStats) {
  char line0[17];
  char line1[17];
  if (flowStats) {
    snprintf(line0, sizeof(line0), "Qmn%.2f Qmx%.2f", sample.flow.minObservedLps, sample.flow.maxObservedLps);
    snprintf(line1, sizeof(line1), "%c%.2f %c%.2f s%.2f", char(0), sample.flow.meanLps, char(1),
             sample.flow.medianLps, sample.flow.stddevLps);
  } else {
    snprintf(line0, sizeof(line0), "h mn%.1f mx%.1f", sample.level.minCm, sample.level.maxCm);
    snprintf(line1, sizeof(line1), "%c%.1f %c%.1f s%.1f", char(0), sample.level.meanCm, char(1),
             sample.level.medianCm, sample.level.stddevCm);
  }
  lcd.setCursor(0, 0);
  lcd.print(line0);
  size_t padding0 = (Config::LCD_COLS > strlen(line0)) ? (Config::LCD_COLS - strlen(line0)) : 0;
  for (size_t i = 0; i < padding0; ++i) {
    lcd.print(' ');
  }
  lcd.setCursor(0, 1);
  lcd.print(line1);
  size_t padding1 = (Config::LCD_COLS > strlen(line1)) ? (Config::LCD_COLS - strlen(line1)) : 0;
  for (size_t i = 0; i < padding1; ++i) {
    lcd.print(' ');
  }
}

void displayCalibrationScreen() {
  lcd.setCursor(0, 0);
  lcd.print("Kalibrasyon h(cm)");
  lcd.setCursor(0, 1);
  char buffer[17];
  snprintf(buffer, sizeof(buffer), "%.1f cm", calibrationEditor.depthCm);
  lcd.print(buffer);
  size_t padding = (Config::LCD_COLS > strlen(buffer)) ? (Config::LCD_COLS - strlen(buffer)) : 0;
  for (size_t i = 0; i < padding; ++i) {
    lcd.print(' ');
  }
  lcd.setCursor(calibrationEditor.cursorIndex, 1);
  lcd.blink();
}

void handleCalibrationEditing(float joyX, float joyY) {
  if (joyX > 0.5f) {
    calibrationEditor.cursorIndex++;
    vTaskDelay(pdMS_TO_TICKS(150));
  } else if (joyX < -0.5f && calibrationEditor.cursorIndex > 0) {
    calibrationEditor.cursorIndex--;
    vTaskDelay(pdMS_TO_TICKS(150));
  }
  calibrationEditor.cursorIndex = clampValue(calibrationEditor.cursorIndex, 0, Config::LCD_COLS - 1);
  float accel = applyAcceleration(joyY);
  if (accel > 0.2f) {
    calibrationEditor.depthCm += 0.5f;
  } else if (accel < -0.2f) {
    calibrationEditor.depthCm = std::max(0.0f, calibrationEditor.depthCm - 0.5f);
  }
}

void applyCalibration(const SensorSample &sample) {
  float rawDepth = sample.level.instantaneousCm / std::max(0.0001f, calibration.densityRatio);
  if (rawDepth > 0.1f) {
    calibration.densityRatio = calibrationEditor.depthCm / rawDepth;
  }
}

void uiTask(void *param) {
  displayBootScreen();
  bootStartMs = millis();
  bootScreenShown = true;
  uint64_t bothPressedStart = 0;

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(50));
    button1.update();
    button2.update();

    if (button1.fell()) {
      if (sdReady && logMutex && xSemaphoreTake(logMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        time_t now = time(nullptr);
        tm timeinfo;
        localtime_r(&now, &timeinfo);
        LogRecord record;
        record.sample = latestSensorSample;
        record.unixTime = now;
        strftime(record.isoTimestamp, sizeof(record.isoTimestamp), "%Y-%m-%dT%H:%M:%S", &timeinfo);
        startEventSnapshot(record);
        xSemaphoreGive(logMutex);
      }
    }

    if (button1.read() == LOW && button2.read() == LOW) {
      if (bothPressedStart == 0) {
        bothPressedStart = millis();
      } else if (millis() - bothPressedStart > 5000 && currentScreen != UiScreen::Calibration) {
        currentScreen = UiScreen::Calibration;
        calibrationEditor.depthCm = latestSensorSample.level.instantaneousCm;
        calibrationEditor.cursorIndex = 0;
        lcd.clear();
        lcd.blink();
      }
    } else {
      bothPressedStart = 0;
    }

    SensorSample sample;
    if (sensorQueue && xQueuePeek(sensorQueue, &sample, 0) == pdTRUE) {
      latestSensorSample = sample;
    }

    float joyX = readJoystickAxis(Config::JOYSTICK_X_PIN);
    float joyY = readJoystickAxis(Config::JOYSTICK_Y_PIN);

    switch (currentScreen) {
      case UiScreen::Boot:
        if (millis() - bootStartMs > 5000) {
          currentScreen = UiScreen::SetTime;
          lcd.clear();
        }
        break;
      case UiScreen::SetTime:
        displayTimeScreen(false);
        handleTimeEditing(joyX, joyY);
        break;
      case UiScreen::SetDate:
        displayTimeScreen(true);
        handleTimeEditing(joyX, joyY);
        break;
      case UiScreen::Main:
        lcd.noBlink();
        displayMainScreen(latestSensorSample);
        if (joyX > 0.8f) {
          currentScreen = UiScreen::LevelStats;
          lcd.clear();
        }
        break;
      case UiScreen::LevelStats:
        displayStatsScreen(latestSensorSample, false);
        if (joyX > 0.8f) {
          currentScreen = UiScreen::FlowStats;
          lcd.clear();
        } else if (joyX < -0.8f) {
          currentScreen = UiScreen::Main;
          lcd.clear();
        }
        break;
      case UiScreen::FlowStats:
        displayStatsScreen(latestSensorSample, true);
        if (joyX < -0.8f) {
          currentScreen = UiScreen::LevelStats;
          lcd.clear();
        }
        break;
      case UiScreen::Calibration:
        displayCalibrationScreen();
        handleCalibrationEditing(joyX, joyY);
        if (button1.rose() || button2.rose()) {
          applyCalibration(latestSensorSample);
          currentScreen = UiScreen::Main;
          lcd.noBlink();
        }
        break;
    }
  }
}

// -----------------------------------------------------------------------------
// Setup and loop

void configureCustomGlyphs() {
  uint8_t mu[8] = {0b00000, 0b00000, 0b10001, 0b10001, 0b10001, 0b10011, 0b10101, 0b10101};
  uint8_t eta[8] = {0b00000, 0b10000, 0b10000, 0b11110, 0b10001, 0b10001, 0b10001, 0b00000};
  uint8_t theta[8] = {0b00000, 0b01110, 0b10001, 0b11111, 0b11111, 0b10001, 0b01110, 0b00000};
  uint8_t sigma[8] = {0b00000, 0b11111, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111, 0b00000};
  lcd.createChar(0, mu);
  lcd.createChar(1, eta);
  lcd.createChar(2, theta);
  lcd.createChar(3, sigma);
}

void setupButtons() {
  pinMode(Config::BUTTON1_PIN, INPUT_PULLUP);
  pinMode(Config::BUTTON2_PIN, INPUT_PULLUP);
  button1.attach(Config::BUTTON1_PIN);
  button1.interval(25);
  button2.attach(Config::BUTTON2_PIN);
  button2.interval(25);
}

void setupJoystick() {
  analogSetPinAttenuation(Config::JOYSTICK_X_PIN, ADC_11db);
  analogSetPinAttenuation(Config::JOYSTICK_Y_PIN, ADC_11db);
}

void setupLevelSensor() {
  analogSetPinAttenuation(Config::LEVEL_SENSOR_PIN, ADC_11db);
  analogReadResolution(12);
}

void setupFlowSensor() {
  pinMode(Config::FLOW_SENSOR_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(Config::FLOW_SENSOR_PIN), flowISR, RISING);
}

void setupLogging() {
  spi.begin(18, 19, 23, 5);
  sdReady = sd.begin(spi, 5, SD_SCK_MHZ(18));
  if (!sdReady) {
    lcd.setCursor(0, 1);
    lcd.print("SD hata!");
    return;
  }
  ensureDirectories();
}

void setupTime() {
  configTime(0, 0, "pool.ntp.org");  // placeholder; user will set manually
}

void setupTasks() {
  sensorQueue = xQueueCreate(1, sizeof(SensorSample));
  logMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(sensorTask, "sensorTask", 8192, nullptr, 2, nullptr, 0);
  xTaskCreatePinnedToCore(uiTask, "uiTask", 8192, nullptr, 1, nullptr, 1);
}

void setup() {
  Serial.begin(115200);
  Wire.begin();
  lcd.init();
  lcd.backlight();
  configureCustomGlyphs();
  setupButtons();
  setupJoystick();
  setupLevelSensor();
  setupFlowSensor();
  setupLogging();
  setupTime();
  setupTasks();
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}

