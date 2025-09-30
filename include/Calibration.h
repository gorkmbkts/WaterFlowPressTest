#pragma once

#include <Arduino.h>

struct CalibrationConfig {
  float densityRatio = 1.0f;
  float zeroVoltage = 0.48f;
  float fullVoltage = 2.4f;
};

extern CalibrationConfig calibration;

float voltageToHeightCm(float voltage);
float pulsesToFlowLps(uint32_t pulseCount, uint32_t elapsedMicros);
