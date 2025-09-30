#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <time.h>

#include <../lib/Buttons/Buttons.h>
#include <../lib/ConfigService/ConfigService.h>
#include <../lib/FlowSensor/FlowSensor.h>
#include <../lib/Joystick/Joystick.h>
#include <../lib/LcdUI/LcdUI.h>
#include <../lib/LevelSensor/LevelSensor.h>
#include <../lib/SdLogger/SdLogger.h>
#include <../lib/Utils/Utils.h>
#include <LiquidCrystal_I2C.h>

// ---- Hardware Pin Map ----
static const int PIN_FLOW_SENSOR = 25;
static const int PIN_LEVEL_SENSOR = 36;  // ADC1 channel
static const int PIN_JOYSTICK_X = 32;
static const int PIN_JOYSTICK_Y = 33;
static const int PIN_BUTTON_1 = 14;
static const int PIN_BUTTON_2 = 27;
static const int PIN_LCD_SDA = 21;
static const int PIN_LCD_SCL = 22;
static const int PIN_SD_CS = 5;
static const int PIN_SD_MOSI = 23;
static const int PIN_SD_MISO = 19;
static const int PIN_SD_SCK = 18;

static const uint8_t LCD_ADDRESS = 0x27;

// ---- Global Objects ----
FlowSensor g_flowSensor;
LevelSensor g_levelSensor;
Buttons g_buttons;
Joystick g_joystick;
ConfigService g_config;
SdLogger g_logger;
LiquidCrystal_I2C g_lcd(LCD_ADDRESS, 16, 2);
LcdUI g_ui;
SPIClass g_spi(VSPI);

QueueHandle_t g_loggerQueue = nullptr;

portMUX_TYPE g_metricsMux = portMUX_INITIALIZER_UNLOCKED;
utils::SensorMetrics g_latestMetrics;
volatile bool g_metricsAvailable = false;

TaskHandle_t g_sensorTaskHandle = nullptr;
TaskHandle_t g_uiTaskHandle = nullptr;
TaskHandle_t g_loggerTaskHandle = nullptr;

void sensorTask(void* parameter);
void uiTask(void* parameter);
void loggerTask(void* parameter);

void applyCalibration(float actualDepthCm) {
    portENTER_CRITICAL(&g_metricsMux);
    float currentDepth = g_latestMetrics.tankHeightCm;
    portEXIT_CRITICAL(&g_metricsMux);
    if (actualDepthCm <= 0.0f || currentDepth <= 0.0f) {
        return;
    }
    float currentDensity = g_config.densityFactor();
    float newDensity = currentDensity * (currentDepth / actualDepthCm);
    g_config.setDensityFactor(newDensity);
    g_levelSensor.setDensityFactor(newDensity);
}

void setup() {
#ifdef PROJECT_KALKAN_DEBUG
    Serial.begin(115200);
    delay(200);
    Serial.println("Project Kalkan debug mode");
#endif

    g_config.begin();

    Wire.begin(PIN_LCD_SDA, PIN_LCD_SCL);

    g_flowSensor.begin(PIN_FLOW_SENSOR);
    g_levelSensor.begin(PIN_LEVEL_SENSOR, ADC_11db);
    g_levelSensor.setOversample(g_config.levelOversampleCount());
    g_levelSensor.setDensityFactor(g_config.densityFactor());

    g_buttons.begin(PIN_BUTTON_1, PIN_BUTTON_2);
    g_joystick.begin(PIN_JOYSTICK_X, PIN_JOYSTICK_Y, 0.08f);

    g_spi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    g_logger.begin(PIN_SD_CS, g_spi, &g_config);

    g_ui.begin(&g_lcd, &g_buttons, &g_joystick, &g_logger, &g_config);
    g_ui.setCalibrationCallback(applyCalibration);

    g_loggerQueue = xQueueCreate(12, sizeof(utils::SensorMetrics));

    xTaskCreatePinnedToCore(sensorTask, "sensor", 8192, nullptr, 3, &g_sensorTaskHandle, 0);
    xTaskCreatePinnedToCore(uiTask, "ui", 8192, nullptr, 2, &g_uiTaskHandle, 1);
    xTaskCreatePinnedToCore(loggerTask, "logger", 6144, nullptr, 1, &g_loggerTaskHandle, 0);
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}

