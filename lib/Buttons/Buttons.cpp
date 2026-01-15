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
    
    // Timing değişkenlerini açıkça sıfırla
    _button1PressStart = 0;
    _button2PressStart = 0;
    _bothPressStart = 0;
    _pressedEvent1 = false;
    _pressedEvent2 = false;
    
    Serial.println("🔘 Buttons initialized, all timers reset");
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

    // Track individual button hold times
    if (isPressed(ButtonId::One)) {
        if (_button1PressStart == 0) {
            _button1PressStart = millis();
        }
    } else {
        _button1PressStart = 0;
    }
    
    if (isPressed(ButtonId::Two)) {
        if (_button2PressStart == 0) {
            _button2PressStart = millis();
        }
    } else {
        _button2PressStart = 0;
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

bool Buttons::isHeldFor(ButtonId id, uint32_t durationMs) {
    // Önce button gerçekten basılı mı kontrol et
    if (!isPressed(id)) {
        return false;
    }
    
    uint32_t pressStart = (id == ButtonId::One) ? _button1PressStart : _button2PressStart;
    if (pressStart == 0) {
        return false;
    }
    
    uint32_t now = millis();
    if (now - pressStart >= durationMs) {
        // Debug mesajı
        Serial.printf("🔴 Button %d gerçekten %d ms basılı tutuldu!\n", 
            (id == ButtonId::One) ? 1 : 2, durationMs);
            
        if (id == ButtonId::One) {
            _button1PressStart = 0;  // prevent repeated triggers
        } else {
            _button2PressStart = 0;  // prevent repeated triggers
        }
        return true;
    }
    return false;
}

