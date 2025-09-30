#pragma once

#include <Arduino.h>

struct FlowMetrics {
  float instantaneousLps = 0.0f;
  float baselineLps = 0.0f;
  float diffPercent = 0.0f;
  float minHealthyLps = 0.0f;
  float meanLps = 0.0f;
  float medianLps = 0.0f;
  float minObservedLps = 0.0f;
  float maxObservedLps = 0.0f;
  float stddevLps = 0.0f;
};

struct LevelMetrics {
  float instantaneousCm = 0.0f;
  float thetaCm = 0.0f;
  float sigmaCm = 0.0f;
  float diffPercent = 0.0f;
  float noisePercent = 0.0f;
  float meanCm = 0.0f;
  float medianCm = 0.0f;
  float minCm = 0.0f;
  float maxCm = 0.0f;
  float stddevCm = 0.0f;
};

struct SensorSample {
  uint64_t timestampMs = 0;
  FlowMetrics flow;
  LevelMetrics level;
  float rawVoltage = 0.0f;
  uint32_t rawPulseCount = 0;
  bool pumpRunning = false;
};
