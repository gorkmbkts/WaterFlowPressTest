#include "Joystick.h"

#include <driver/adc.h>

#include <../Utils/Utils.h>

namespace {
constexpr float ANALOG_MAX = 4095.0f;
constexpr float ANALOG_CENTER = ANALOG_MAX / 2.0f;
}

void Joystick::begin(uint8_t pinX, uint8_t pinY, float deadband) {
    _pinX = pinX;
    _pinY = pinY;
    _deadband = deadband;
    analogSetPinAttenuation(_pinX, ADC_11db);
    analogSetPinAttenuation(_pinY, ADC_11db);
    analogSetWidth(12);
}

float Joystick::normalize(int raw) const {
    float normalized = (static_cast<float>(raw) - ANALOG_CENTER) / ANALOG_CENTER;
    if (fabs(normalized) < _deadband) {
        return 0.0f;
    }
    normalized = utils::clampValue(normalized / (1.0f - _deadband), -1.0f, 1.0f);
    return normalized;
}

float Joystick::readX() {
    int raw = analogRead(_pinX);
    return -normalize(raw);
}

float Joystick::readY() {
    int raw = analogRead(_pinY);
    return -normalize(raw);  // Y ekseni ters çevrildi
}

