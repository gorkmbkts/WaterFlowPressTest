#pragma once

#include <Arduino.h>
#include <Bounce2.h>

class Buttons {
  public:
    enum class ButtonId { One = 0, Two = 1 };

    void begin(uint8_t pinButton1, uint8_t pinButton2);
    void update();

    bool wasPressed(ButtonId id);
    bool isPressed(ButtonId id) const;
    bool bothHeldFor(uint32_t durationMs);
    bool isHeldFor(ButtonId id, uint32_t durationMs);

  private:
    Bounce _button1;
    Bounce _button2;
    uint8_t _pin1 = 0;
    uint8_t _pin2 = 0;
    bool _pressedEvent1 = false;
    bool _pressedEvent2 = false;
    uint32_t _bothPressStart = 0;
    uint32_t _button1PressStart = 0;
    uint32_t _button2PressStart = 0;
};

