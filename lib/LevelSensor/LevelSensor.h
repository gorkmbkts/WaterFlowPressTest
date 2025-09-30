#pragma once

#include <Arduino.h>
#include <driver/adc.h>
#include <../Utils/Utils.h>

class LevelSensor {
  public:
    LevelSensor();

    void begin(uint8_t pin, adc_attenuation_t attenuation = ADC_11db);
    void setOversample(uint8_t count);
    void setEmaAlpha(float alpha);
    void setCalibration(float zeroVoltage, float fullScaleVoltage, float fullScaleHeightCm);
    void setCalibrationCurrent(float zeroCurrentMa, float fullCurrentMa, float fullScaleHeightMm);
    void setCurrentSense(float resistorOhms, float gain = 1.0f);
    void setFilterGains(float alphaGain, float betaGain);
    void setSampleIntervalMs(uint32_t intervalMs);
    void setDensityFactor(float densityFactor);
    float densityFactor() const { return _densityFactor; }

    utils::LevelReading sample();

  private:
    float rawToVoltage(uint16_t raw) const;
    float computeCurrentMilliAmps(float voltage) const;
    float applyAlphaBetaFilter(float depthMm);

    uint8_t _pin;
    uint8_t _oversampleCount;
    float _emaAlpha;
    float _ema;
    float _zeroVoltage;
    float _fullScaleVoltage;
    float _fullScaleHeightCm;
    float _densityFactor;
    float _zeroCurrentMa;
    float _fullCurrentMa;
    float _fullScaleHeightMm;
    float _senseResistorOhms;
    float _senseGain;
    float _alphaGain;
    float _betaGain;
    float _filteredDepthMm;
    float _velocityMmPerSec;
    float _sampleIntervalSec;
};

