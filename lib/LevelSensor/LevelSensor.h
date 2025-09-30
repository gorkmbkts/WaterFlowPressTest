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
    void setDensityFactor(float densityFactor);
    float densityFactor() const { return _densityFactor; }

    utils::LevelReading sample();

  private:
    float rawToVoltage(uint16_t raw) const;

    uint8_t _pin;
    uint8_t _oversampleCount;
    float _emaAlpha;
    float _ema;
    float _zeroVoltage;
    float _fullScaleVoltage;
    float _fullScaleHeightCm;
    float _densityFactor;
};

