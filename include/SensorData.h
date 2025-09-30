#pragma once

#include <Arduino.h>

#include "Config.h"

struct FlowMetrics {
  float instantaneousLps = 0.0f;
  float baselineLps = 0.0f;  // Q_n
  float differencePct = 0.0f;
  float minimumHealthyLps = 0.0f;  // Qmin
  float meanLps = 0.0f;  // Qμ
  float medianLps = 0.0f;  // Qη
};

struct LevelMetrics {
  float instantaneousCm = 0.0f;  // h
  float baselineCm = 0.0f;       // hθ
  float fullTankCm = 0.0f;       // hΣ
  float differencePct = 0.0f;    // hdif
  float noiseMetric = 0.0f;      // K, percent or qualitative
};

struct SensorSnapshot {
  time_t timestamp = 0;
  uint32_t pulseCount = 0;
  float pulseFrequencyHz = 0.0f;
  float levelVoltage = 0.0f;
  FlowMetrics flow;
  LevelMetrics level;
};

struct StatisticsSummary {
  float minValue = 0.0f;
  float maxValue = 0.0f;
  float meanValue = 0.0f;
  float medianValue = 0.0f;
  float stddevValue = 0.0f;
};

struct AnalyticsState {
  FlowMetrics flow;
  LevelMetrics level;
  StatisticsSummary flowStats;
  StatisticsSummary levelStats;
};

struct CalibrationFactors {
  float vMin = config::LEVEL_V_MIN;
  float vMax = config::LEVEL_V_MAX;
  float referenceHeightCm = config::LEVEL_RANGE_CM;
  float densityRatio = config::WATER_DENSITY;
};

struct LogRecord {
  time_t timestamp;
  String iso8601;
  uint32_t pulseCount;
  float pulseFrequency;
  float levelVoltage;
  FlowMetrics flow;
  LevelMetrics level;
};

