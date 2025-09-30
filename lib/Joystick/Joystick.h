#pragma once

#include <Arduino.h>

class Joystick {
  public:
    void begin(uint8_t pinX, uint8_t pinY, float deadband = 0.1f);
    float readX();
    float readY();

  private:
    float normalize(int raw) const;

    uint8_t _pinX = 0;
    uint8_t _pinY = 0;
    float _deadband = 0.1f;
};

