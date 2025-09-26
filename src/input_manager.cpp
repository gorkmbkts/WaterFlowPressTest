#include "input_manager.h"
#include <algorithm>

InputManager::InputManager() : button1(BUTTON1_PIN, 25), button2(BUTTON2_PIN, 25) {
    // Initialize state
    memset(&joystick, 0, sizeof(joystick));
    memset(&button1_state, 0, sizeof(button1_state));
    memset(&button2_state, 0, sizeof(button2_state));
    
    both_buttons_held = false;
    both_buttons_start = 0;
    last_joystick_event = 0;
    joystick_repeat_delay = 200; // Default 200ms
    
    // Default joystick calibration values
    joystick_x_center = 2048;
    joystick_y_center = 2048;
    joystick_x_range = 1800;
    joystick_y_range = 1800;
}

bool InputManager::initialize() {
    DEBUG_PRINTLN(F("Initializing input manager..."));
    
    // Initialize buttons with internal pull-ups
    pinMode(BUTTON1_PIN, INPUT_PULLUP);
    pinMode(BUTTON2_PIN, INPUT_PULLUP);
    
    button1.attach(BUTTON1_PIN, INPUT_PULLUP);
    button2.attach(BUTTON2_PIN, INPUT_PULLUP);
    
    // Set debounce interval
    button1.interval(25);
    button2.interval(25);
    
    // Configure ADC for joystick
    analogSetAttenuation(ADC_11db);
    analogReadResolution(12);
    
    // Calibrate joystick center position
    calibrateJoystick();
    
    DEBUG_PRINTLN(F("Input manager initialized"));
    return true;
}

void InputManager::calibrateJoystick() {
    DEBUG_PRINTLN(F("Calibrating joystick..."));
    
    // Take multiple readings to find center position
    uint32_t x_total = 0, y_total = 0;
    const int samples = 50;
    
    for (int i = 0; i < samples; i++) {
        x_total += analogRead(JOYSTICK_X_PIN);
        y_total += analogRead(JOYSTICK_Y_PIN);
        delay(20);
    }
    
    joystick_x_center = x_total / samples;
    joystick_y_center = y_total / samples;
    
    DEBUG_PRINT(F("Joystick center: X="));
    DEBUG_PRINT(joystick_x_center);
    DEBUG_PRINT(F(", Y="));
    DEBUG_PRINTLN(joystick_y_center);
}

InputEvent InputManager::update() {
    // Update button states
    updateButtons();
    
    // Update joystick
    readJoystick();
    
    // Check for button events first (highest priority)
    InputEvent button_event = processButtonEvent();
    if (button_event != EVENT_NONE) {
        return button_event;
    }
    
    // Check for joystick events
    return processJoystickEvent();
}

void InputManager::readJoystick() {
    uint16_t x_raw = analogRead(JOYSTICK_X_PIN);
    uint16_t y_raw = analogRead(JOYSTICK_Y_PIN);
    
    // Map to -1.0 to 1.0 range
    joystick.x = mapJoystickAxis(x_raw, joystick_x_center, joystick_x_range);
    joystick.y = mapJoystickAxis(y_raw, joystick_y_center, joystick_y_range);
    
    // Apply deadband
    joystick.x_active = !isInDeadband(joystick.x);
    joystick.y_active = !isInDeadband(joystick.y);
    
    // Check for acceleration
    joystick.acceleration_active = (abs(joystick.y) > JOYSTICK_ACCEL_THRESHOLD);
    
    // Clear inactive axes
    if (!joystick.x_active) joystick.x = 0.0f;
    if (!joystick.y_active) joystick.y = 0.0f;
}

void InputManager::updateButtons() {
    button1.update();
    button2.update();
    
    // Update button 1 state
    button1_state.previous = button1_state.current;
    button1_state.current = !button1.read(); // Active low
    button1_state.pressed = !button1_state.previous && button1_state.current;
    button1_state.released = button1_state.previous && !button1_state.current;
    
    if (button1_state.pressed) {
        button1_state.press_start = millis();
    }
    if (button1_state.current) {
        button1_state.hold_duration = millis() - button1_state.press_start;
    } else {
        button1_state.hold_duration = 0;
    }
    
    // Update button 2 state
    button2_state.previous = button2_state.current;
    button2_state.current = !button2.read(); // Active low
    button2_state.pressed = !button2_state.previous && button2_state.current;
    button2_state.released = button2_state.previous && !button2_state.current;
    
    if (button2_state.pressed) {
        button2_state.press_start = millis();
    }
    if (button2_state.current) {
        button2_state.hold_duration = millis() - button2_state.press_start;
    } else {
        button2_state.hold_duration = 0;
    }
    
    // Check for both buttons held
    if (button1_state.current && button2_state.current) {
        if (!both_buttons_held) {
            both_buttons_held = true;
            both_buttons_start = millis();
        }
    } else {
        both_buttons_held = false;
        both_buttons_start = 0;
    }
}

