#include "Buttons.h"

void Buttons::begin(uint8_t pinButton1, uint8_t pinButton2) {
    _pin1 = pinButton1;
    _pin2 = pinButton2;
    pinMode(_pin1, INPUT_PULLUP);
    pinMode(_pin2, INPUT_PULLUP);
    _button1.attach(_pin1);
    _button2.attach(_pin2);
    _button1.interval(10);
    _button2.interval(10);
}

void Buttons::update() {
    _button1.update();
    _button2.update();

    if (_button1.fell()) {
        _pressedEvent1 = true;
    }
    if (_button2.fell()) {
        _pressedEvent2 = true;
    }

    if (isPressed(ButtonId::One) && isPressed(ButtonId::Two)) {
        if (_bothPressStart == 0) {
            _bothPressStart = millis();
        }
    } else {
        _bothPressStart = 0;
    }
}

bool Buttons::wasPressed(ButtonId id) {
    bool triggered = false;
    if (id == ButtonId::One) {
        triggered = _pressedEvent1;
        _pressedEvent1 = false;
    } else {
        triggered = _pressedEvent2;
        _pressedEvent2 = false;
    }
    return triggered;
}

bool Buttons::isPressed(ButtonId id) const {
    if (id == ButtonId::One) {
        return _button1.read() == LOW;
    }
    return _button2.read() == LOW;
}

bool Buttons::bothHeldFor(uint32_t durationMs) {
    if (_bothPressStart == 0) {
        return false;
    }
    uint32_t now = millis();
    if (now - _bothPressStart >= durationMs) {
        _bothPressStart = 0;  // prevent repeated triggers
        return true;
    }
    return false;
}

