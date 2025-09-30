#pragma once

#include <Arduino.h>

class ConfigService {
  public:
    void begin() {}

    uint32_t sensorIntervalMs() const { return _sensorIntervalMs; }
    void setSensorIntervalMs(uint32_t value) { _sensorIntervalMs = value; }

    uint32_t loggingIntervalMs() const { return _loggingIntervalMs; }
    void setLoggingIntervalMs(uint32_t value) { _loggingIntervalMs = value; }

    float densityFactor() const { return _densityFactor; }
    void setDensityFactor(float value) { _densityFactor = value <= 0.0f ? 1.0f : value; }

    uint8_t levelOversampleCount() const { return _oversampleCount; }
    void setLevelOversampleCount(uint8_t count) { _oversampleCount = count; }

  private:
    uint32_t _sensorIntervalMs = 1000;
    uint32_t _loggingIntervalMs = 1000;
    float _densityFactor = 1.0f;
    uint8_t _oversampleCount = 10;
};

