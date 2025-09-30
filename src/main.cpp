#include <Arduino.h>
#include <Bounce2.h>
#include <LiquidCrystal_I2C.h>
#include <SdFat.h>
#include <SPI.h>
#include <algorithm>
#include <array>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sys/time.h>
#include <time.h>

#include "Config.h"
#include "Conversions.h"
#include "LoggingManager.h"
#include "RunningStatistics.h"
#include "SensorData.h"
#include "UIManager.h"

// Uncomment to enable verbose debug serial output.
//#define DEBUG_PROJECT_KALKAN

#ifdef DEBUG_PROJECT_KALKAN
static constexpr bool kDebug = true;
#else
static constexpr bool kDebug = false;
#endif

namespace {

LiquidCrystal_I2C lcd(config::LCD_ADDR, config::LCD_COLS, config::LCD_ROWS);
UIManager ui(lcd);
SdFat sd;
LoggingManager logger(sd);

QueueHandle_t sensorQueue = nullptr;
QueueHandle_t commandQueue = nullptr;
SemaphoreHandle_t calibrationMutex = nullptr;

CalibrationFactors calibrationFactors;

volatile uint32_t gPulseCount = 0;
volatile uint32_t gLastPulseMicros = 0;
volatile uint32_t gPrevPulseMicros = 0;

struct SensorMessage {
  SensorSnapshot snapshot;
  AnalyticsState analytics;
};

enum class UICommandType : uint8_t {
  TriggerEvent,
  SetCalibration,
};

struct UICommand {
  UICommandType type;
  float value;
};

void IRAM_ATTR onFlowPulse() {
  uint32_t now = micros();
  gPulseCount++;
  gPrevPulseMicros = gLastPulseMicros;
  gLastPulseMicros = now;
}

float readLevelVoltage() {
  std::array<uint16_t, config::ANALOG_OVERSAMPLE> samples{};
  for (size_t i = 0; i < samples.size(); ++i) {
    samples[i] = analogRead(config::LEVEL_SENSOR_PIN);
    delayMicroseconds(200);
  }
  std::array<uint16_t, config::ANALOG_OVERSAMPLE> sorted = samples;
  std::sort(sorted.begin(), sorted.end());
  uint32_t sum = 0;
  for (size_t i = 1; i + 1 < sorted.size(); ++i) {
    sum += sorted[i];
  }
  float average = static_cast<float>(sum) / static_cast<float>(sorted.size() - 2);
  float voltage = (average / 4095.0f) * 3.3f;
  return voltage;
}

String iso8601FromTime(time_t now) {
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  char buffer[32];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(buffer);
}

void processUICommand(const UICommand& command, const LogRecord& latestRecord) {
  switch (command.type) {
    case UICommandType::TriggerEvent:
      logger.triggerEvent(latestRecord);
      break;
    case UICommandType::SetCalibration: {
      CalibrationFactors copy = calibrationFactors;
      copy.referenceHeightCm = command.value;
      if (xSemaphoreTake(calibrationMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        calibrationFactors = copy;
        xSemaphoreGive(calibrationMutex);
      }
      break;
    }
  }
}

float readJoystickAxis(gpio_num_t pin) {
  int raw = analogRead(pin);
  int centered = raw - config::JOYSTICK_MAX / 2;
  if (abs(centered) < config::JOYSTICK_DEADBAND) {
    return 0.0f;
  }
  float normalized = static_cast<float>(centered) / (config::JOYSTICK_MAX / 2.0f);
  normalized = std::max(-1.0f, std::min(1.0f, normalized));
  return normalized;
}

void configureAdc() {
  analogReadResolution(12);
  analogSetPinAttenuation(config::LEVEL_SENSOR_PIN, ADC_11db);
  analogSetPinAttenuation(config::JOYSTICK_X_PIN, ADC_11db);
  analogSetPinAttenuation(config::JOYSTICK_Y_PIN, ADC_11db);
}

}  // namespace

void sensorTask(void* pvParameters) {
  configureAdc();

  RunningStatistics<config::FLOW_STATS_WINDOW> flowStats;
  RunningStatistics<config::LEVEL_STATS_WINDOW> levelStats;
  float emaVoltage = 0.0f;
  bool emaInitialized = false;

  uint32_t lastPulseCount = 0;
  uint32_t windowPulseCount = 0;
  uint32_t windowStartMs = millis();
  LogRecord latestRecord{};

  logger.begin();

  for (;;) {
    uint32_t loopStartMs = millis();

    uint32_t currentPulseCount;
    taskENTER_CRITICAL();
    currentPulseCount = gPulseCount;
    taskEXIT_CRITICAL();

    uint32_t delta = currentPulseCount - lastPulseCount;
    lastPulseCount = currentPulseCount;
    windowPulseCount += delta;
    uint32_t elapsedWindowMs = loopStartMs - windowStartMs;
    float frequencyHz = 0.0f;
    if (elapsedWindowMs >= config::FLOW_WINDOW_MS) {
      frequencyHz = (windowPulseCount * 1000.0f) / static_cast<float>(elapsedWindowMs);
      windowPulseCount = 0;
      windowStartMs = loopStartMs;
    }

    float flowLps = convertPulseToFlowLps(frequencyHz);
    flowStats.push(flowLps);

    float voltage = readLevelVoltage();
    if (!emaInitialized) {
      emaVoltage = voltage;
      emaInitialized = true;
    } else {
      emaVoltage = emaVoltage + config::ANALOG_ALPHA * (voltage - emaVoltage);
    }

    CalibrationFactors calibrationCopy;
    if (xSemaphoreTake(calibrationMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      calibrationCopy = calibrationFactors;
      xSemaphoreGive(calibrationMutex);
    } else {
      calibrationCopy = calibrationFactors;
    }

    float heightCm = convertVoltageToHeight(emaVoltage, calibrationCopy);
    levelStats.push(heightCm);

    FlowMetrics flowMetrics;
    flowMetrics.instantaneousLps = flowLps;
    flowMetrics.baselineLps = flowStats.percentile(0.9f);
    flowMetrics.minimumHealthyLps = flowStats.percentile(0.1f);
    flowMetrics.meanLps = flowStats.mean();
    flowMetrics.medianLps = flowStats.median();
    if (flowMetrics.baselineLps > 0.01f) {
      flowMetrics.differencePct =
          ((flowMetrics.instantaneousLps - flowMetrics.baselineLps) / flowMetrics.baselineLps) * 100.0f;
    } else {
      flowMetrics.differencePct = 0.0f;
    }

    LevelMetrics levelMetrics;
    levelMetrics.instantaneousCm = heightCm;
    levelMetrics.baselineCm = levelStats.percentile(0.1f);
    levelMetrics.fullTankCm = levelStats.percentile(0.9f);
    if (levelMetrics.fullTankCm > 0.01f) {
      levelMetrics.differencePct =
          ((levelMetrics.instantaneousCm - levelMetrics.fullTankCm) / levelMetrics.fullTankCm) * 100.0f;
    } else {
      levelMetrics.differencePct = 0.0f;
    }
    float meanLevel = levelStats.mean();
    float stdLevel = levelStats.stddev();
    if (meanLevel > 0.01f) {
      levelMetrics.noiseMetric = (stdLevel / meanLevel) * 100.0f;
    } else {
      levelMetrics.noiseMetric = 0.0f;
    }

    AnalyticsState analytics;
    analytics.flow = flowMetrics;
    analytics.level = levelMetrics;
    analytics.flowStats.minValue = flowStats.minimum();
    analytics.flowStats.maxValue = flowStats.maximum();
    analytics.flowStats.meanValue = flowStats.mean();
    analytics.flowStats.medianValue = flowStats.median();
    analytics.flowStats.stddevValue = flowStats.stddev();
    analytics.levelStats.minValue = levelStats.minimum();
    analytics.levelStats.maxValue = levelStats.maximum();
    analytics.levelStats.meanValue = levelStats.mean();
    analytics.levelStats.medianValue = levelStats.median();
    analytics.levelStats.stddevValue = levelStats.stddev();

    SensorSnapshot snapshot;
    snapshot.timestamp = time(nullptr);
    snapshot.pulseCount = currentPulseCount;
    snapshot.pulseFrequencyHz = frequencyHz;
    snapshot.levelVoltage = emaVoltage;
    snapshot.flow = flowMetrics;
    snapshot.level = levelMetrics;

    latestRecord.timestamp = snapshot.timestamp;
    latestRecord.iso8601 = iso8601FromTime(snapshot.timestamp);
    latestRecord.pulseCount = snapshot.pulseCount;
    latestRecord.pulseFrequency = snapshot.pulseFrequencyHz;
    latestRecord.levelVoltage = snapshot.levelVoltage;
    latestRecord.flow = snapshot.flow;
    latestRecord.level = snapshot.level;

    logger.update(snapshot.timestamp, latestRecord);
    logger.loop();

    SensorMessage message{snapshot, analytics};
    xQueueOverwrite(sensorQueue, &message);

    UICommand command;
    while (xQueueReceive(commandQueue, &command, 0) == pdTRUE) {
      processUICommand(command, latestRecord);
    }

    if (kDebug) {
      Serial.print("Flow L/s: ");
      Serial.print(flowLps, 3);
      Serial.print(" Voltage: ");
      Serial.print(voltage, 3);
      Serial.print(" Height: ");
      Serial.print(heightCm, 2);
      Serial.print(" Noise%: ");
      Serial.println(levelMetrics.noiseMetric, 2);
    }

    vTaskDelay(config::SENSOR_TASK_DELAY);
  }
}

void uiTask(void* pvParameters) {
  Bounce button1;
  Bounce button2;
  button1.attach(static_cast<uint8_t>(config::BUTTON1_PIN), INPUT_PULLUP);
  button2.attach(static_cast<uint8_t>(config::BUTTON2_PIN), INPUT_PULLUP);
  button1.interval(config::BUTTON_DEBOUNCE_MS);
  button2.interval(config::BUTTON_DEBOUNCE_MS);

  ui.begin();

  struct tm initialTime = {};
  time_t now;
  time(&now);
  localtime_r(&now, &initialTime);
  if (initialTime.tm_year + 1900 < 2023) {
    initialTime.tm_year = 125;  // 2025 baseline
    initialTime.tm_mon = 6;
    initialTime.tm_mday = 6;
    initialTime.tm_hour = 12;
    initialTime.tm_min = 0;
  }
  ui.setTimeSetting(initialTime);

  SensorMessage lastMessage{};
  bool haveMessage = false;
  bool timeConfigured = false;
  uint32_t bothPressedStart = 0;

  for (;;) {
    SensorMessage message;
    if (xQueueReceive(sensorQueue, &message, 0) == pdTRUE) {
      lastMessage = message;
      haveMessage = true;
    }

    button1.update();
    button2.update();
    bool b1 = !button1.read();
    bool b2 = !button2.read();

    if (b1 && b2) {
      if (bothPressedStart == 0) {
        bothPressedStart = millis();
      } else if (millis() - bothPressedStart >= config::BUTTON_HOLD_MS) {
        if (haveMessage) {
          ui.setCalibrationValue(lastMessage.snapshot.level.instantaneousCm);
        }
        ui.handleButtons(true, true, true);
      }
    } else {
      bothPressedStart = 0;
      ui.handleButtons(b1, b2, false);
    }

    if (button1.fell()) {
      UICommand cmd{UICommandType::TriggerEvent, 0.0f};
      xQueueSend(commandQueue, &cmd, 0);
    }

    if (ui.currentScreen() == UIManager::Screen::Calibration && !b1 && !b2 && ui.calibrationRequested()) {
      UICommand cmd{UICommandType::SetCalibration, ui.calibrationInputValue()};
      xQueueSend(commandQueue, &cmd, 0);
      ui.resetCalibrationRequest();
    }

    float joyX = readJoystickAxis(config::JOYSTICK_X_PIN);
    float joyY = readJoystickAxis(config::JOYSTICK_Y_PIN);
    ui.handleJoystick(joyX, joyY);

    if (haveMessage) {
      ui.update(lastMessage.analytics, lastMessage.snapshot);
    } else {
      SensorSnapshot emptySnapshot;
      AnalyticsState emptyAnalytics;
      ui.update(emptyAnalytics, emptySnapshot);
    }

    if (!timeConfigured && ui.currentScreen() == UIManager::Screen::Main) {
      struct tm edited = ui.editableTime();
      edited.tm_sec = 0;
      edited.tm_isdst = -1;
      time_t newTime = mktime(&edited);
      struct timeval tv;
      tv.tv_sec = newTime;
      tv.tv_usec = 0;
      settimeofday(&tv, nullptr);
      timeConfigured = true;
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void setup() {
  if (kDebug) {
    Serial.begin(115200);
    while (!Serial) {
      delay(10);
    }
  }

  pinMode(config::FLOW_SENSOR_PIN, INPUT);
  pinMode(config::LEVEL_SENSOR_PIN, INPUT);
  pinMode(config::JOYSTICK_X_PIN, INPUT);
  pinMode(config::JOYSTICK_Y_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(config::FLOW_SENSOR_PIN), onFlowPulse, RISING);

  sensorQueue = xQueueCreate(1, sizeof(SensorMessage));
  commandQueue = xQueueCreate(5, sizeof(UICommand));
  calibrationMutex = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(sensorTask, "SensorTask", 8192, nullptr, 2, nullptr, 0);
  xTaskCreatePinnedToCore(uiTask, "UITask", 8192, nullptr, 1, nullptr, 1);
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}