float InputManager::mapJoystickAxis(uint16_t raw_value, uint16_t center, uint16_t range) {
    float normalized;
    
    if (raw_value > center) {
        // Positive direction
        float max_val = center + range / 2;
        normalized = (float)(raw_value - center) / (max_val - center);
        normalized = std::min(normalized, 1.0f);
    } else {
        // Negative direction
        float min_val = center - range / 2;
        normalized = (float)(raw_value - center) / (center - min_val);
        normalized = std::max(normalized, -1.0f);
    }
    
    return normalized;
}

bool InputManager::isInDeadband(float value) {
    return abs(value) < JOYSTICK_DEADBAND;
}

InputEvent InputManager::processJoystickEvent() {
    // Implement rate limiting for joystick events
    uint32_t now = millis();
    if (now - last_joystick_event < joystick_repeat_delay) {
        return EVENT_NONE;
    }
    
    // Check for movement events
    if (joystick.x_active) {
        last_joystick_event = now;
        if (joystick.x > 0.5f) {
            return EVENT_JOYSTICK_RIGHT;
        } else if (joystick.x < -0.5f) {
            return EVENT_JOYSTICK_LEFT;
        }
        
        // Check for screen change events (smaller threshold)
        if (joystick.x > 0.3f) {
            return EVENT_SCREEN_CHANGE_RIGHT;
        } else if (joystick.x < -0.3f) {
            return EVENT_SCREEN_CHANGE_LEFT;
        }
    }
    
    if (joystick.y_active) {
        last_joystick_event = now;
        if (joystick.y > 0.5f) {
            return EVENT_JOYSTICK_UP;
        } else if (joystick.y < -0.5f) {
            return EVENT_JOYSTICK_DOWN;
        }
    }
    
    return EVENT_NONE;
}

InputEvent InputManager::processButtonEvent() {
    // Check for both buttons held
    if (both_buttons_held && getBothButtonsHoldTime() >= CALIBRATION_HOLD_MS) {
        return EVENT_BOTH_BUTTONS_HOLD;
    }
    
    // Individual button presses
    if (button1_state.pressed) {
        return EVENT_BUTTON1_PRESS;
    }
    
    if (button2_state.pressed) {
        return EVENT_BUTTON2_PRESS;
    }
    
    return EVENT_NONE;
}

uint32_t InputManager::getBothButtonsHoldTime() {
    if (both_buttons_held) {
        return millis() - both_buttons_start;
    }
    return 0;
}

bool InputManager::hasJoystickMoved() {
    return joystick.x_active || joystick.y_active;
}

float InputManager::getJoystickAcceleration() {
    if (joystick.acceleration_active) {
        return JOYSTICK_ACCEL_FACTOR;
    }
    return 1.0f;
}

int InputManager::getJoystickDirection() {
    if (joystick.y > 0.5f) return 1;
    if (joystick.y < -0.5f) return -1;
    return 0;
}

void InputManager::printDebugInfo() {
#ifdef DEBUG_MODE
    Serial.printf("Joystick: X=%.2f Y=%.2f Active:%s%s Accel:%s\n",
        joystick.x, joystick.y,
        joystick.x_active ? " X" : "",
        joystick.y_active ? " Y" : "",
        joystick.acceleration_active ? "YES" : "NO");
    Serial.printf("Buttons: B1=%s B2=%s Both=%s (Hold: %dms)\n",
        button1_state.current ? "ON" : "OFF",
        button2_state.current ? "ON" : "OFF",
        both_buttons_held ? "YES" : "NO",
        getBothButtonsHoldTime());
#endif
}

void InputManager::calibrateInputs() {
    DEBUG_PRINTLN(F("Starting input calibration..."));
    
    // Re-calibrate joystick
    calibrateJoystick();
    
    // Test button responsiveness
    DEBUG_PRINTLN(F("Press each button to test..."));
    uint32_t test_start = millis();
    bool button1_tested = false, button2_tested = false;
    
    while (millis() - test_start < 10000 && (!button1_tested || !button2_tested)) {
        update();
        
        if (button1_state.pressed && !button1_tested) {
            DEBUG_PRINTLN(F("Button 1 OK"));
            button1_tested = true;
        }
        
        if (button2_state.pressed && !button2_tested) {
            DEBUG_PRINTLN(F("Button 2 OK"));
            button2_tested = true;
        }
        
        delay(50);
    }
    
    if (button1_tested && button2_tested) {
        DEBUG_PRINTLN(F("Input calibration complete"));
    } else {
        DEBUG_PRINTLN(F("WARNING: Some inputs not responding"));
    }
}