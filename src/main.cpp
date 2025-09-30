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
#include <algorithm>
#include <cmath>
#include <vector>
#include <LiquidCrystal_I2C.h>

// ---- Hardware Pin Map ----
static const int PIN_FLOW_SENSOR = 25;
static const int PIN_LEVEL_SENSOR = 32;  // ADC1 channel
static const int PIN_JOYSTICK_X = 27;
static const int PIN_JOYSTICK_Y = 26;
static const int PIN_BUTTON_1 = 14;
static const int PIN_BUTTON_2 = 13;
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
    g_levelSensor.setCalibrationCurrent(g_config.zeroCurrentMa(), g_config.fullScaleCurrentMa(),
                                       g_config.fullScaleHeightMm());
    g_levelSensor.setCurrentSense(g_config.currentSenseResistorOhms(), g_config.currentSenseGain());
    g_levelSensor.setFilterGains(g_config.alphaGain(), g_config.betaGain());
    g_levelSensor.setSampleIntervalMs(g_config.sensorIntervalMs());

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
    uint32_t intervalMs = g_config.sensorIntervalMs();
    if (intervalMs < 200) {
        intervalMs = 200;
    }
    TickType_t intervalTicks = pdMS_TO_TICKS(intervalMs);
    g_levelSensor.setSampleIntervalMs(intervalMs);
    g_levelSensor.setCalibrationCurrent(g_config.zeroCurrentMa(), g_config.fullScaleCurrentMa(), g_config.fullScaleHeightMm());
    g_levelSensor.setCurrentSense(g_config.currentSenseResistorOhms(), g_config.currentSenseGain());
    g_levelSensor.setFilterGains(g_config.alphaGain(), g_config.betaGain());
    g_levelSensor.setDensityFactor(g_config.densityFactor());
    FlowSensor::Snapshot initialSnapshot = g_flowSensor.takeSnapshot();
    uint64_t previousCount = initialSnapshot.totalPulses;
    float lastZeroCurrent = g_config.zeroCurrentMa();
    float lastFullCurrent = g_config.fullScaleCurrentMa();
    float lastFullHeightMm = g_config.fullScaleHeightMm();
    float lastSenseResistor = g_config.currentSenseResistorOhms();
    float lastSenseGain = g_config.currentSenseGain();
    float lastAlphaGain = g_config.alphaGain();
    float lastBetaGain = g_config.betaGain();
    float lastDensity = g_config.densityFactor();

    while (true) {
        vTaskDelayUntil(&lastWake, intervalTicks);

        uint32_t desiredInterval = g_config.sensorIntervalMs();
        if (desiredInterval != intervalMs) {
            intervalMs = desiredInterval < 200 ? 200 : desiredInterval;
            intervalTicks = pdMS_TO_TICKS(intervalMs);
            g_levelSensor.setSampleIntervalMs(intervalMs);
        }

        float zeroCurrent = g_config.zeroCurrentMa();
        float fullCurrent = g_config.fullScaleCurrentMa();
        float fullHeightMm = g_config.fullScaleHeightMm();
        if (fabsf(zeroCurrent - lastZeroCurrent) > 0.001f || fabsf(fullCurrent - lastFullCurrent) > 0.001f ||
            fabsf(fullHeightMm - lastFullHeightMm) > 0.1f) {
            g_levelSensor.setCalibrationCurrent(zeroCurrent, fullCurrent, fullHeightMm);
            lastZeroCurrent = zeroCurrent;
            lastFullCurrent = fullCurrent;
            lastFullHeightMm = fullHeightMm;
        }
        float senseRes = g_config.currentSenseResistorOhms();
        float senseGain = g_config.currentSenseGain();
        if (fabsf(senseRes - lastSenseResistor) > 0.1f || fabsf(senseGain - lastSenseGain) > 0.001f) {
            g_levelSensor.setCurrentSense(senseRes, senseGain);
            lastSenseResistor = senseRes;
            lastSenseGain = senseGain;
        }
        float alphaGain = g_config.alphaGain();
        float betaGain = g_config.betaGain();
        if (fabsf(alphaGain - lastAlphaGain) > 0.0001f || fabsf(betaGain - lastBetaGain) > 0.0001f) {
            g_levelSensor.setFilterGains(alphaGain, betaGain);
            lastAlphaGain = alphaGain;
            lastBetaGain = betaGain;
        }
        float density = g_config.densityFactor();
        if (fabsf(density - lastDensity) > 0.0001f) {
            g_levelSensor.setDensityFactor(density);
            lastDensity = density;
        }

        FlowSensor::Snapshot snapshot = g_flowSensor.takeSnapshot();
        uint64_t deltaTotal = snapshot.totalPulses - previousCount;
        previousCount = snapshot.totalPulses;
        uint32_t deltaPulses = static_cast<uint32_t>(deltaTotal);
        float intervalSeconds = static_cast<float>(intervalMs) / 1000.0f;
        float flowLps = utils::pulsesToFlowLps(deltaPulses, intervalSeconds, g_config.pulsesPerLiter());

        std::vector<float> pulsePeriodsUs;
        pulsePeriodsUs.reserve(snapshot.periodCount);
        for (size_t i = 0; i < snapshot.periodCount; ++i) {
            if (snapshot.recentPeriods[i] > 0) {
                pulsePeriodsUs.push_back(static_cast<float>(snapshot.recentPeriods[i]));
            }
        }

        float pulseMeanUs = NAN;
        float pulseMedianUs = NAN;
        float pulseStdUs = NAN;
        float pulseCv = NAN;
        if (!pulsePeriodsUs.empty()) {
            double sum = 0.0;
            for (float value : pulsePeriodsUs) {
                sum += value;
            }
            pulseMeanUs = static_cast<float>(sum / pulsePeriodsUs.size());
            std::vector<float> sortedPeriods = pulsePeriodsUs;
            std::sort(sortedPeriods.begin(), sortedPeriods.end());
            if (sortedPeriods.size() % 2 == 0) {
                pulseMedianUs = (sortedPeriods[sortedPeriods.size() / 2 - 1] +
                                 sortedPeriods[sortedPeriods.size() / 2]) /
                                2.0f;
            } else {
                pulseMedianUs = sortedPeriods[sortedPeriods.size() / 2];
            }
            double variance = 0.0;
            for (float value : pulsePeriodsUs) {
                double diff = value - pulseMeanUs;
                variance += diff * diff;
            }
            variance /= pulsePeriodsUs.size();
            pulseStdUs = static_cast<float>(sqrt(variance));
            if (!isnan(pulseMeanUs) && fabs(pulseMeanUs) > 0.0001f) {
                pulseCv = (pulseStdUs / pulseMeanUs) * 100.0f;
            }
        }

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
        metrics.flowPulseMeanUs = pulseMeanUs;
        metrics.flowPulseMedianUs = pulseMedianUs;
        metrics.flowPulseStdUs = pulseStdUs;
        metrics.flowPulseCv = pulseCv;
        metrics.flowPeriodCount = pulsePeriodsUs.size();
        for (size_t i = 0; i < utils::MAX_FLOW_PERIOD_SAMPLES && i < pulsePeriodsUs.size(); ++i) {
            metrics.flowRecentPeriods[i] = static_cast<uint32_t>(pulsePeriodsUs[i]);
        }
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
        metrics.levelVoltage = levelReading.voltage;
        metrics.levelAverageVoltage = levelReading.averageVoltage;
        metrics.levelMedianVoltage = levelReading.medianVoltage;
        metrics.levelTrimmedVoltage = levelReading.trimmedMeanVoltage;
        metrics.levelStdDevVoltage = levelReading.standardDeviation;
        metrics.levelEmaVoltage = levelReading.emaVoltage;
        metrics.levelCurrentMa = levelReading.currentMilliAmps;
        metrics.levelDepthMm = levelReading.depthMillimeters;
        metrics.levelRawHeightCm = levelReading.rawHeightCm;
        metrics.levelFilteredHeightCm = levelReading.filteredHeightCm;
        metrics.levelAlphaBetaVelocity = levelReading.alphaBetaVelocity;
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
    utils::SensorMetrics latest;
    bool haveLatest = false;
    TickType_t lastLogTick = xTaskGetTickCount();
    while (true) {
        if (g_loggerQueue && xQueueReceive(g_loggerQueue, &metrics, pdMS_TO_TICKS(1000)) == pdTRUE) {
            latest = metrics;
            haveLatest = true;
        }
        uint32_t intervalMs = g_config.loggingIntervalMs();
        if (intervalMs < 500) {
            intervalMs = 500;
        }
        TickType_t now = xTaskGetTickCount();
        if (haveLatest && now - lastLogTick >= pdMS_TO_TICKS(intervalMs)) {
            g_logger.log(latest);
            lastLogTick = now;
            haveLatest = false;
        }
        g_logger.update();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

