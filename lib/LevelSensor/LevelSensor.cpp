#include "LevelSensor.h"

#include <algorithm>
#include <numeric>
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
      _densityFactor(1.0f),
      _zeroCurrentMa(4.0f),
      _fullCurrentMa(20.0f),
      _fullScaleHeightMm(5000.0f),
      _senseResistorOhms(150.0f),
      _senseGain(1.0f),
      _alphaGain(0.4f),
      _betaGain(0.02f),
      _filteredDepthMm(NAN),
      _velocityMmPerSec(0.0f),
      _sampleIntervalSec(1.0f) {}

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

void LevelSensor::setCalibrationCurrent(float zeroCurrentMa, float fullCurrentMa, float fullScaleHeightMm) {
    _zeroCurrentMa = zeroCurrentMa;
    _fullCurrentMa = fullCurrentMa;
    _fullScaleHeightMm = fullScaleHeightMm;
}

void LevelSensor::setCurrentSense(float resistorOhms, float gain) {
    _senseResistorOhms = std::max(1.0f, resistorOhms);
    _senseGain = std::max(0.1f, gain);
}

void LevelSensor::setFilterGains(float alphaGain, float betaGain) {
    _alphaGain = utils::clampValue(alphaGain, 0.01f, 1.0f);
    _betaGain = utils::clampValue(betaGain, 0.001f, 1.0f);
}

void LevelSensor::setSampleIntervalMs(uint32_t intervalMs) {
    if (intervalMs < 50) {
        intervalMs = 50;
    }
    _sampleIntervalSec = static_cast<float>(intervalMs) / 1000.0f;
}

void LevelSensor::setDensityFactor(float densityFactor) {
    _densityFactor = densityFactor;
}

float LevelSensor::rawToVoltage(uint16_t raw) const {
    return (static_cast<float>(raw) / ADC_MAX_VALUE) * ADC_REFERENCE_VOLTAGE;
}

float LevelSensor::computeCurrentMilliAmps(float voltage) const {
    float effectiveGain = (_senseGain <= 0.0f) ? 1.0f : _senseGain;
    float resistor = (_senseResistorOhms <= 0.0f) ? 1.0f : _senseResistorOhms;
    return (voltage / (resistor * effectiveGain)) * 1000.0f;
}

float LevelSensor::applyAlphaBetaFilter(float depthMm) {
    float depth = depthMm;
    if (isnan(depth)) {
        return _filteredDepthMm;
    }
    float dt = (_sampleIntervalSec <= 0.0f) ? 1.0f : _sampleIntervalSec;
    if (isnan(_filteredDepthMm)) {
        _filteredDepthMm = depth;
        _velocityMmPerSec = 0.0f;
    } else {
        float prediction = _filteredDepthMm + _velocityMmPerSec * dt;
        float residual = depth - prediction;
        _filteredDepthMm = prediction + _alphaGain * residual;
        _velocityMmPerSec = _velocityMmPerSec + (_betaGain * residual) / dt;
    }
    if (_filteredDepthMm < 0.0f) {
        _filteredDepthMm = 0.0f;
        _velocityMmPerSec = 0.0f;
    }
    return _filteredDepthMm;
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

    size_t trimCount = std::max<size_t>(1, sorted.size() / 10);
    float trimmedMean = 0.0f;
    if (sorted.size() > 2 * trimCount) {
        float trimmedSum = 0.0f;
        for (size_t i = trimCount; i < sorted.size() - trimCount; ++i) {
            trimmedSum += sorted[i];
        }
        trimmedMean = trimmedSum / static_cast<float>(sorted.size() - 2 * trimCount);
    } else {
        trimmedMean = reading.averageVoltage;
    }
    reading.trimmedMeanVoltage = trimmedMean;

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
    float referenceVoltage = reading.trimmedMeanVoltage > 0.0f ? reading.trimmedMeanVoltage : reading.averageVoltage;
    reading.noisePercent = (referenceVoltage > 0.0f) ? (reading.standardDeviation / referenceVoltage) * 100.0f : 0.0f;

    float currentMa = computeCurrentMilliAmps(trimmedMean);
    reading.currentMilliAmps = currentMa;

    float spanMa = _fullCurrentMa - _zeroCurrentMa;
    float normalizedCurrent = 0.0f;
    if (spanMa > 0.1f) {
        normalizedCurrent = (currentMa - _zeroCurrentMa) / spanMa;
    }
    normalizedCurrent = utils::clampValue(normalizedCurrent, 0.0f, 1.0f);
    float depthMm = normalizedCurrent * _fullScaleHeightMm;
    float density = (_densityFactor <= 0.0f) ? 1.0f : _densityFactor;
    if (density > 0.0f) {
        depthMm /= density;
    }
    reading.depthMillimeters = depthMm;
    float filteredMm = applyAlphaBetaFilter(depthMm);
    reading.filteredHeightCm = isnan(filteredMm) ? NAN : (filteredMm / 10.0f);
    reading.rawHeightCm = depthMm / 10.0f;
    reading.heightCm = isnan(reading.filteredHeightCm) ? reading.rawHeightCm : reading.filteredHeightCm;
    reading.alphaBetaVelocity = _velocityMmPerSec;

    return reading;
}