void sensorTask(void* parameter) {
    utils::FlowAnalytics flowAnalytics;
    utils::LevelAnalytics levelAnalytics;
    TickType_t lastWake = xTaskGetTickCount();
    const TickType_t intervalTicks = pdMS_TO_TICKS(g_config.sensorIntervalMs());
    uint32_t previousCount = g_flowSensor.takeSnapshot().totalPulses;

    while (true) {
        vTaskDelayUntil(&lastWake, intervalTicks);

        FlowSensor::Snapshot snapshot = g_flowSensor.takeSnapshot();
        uint32_t deltaPulses = snapshot.totalPulses - previousCount;
        previousCount = snapshot.totalPulses;
        float intervalSeconds = static_cast<float>(g_config.sensorIntervalMs()) / 1000.0f;
        float flowLps = utils::pulsesToFlowLps(deltaPulses, intervalSeconds);

        utils::FlowAnalyticsResult flowResult = flowAnalytics.update(flowLps);

        utils::LevelReading levelReading = g_levelSensor.sample();
        utils::LevelAnalyticsResult levelResult = levelAnalytics.update(levelReading.heightCm, levelReading.noisePercent);

        utils::SensorMetrics metrics;
        metrics.timestamp = time(nullptr);
        metrics.pulseCount = deltaPulses;
        metrics.pulseIntervalSeconds = intervalSeconds;
        metrics.flowLps = flowLps;
        metrics.flowBaselineLps = flowResult.baselineLps;
        metrics.flowDiffPercent = (!isnan(flowResult.baselineLps) && flowResult.baselineLps > 0.0f)
                                      ? ((flowLps - flowResult.baselineLps) / flowResult.baselineLps) * 100.0f
                                      : NAN;
        metrics.flowMinHealthyLps = flowResult.minHealthyLps;
        metrics.flowMeanLps = flowResult.meanLps;
        metrics.flowMedianLps = flowResult.medianLps;
        metrics.flowStdDevLps = flowResult.stdDevLps;
        metrics.flowMinLps = flowResult.minLps;
        metrics.flowMaxLps = flowResult.maxLps;
        metrics.pumpOn = flowResult.pumpOn;

        metrics.tankHeightCm = levelReading.heightCm;
        metrics.tankEmptyEstimateCm = levelResult.emptyEstimateCm;
        metrics.tankFullEstimateCm = levelResult.fullEstimateCm;
        if (!isnan(metrics.tankFullEstimateCm) && metrics.tankFullEstimateCm > 0.0f) {
            metrics.tankDiffPercent = ((metrics.tankHeightCm - metrics.tankFullEstimateCm) / metrics.tankFullEstimateCm) * 100.0f;
        }
        metrics.tankNoisePercent = levelReading.noisePercent;
        metrics.tankMeanCm = levelResult.meanCm;
        metrics.tankMedianCm = levelResult.medianCm;
        metrics.tankStdDevCm = levelResult.stdDevCm;
        metrics.tankMinObservedCm = levelResult.minCm;
        metrics.tankMaxObservedCm = levelResult.maxCm;
        metrics.levelVoltage = levelReading.averageVoltage;
        metrics.emaVoltage = levelReading.emaVoltage;
        metrics.densityFactor = g_config.densityFactor();

        portENTER_CRITICAL(&g_metricsMux);
        g_latestMetrics = metrics;
        g_metricsAvailable = true;
        portEXIT_CRITICAL(&g_metricsMux);

        if (g_loggerQueue) {
            xQueueSend(g_loggerQueue, &metrics, 0);
        }

#ifdef PROJECT_KALKAN_DEBUG
        Serial.print("Flow L/s: ");
        Serial.print(metrics.flowLps, 3);
        Serial.print(" | Tank cm: ");
        Serial.print(metrics.tankHeightCm, 2);
        Serial.print(" | Noise %: ");
        Serial.println(metrics.tankNoisePercent, 2);
#endif
    }
}

void uiTask(void* parameter) {
    while (true) {
        utils::SensorMetrics metrics;
        bool hasMetrics = false;
        portENTER_CRITICAL(&g_metricsMux);
        if (g_metricsAvailable) {
            metrics = g_latestMetrics;
            hasMetrics = true;
        }
        portEXIT_CRITICAL(&g_metricsMux);

        if (hasMetrics) {
            g_ui.setMetrics(metrics);
        }
        g_ui.update();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void loggerTask(void* parameter) {
    utils::SensorMetrics metrics;
    while (true) {
        if (g_loggerQueue && xQueueReceive(g_loggerQueue, &metrics, pdMS_TO_TICKS(1000)) == pdTRUE) {
            g_logger.log(metrics);
        }
        g_logger.update();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

