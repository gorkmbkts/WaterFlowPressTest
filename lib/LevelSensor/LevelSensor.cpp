#include "LevelSensor.h"

#include <algorithm>
#include <vector>

namespace {
constexpr float ADC_MAX_VALUE = 4095.0f;
constexpr float ADC_REFERENCE_VOLTAGE = 3.3f;
}

LevelSensor::LevelSensor()
    : _pin(0),
      _oversampleCount(10),
      _emaAlpha(0.2f),
      _ema(NAN),
      _zeroVoltage(0.48f),
      _fullScaleVoltage(2.4f),
      _fullScaleHeightCm(500.0f),
      _densityFactor(1.0f) {}

void LevelSensor::begin(uint8_t pin, adc_attenuation_t attenuation) {
    _pin = pin;
    analogSetPinAttenuation(_pin, attenuation);
    analogSetWidth(12);
    pinMode(_pin, INPUT);
}

void LevelSensor::setOversample(uint8_t count) {
    _oversampleCount = std::max<uint8_t>(3, count);
}

void LevelSensor::setEmaAlpha(float alpha) {
    _emaAlpha = utils::clampValue(alpha, 0.01f, 1.0f);
}

void LevelSensor::setCalibration(float zeroVoltage, float fullScaleVoltage, float fullScaleHeightCm) {
    _zeroVoltage = zeroVoltage;
    _fullScaleVoltage = fullScaleVoltage;
    _fullScaleHeightCm = fullScaleHeightCm;
}

void LevelSensor::setDensityFactor(float densityFactor) {
    _densityFactor = densityFactor;
}

float LevelSensor::rawToVoltage(uint16_t raw) const {
    return (static_cast<float>(raw) / ADC_MAX_VALUE) * ADC_REFERENCE_VOLTAGE;
}

utils::LevelReading LevelSensor::sample() {
    std::vector<uint16_t> samples;
    samples.reserve(_oversampleCount);

    for (uint8_t i = 0; i < _oversampleCount; ++i) {
        samples.push_back(analogRead(_pin));
        delayMicroseconds(200);
    }

    std::vector<float> voltages;
    voltages.reserve(samples.size());
    for (auto raw : samples) {
        voltages.push_back(rawToVoltage(raw));
    }

    utils::LevelReading reading;
    if (voltages.empty()) {
        return reading;
    }

    float sum = 0.0f;
    for (float v : voltages) {
        sum += v;
    }
    reading.averageVoltage = sum / voltages.size();

    std::vector<float> sorted = voltages;
    std::sort(sorted.begin(), sorted.end());
    if (!sorted.empty()) {
        if (sorted.size() % 2 == 0) {
            reading.medianVoltage = (sorted[sorted.size() / 2 - 1] + sorted[sorted.size() / 2]) / 2.0f;
        } else {
            reading.medianVoltage = sorted[sorted.size() / 2];
        }
    }

    float variance = 0.0f;
    for (float v : voltages) {
        float diff = v - reading.averageVoltage;
        variance += diff * diff;
    }
    variance /= voltages.size();
    reading.standardDeviation = sqrtf(variance);

    if (isnan(_ema)) {
        _ema = reading.averageVoltage;
    } else {
        _ema = _emaAlpha * reading.averageVoltage + (1.0f - _emaAlpha) * _ema;
    }

    reading.voltage = voltages.back();
    reading.emaVoltage = _ema;
    reading.noisePercent = (reading.averageVoltage > 0.0f) ? (reading.standardDeviation / reading.averageVoltage) * 100.0f : 0.0f;
    reading.heightCm = utils::voltageToHeightCm(_ema, _zeroVoltage, _fullScaleVoltage, _fullScaleHeightCm, _densityFactor);

    return reading;
}

