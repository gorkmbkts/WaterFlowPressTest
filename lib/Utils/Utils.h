#pragma once

#include <Arduino.h>
#include <algorithm>
#include <deque>
#include <set>
#include <string>
#include <vector>

namespace utils {

constexpr float EPSILON = 1e-6f;

template <typename T>
T clampValue(T value, T low, T high) {
    return std::max(low, std::min(value, high));
}

inline float mapToRange(float value, float inMin, float inMax, float outMin, float outMax) {
    if (fabs(inMax - inMin) < EPSILON) {
        return outMin;
    }
    float clamped = clampValue(value, inMin, inMax);
    return (clamped - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}

struct SensorMetrics {
    time_t timestamp = 0;
    uint32_t pulseCount = 0;
    float pulseIntervalSeconds = 0.0f;
    float flowLps = 0.0f;
    float flowBaselineLps = NAN;
    float flowDiffPercent = NAN;
    float flowMinHealthyLps = NAN;
    float flowMeanLps = NAN;
    float flowMedianLps = NAN;
    float flowStdDevLps = NAN;
    float flowMaxLps = NAN;
    float flowMinLps = NAN;

    float tankHeightCm = 0.0f;
    float tankEmptyEstimateCm = NAN;
    float tankFullEstimateCm = NAN;
    float tankDiffPercent = NAN;
    float tankNoisePercent = NAN;
    float tankMeanCm = NAN;
    float tankMedianCm = NAN;
    float tankStdDevCm = NAN;
    float tankMinObservedCm = NAN;
    float tankMaxObservedCm = NAN;

    float levelVoltage = NAN;
    float emaVoltage = NAN;
    float densityFactor = 1.0f;

    bool pumpOn = false;
};

struct LevelReading {
    float voltage = 0.0f;
    float averageVoltage = 0.0f;
    float medianVoltage = 0.0f;
    float emaVoltage = 0.0f;
    float heightCm = 0.0f;
    float standardDeviation = 0.0f;
    float noisePercent = 0.0f;
};

struct FlowReading {
    uint32_t totalPulses = 0;
    uint32_t deltaPulses = 0;
    uint32_t lastPeriodMicros = 0;
    uint32_t lastTimestampMicros = 0;
    float flowLps = 0.0f;
};

inline float pulsesToFrequency(uint32_t pulses, float durationSeconds) {
    if (durationSeconds <= 0.0f) {
        return 0.0f;
    }
    return static_cast<float>(pulses) / durationSeconds;
}

inline float pulsesToFlowLps(uint32_t pulses, float durationSeconds) {
    float frequency = pulsesToFrequency(pulses, durationSeconds);
    // Datasheet: f = 0.2 * Q(L/min) => Q(L/min) = f / 0.2 => Q(L/s) = (f / 0.2) / 60 = f / 12
    return frequency / 12.0f;
}

inline float voltageToHeightCm(float voltage, float zeroVoltage, float fullScaleVoltage, float fullScaleHeightCm, float densityFactor) {
    float numerator = voltage - zeroVoltage;
    float denominator = fullScaleVoltage - zeroVoltage;
    if (denominator <= 0.0f) {
        denominator = 1.0f;
    }
    float normalized = numerator / denominator;
    normalized = clampValue(normalized, 0.0f, 1.0f);
    float waterDensity = 1.0f;
    if (densityFactor <= 0.0f) {
        densityFactor = waterDensity;
    }
    float adjustedHeight = normalized * fullScaleHeightCm * (waterDensity / densityFactor);
    return adjustedHeight;
}

inline String formatFloat(float value, uint8_t decimals = 2) {
    if (isnan(value)) {
        return "--";
    }
    char buffer[16];
    dtostrf(value, 0, decimals, buffer);
    return String(buffer);
}

inline String qualitativeNoise(float noisePercent) {
    if (isnan(noisePercent)) {
        return "unknown";
    }
    if (noisePercent < 2.0f) {
        return "good";
    }
    if (noisePercent < 5.0f) {
        return "fair";
    }
    return "poor";
}

class RollingStats {
  public:
    explicit RollingStats(size_t maxSamples = 300) : _maxSamples(maxSamples) {}

    void setMaxSamples(size_t samples) {
        _maxSamples = std::max<size_t>(2, samples);
        trim();
    }

    void add(float value) {
        _history.push_back(value);
        if (_history.size() > _maxSamples) {
            _history.pop_front();
        }
    }

    size_t size() const {
        return _history.size();
    }

    bool empty() const {
        return _history.empty();
    }

    void clear() {
        _history.clear();
    }

    float mean() const {
        if (_history.empty()) {
            return NAN;
        }
        double sum = 0.0;
        for (float v : _history) {
            sum += v;
        }
        return static_cast<float>(sum / static_cast<double>(_history.size()));
    }

    float variance() const {
        if (_history.size() < 2) {
            return NAN;
        }
        float m = mean();
        double sum = 0.0;
        for (float v : _history) {
            double diff = static_cast<double>(v) - m;
            sum += diff * diff;
        }
        return static_cast<float>(sum / static_cast<double>(_history.size() - 1));
    }

    float stddev() const {
        float var = variance();
        return isnan(var) ? NAN : sqrtf(var);
    }

    float min() const {
        if (_history.empty()) {
            return NAN;
        }
        return *std::min_element(_history.begin(), _history.end());
    }

    float max() const {
        if (_history.empty()) {
            return NAN;
        }
        return *std::max_element(_history.begin(), _history.end());
    }

    float percentile(float percent) const {
        if (_history.empty()) {
            return NAN;
        }
        percent = clampValue(percent, 0.0f, 100.0f);
        std::vector<float> sorted(_history.begin(), _history.end());
        std::sort(sorted.begin(), sorted.end());
        float rank = percent / 100.0f * (sorted.size() - 1);
        size_t lower = static_cast<size_t>(floorf(rank));
        size_t upper = static_cast<size_t>(ceilf(rank));
        if (upper >= sorted.size()) {
            upper = sorted.size() - 1;
        }
        float fraction = rank - lower;
        return sorted[lower] + (sorted[upper] - sorted[lower]) * fraction;
    }

    float median() const {
        return percentile(50.0f);
    }

    const std::deque<float>& history() const {
        return _history;
    }

  private:
    void trim() {
        while (_history.size() > _maxSamples) {
            _history.pop_front();
        }
    }

    std::deque<float> _history;
    size_t _maxSamples;
};

struct FlowAnalyticsResult {
    float baselineLps = NAN;
    float minHealthyLps = NAN;
    float meanLps = NAN;
    float medianLps = NAN;
    float stdDevLps = NAN;
    float minLps = NAN;
    float maxLps = NAN;
    bool pumpOn = false;
};

class FlowAnalytics {
  public:
    FlowAnalytics() : _overall(300), _pumpSamples(300) {}

    FlowAnalyticsResult update(float flowLps) {
        FlowAnalyticsResult result;
        if (isnan(flowLps)) {
            return result;
        }
        _overall.add(flowLps);
        result.meanLps = _overall.mean();
        result.medianLps = _overall.median();
        result.stdDevLps = _overall.stddev();
        result.minLps = _overall.min();
        result.maxLps = _overall.max();

        bool pumpOn = flowLps > 0.05f;
        result.pumpOn = pumpOn;
        if (pumpOn) {
            _pumpSamples.add(flowLps);
        }

        if (!_pumpSamples.empty()) {
            result.baselineLps = _pumpSamples.percentile(90.0f);
            result.minHealthyLps = _pumpSamples.percentile(10.0f);
        }

        return result;
    }

  private:
    RollingStats _overall;
    RollingStats _pumpSamples;
};

struct LevelAnalyticsResult {
    float emptyEstimateCm = NAN;
    float fullEstimateCm = NAN;
    float meanCm = NAN;
    float medianCm = NAN;
    float stdDevCm = NAN;
    float minCm = NAN;
    float maxCm = NAN;
};

class LevelAnalytics {
  public:
    LevelAnalytics() : _allSamples(600) {}

    LevelAnalyticsResult update(float heightCm, float noisePercent) {
        LevelAnalyticsResult result;
        if (isnan(heightCm)) {
            return result;
        }
        _allSamples.add(heightCm);
        result.meanCm = _allSamples.mean();
        result.medianCm = _allSamples.median();
        result.stdDevCm = _allSamples.stddev();
        result.minCm = _allSamples.min();
        result.maxCm = _allSamples.max();

        bool quietSurface = noisePercent < 3.0f;
        if (quietSurface) {
            if (isnan(_emptyEstimate)) {
                _emptyEstimate = heightCm;
            } else {
                _emptyEstimate = 0.98f * _emptyEstimate + 0.02f * heightCm;
            }
            if (heightCm > _fullEstimate || isnan(_fullEstimate)) {
                if (isnan(_fullEstimate)) {
                    _fullEstimate = heightCm;
                } else {
                    _fullEstimate = 0.90f * _fullEstimate + 0.10f * heightCm;
                }
            }
        }

        result.emptyEstimateCm = _emptyEstimate;
        result.fullEstimateCm = _fullEstimate;
        return result;
    }

  private:
    RollingStats _allSamples;
    float _emptyEstimate = NAN;
    float _fullEstimate = NAN;
};

}  // namespace utils

