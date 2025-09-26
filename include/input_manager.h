#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include <Arduino.h>
#include <Bounce2.h>
#include "config.h"

// Input events
enum InputEvent {
    EVENT_NONE,
    EVENT_JOYSTICK_LEFT,
    EVENT_JOYSTICK_RIGHT,
    EVENT_JOYSTICK_UP,
    EVENT_JOYSTICK_DOWN,
    EVENT_BUTTON1_PRESS,
    EVENT_BUTTON2_PRESS,
    EVENT_BOTH_BUTTONS_HOLD,
    EVENT_SCREEN_CHANGE_LEFT,
    EVENT_SCREEN_CHANGE_RIGHT
};

// Joystick state
struct JoystickState {
    float x;                    // -1.0 to 1.0
    float y;                    // -1.0 to 1.0
    bool x_active;              // Outside deadband
    bool y_active;              // Outside deadband
    bool acceleration_active;   // |y| > acceleration threshold
    uint32_t last_event;        // For debouncing rapid changes
};

// Button state
struct ButtonState {
    bool current;
    bool previous;
    bool pressed;               // Edge detection
    bool released;              // Edge detection
    uint32_t press_start;       // For hold detection
    uint32_t hold_duration;
};

class InputManager {
private:
    // Hardware interfaces
    Bounce button1;
    Bounce button2;
    
    // State tracking
    JoystickState joystick;
    ButtonState button1_state;
    ButtonState button2_state;
    bool both_buttons_held;
    uint32_t both_buttons_start;
    
    // Calibration values for joystick
    uint16_t joystick_x_center;
    uint16_t joystick_y_center;
    uint16_t joystick_x_range;
    uint16_t joystick_y_range;
    
    // Event timing
    uint32_t last_joystick_event;
    uint32_t joystick_repeat_delay;
    
    // Private methods
    void readJoystick();
    void updateButtons();
    float mapJoystickAxis(uint16_t raw_value, uint16_t center, uint16_t range);
    bool isInDeadband(float value);
    InputEvent processJoystickEvent();
    InputEvent processButtonEvent();
    void calibrateJoystick();
    
public:
    InputManager();
    
    // Initialization
    bool initialize();
    void calibrateInputs();
    
    // Main update function
    InputEvent update();
    
    // State accessors
    JoystickState getJoystickState() { return joystick; }
    bool isButton1Pressed() { return button1_state.current; }
    bool isButton2Pressed() { return button2_state.current; }
    bool areBothButtonsHeld() { return both_buttons_held; }
    uint32_t getBothButtonsHoldTime();
    
    // Configuration
    void setJoystickRepeatDelay(uint32_t delay_ms) { joystick_repeat_delay = delay_ms; }
    
    // Utility functions for UI
    bool hasJoystickMoved();
    float getJoystickAcceleration(); // Returns acceleration factor
    int getJoystickDirection(); // -1, 0, 1 for each axis
    
    // Debug
    void printDebugInfo();
};

// Global instance declaration
extern InputManager inputManager;

#endif // INPUT_MANAGER_H