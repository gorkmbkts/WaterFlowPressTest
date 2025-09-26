/**
 * Simple unit tests for Project Kalkan conversion functions
 */

#include <Arduino.h>
#include <unity.h>
#include "../include/config.h"

// Test flow rate conversion
void test_flow_conversion() {
    // From datasheet: f = 0.2 * Q(L/min)
    // So Q(L/s) = f / 0.2 / 60 = f / 12
    
    float frequency = 12.0f; // Hz
    float expected_flow = 1.0f; // L/s
    float actual_flow = frequency * FLOW_CONVERSION;
    
    TEST_ASSERT_FLOAT_WITHIN(0.001f, expected_flow, actual_flow);
}

// Test pressure sensor conversion
void test_pressure_conversion() {
    // Test voltage to height conversion
    float voltage = 1.44f; // Middle of 0.48V - 2.4V range
    float expected_height = 250.0f; // Middle of 0-500cm range
    
    float normalized = (voltage - PRESSURE_V_MIN) / (PRESSURE_V_MAX - PRESSURE_V_MIN);
    float actual_height = normalized * PRESSURE_HEIGHT_MAX;
    
    TEST_ASSERT_FLOAT_WITHIN(1.0f, expected_height, actual_height);
}

// Test boundary conditions
void test_pressure_boundaries() {
    // Test minimum voltage
    float min_voltage = PRESSURE_V_MIN;
    float normalized_min = (min_voltage - PRESSURE_V_MIN) / (PRESSURE_V_MAX - PRESSURE_V_MIN);
    float height_min = normalized_min * PRESSURE_HEIGHT_MAX;
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, height_min);
    
    // Test maximum voltage
    float max_voltage = PRESSURE_V_MAX;
    float normalized_max = (max_voltage - PRESSURE_V_MIN) / (PRESSURE_V_MAX - PRESSURE_V_MIN);
    float height_max = normalized_max * PRESSURE_HEIGHT_MAX;
    TEST_ASSERT_FLOAT_WITHIN(0.1f, PRESSURE_HEIGHT_MAX, height_max);
}

// Test joystick deadband
void test_joystick_deadband() {
    // Values within deadband should be considered inactive
    TEST_ASSERT_TRUE(abs(0.05f) < JOYSTICK_DEADBAND);
    TEST_ASSERT_TRUE(abs(-0.08f) < JOYSTICK_DEADBAND);
    
    // Values outside deadband should be active
    TEST_ASSERT_FALSE(abs(0.15f) < JOYSTICK_DEADBAND);
    TEST_ASSERT_FALSE(abs(-0.15f) < JOYSTICK_DEADBAND);
}

void setUp(void) {
    // Set up before each test
}

void tearDown(void) {
    // Clean up after each test
}

void setup() {
    delay(2000); // Wait for serial
    
    UNITY_BEGIN();
    
    RUN_TEST(test_flow_conversion);
    RUN_TEST(test_pressure_conversion);
    RUN_TEST(test_pressure_boundaries);
    RUN_TEST(test_joystick_deadband);
    
    UNITY_END();
}

void loop() {
    // Tests run once in setup()
}