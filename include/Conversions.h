#pragma once

#include <algorithm>

#include "SensorData.h"

inline float convertPulseToFlowLps(float frequencyHz) {
  return frequencyHz / 12.0f;
}

inline float convertVoltageToHeight(float voltage, const CalibrationFactors& calibration) {
  float vRange = calibration.vMax - calibration.vMin;
  if (vRange <= 0.01f) {
    return 0.0f;
  }
  float fraction = (voltage - calibration.vMin) / vRange;
  fraction = std::max(0.0f, std::min(1.0f, fraction));
  float densityFactor = (calibration.densityRatio > 0.0f) ? (1.0f / calibration.densityRatio) : 1.0f;
  float height = fraction * calibration.referenceHeightCm * densityFactor;
  return height;
}

