#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <algorithm>
#include <cmath>

class ConfigService {
  public:
    void begin() {
        if (_prefs.begin("wfpress", false)) {
            _prefsInitialized = true;
            loadFromStorage();
        }
    }

    void end() {
        if (_prefsInitialized) {
            _prefs.end();
            _prefsInitialized = false;
        }
    }

    uint32_t sensorIntervalMs() const { return _sensorIntervalMs; }
    void setSensorIntervalMs(uint32_t value) {
        value = clampInterval(value, 200, 60000);
        if (value != _sensorIntervalMs) {
            _sensorIntervalMs = value;
            persist();
        }
    }

    uint32_t loggingIntervalMs() const { return _loggingIntervalMs; }
    void setLoggingIntervalMs(uint32_t value) {
        value = clampInterval(value, 500, 60000);
        if (value != _loggingIntervalMs) {
            _loggingIntervalMs = value;
            persist();
        }
    }

    float densityFactor() const { return _densityFactor; }
    void setDensityFactor(float value) {
        value = (value <= 0.0f) ? 1.0f : value;
        if (fabsf(_densityFactor - value) > 0.0001f) {
            _densityFactor = value;
            persist();
        }
    }

    uint8_t levelOversampleCount() const { return _oversampleCount; }
    void setLevelOversampleCount(uint8_t count) {
        count = std::max<uint8_t>(3, std::min<uint8_t>(64, count));
        if (count != _oversampleCount) {
            _oversampleCount = count;
            persist();
        }
    }

    float zeroCurrentMa() const { return _zeroCurrentMa; }
    void setZeroCurrentMa(float value) {
        value = constrainFloat(value, 0.0f, 10.0f);
        if (fabsf(_zeroCurrentMa - value) > 0.0001f) {
            _zeroCurrentMa = value;
            persist();
        }
    }

    float fullScaleCurrentMa() const { return _fullScaleCurrentMa; }
    void setFullScaleCurrentMa(float value) {
        value = constrainFloat(value, 12.0f, 30.0f);
        if (fabsf(_fullScaleCurrentMa - value) > 0.0001f) {
            _fullScaleCurrentMa = value;
            persist();
        }
    }

    float fullScaleHeightMm() const { return _fullScaleHeightMm; }
    void setFullScaleHeightMm(float value) {
        value = constrainFloat(value, 500.0f, 10000.0f);
        if (fabsf(_fullScaleHeightMm - value) > 0.01f) {
            _fullScaleHeightMm = value;
            persist();
        }
    }

    float pulsesPerLiter() const { return _pulsesPerLiter; }
    void setPulsesPerLiter(float value) {
        value = constrainFloat(value, 1.0f, 200.0f);
        if (fabsf(_pulsesPerLiter - value) > 0.0001f) {
            _pulsesPerLiter = value;
            persist();
        }
    }

    float currentSenseResistorOhms() const { return _senseResistorOhms; }
    void setCurrentSenseResistorOhms(float value) {
        value = constrainFloat(value, 10.0f, 1000.0f);
        if (fabsf(_senseResistorOhms - value) > 0.01f) {
            _senseResistorOhms = value;
            persist();
        }
    }

    float currentSenseGain() const { return _senseGain; }
    void setCurrentSenseGain(float value) {
        value = constrainFloat(value, 0.1f, 10.0f);
        if (fabsf(_senseGain - value) > 0.0001f) {
            _senseGain = value;
            persist();
        }
    }

    float alphaGain() const { return _alphaGain; }
    void setAlphaGain(float value) {
        value = constrainFloat(value, 0.01f, 1.0f);
        if (fabsf(_alphaGain - value) > 0.0001f) {
            _alphaGain = value;
            persist();
        }
    }

    float betaGain() const { return _betaGain; }
    void setBetaGain(float value) {
        value = constrainFloat(value, 0.001f, 1.0f);
        if (fabsf(_betaGain - value) > 0.0001f) {
            _betaGain = value;
            persist();
        }
    }

  private:
    uint32_t clampInterval(uint32_t value, uint32_t minValue, uint32_t maxValue) const {
        if (value < minValue) {
            return minValue;
        }
        if (value > maxValue) {
            return maxValue;
        }
        return value;
    }

    float constrainFloat(float value, float minValue, float maxValue) const {
        if (value < minValue) {
            return minValue;
        }
        if (value > maxValue) {
            return maxValue;
        }
        return value;
    }

    void loadFromStorage() {
        if (!_prefsInitialized) {
            return;
        }
        _sensorIntervalMs = _prefs.getULong("sens_int", _sensorIntervalMs);
        _loggingIntervalMs = _prefs.getULong("log_int", _loggingIntervalMs);
        _densityFactor = _prefs.getFloat("density", _densityFactor);
        _oversampleCount = static_cast<uint8_t>(_prefs.getUInt("os_cnt", _oversampleCount));
        _zeroCurrentMa = _prefs.getFloat("zero_ma", _zeroCurrentMa);
        _fullScaleCurrentMa = _prefs.getFloat("full_ma", _fullScaleCurrentMa);
        _fullScaleHeightMm = _prefs.getFloat("full_mm", _fullScaleHeightMm);
        _pulsesPerLiter = _prefs.getFloat("ppl", _pulsesPerLiter);
        _senseResistorOhms = _prefs.getFloat("sense_r", _senseResistorOhms);
        _senseGain = _prefs.getFloat("sense_g", _senseGain);
        _alphaGain = _prefs.getFloat("alpha", _alphaGain);
        _betaGain = _prefs.getFloat("beta", _betaGain);
    }

    void persist() {
        if (!_prefsInitialized) {
            return;
        }
        _prefs.putULong("sens_int", _sensorIntervalMs);
        _prefs.putULong("log_int", _loggingIntervalMs);
        _prefs.putFloat("density", _densityFactor);
        _prefs.putUInt("os_cnt", _oversampleCount);
        _prefs.putFloat("zero_ma", _zeroCurrentMa);
        _prefs.putFloat("full_ma", _fullScaleCurrentMa);
        _prefs.putFloat("full_mm", _fullScaleHeightMm);
        _prefs.putFloat("ppl", _pulsesPerLiter);
        _prefs.putFloat("sense_r", _senseResistorOhms);
        _prefs.putFloat("sense_g", _senseGain);
        _prefs.putFloat("alpha", _alphaGain);
        _prefs.putFloat("beta", _betaGain);
    }

    Preferences _prefs;
    bool _prefsInitialized = false;
    uint32_t _sensorIntervalMs = 1000;
    uint32_t _loggingIntervalMs = 1000;
    float _densityFactor = 1.0f;
    uint8_t _oversampleCount = 10;
    float _zeroCurrentMa = 4.0f;
    float _fullScaleCurrentMa = 20.0f;
    float _fullScaleHeightMm = 5000.0f;
    float _pulsesPerLiter = 12.0f;
    float _senseResistorOhms = 150.0f;
    float _senseGain = 1.0f;
    float _alphaGain = 0.4f;
    float _betaGain = 0.02f;
};

