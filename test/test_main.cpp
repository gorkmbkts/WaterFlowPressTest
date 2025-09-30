#include <Arduino.h>
#include <unity.h>

#include <Utils/Utils.h>

void test_pulses_to_flow() {
    float flow = utils::pulsesToFlowLps(240, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, flow);
}

void test_voltage_to_height() {
    float height = utils::voltageToHeightCm(1.44f, 0.48f, 2.4f, 500.0f, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 250.0f, height);
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_pulses_to_flow);
    RUN_TEST(test_voltage_to_height);
    UNITY_END();
}

void loop() {}

