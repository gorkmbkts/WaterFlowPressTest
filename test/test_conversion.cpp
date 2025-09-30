#ifdef UNIT_TEST

#include <Arduino.h>
#include <unity.h>

#include "Calibration.h"
#include "Config.h"

void setUp() {}
void tearDown() {}

void test_pulses_to_flow() {
  float flow = pulsesToFlowLps(120, 1000000);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, flow);
}

void test_voltage_to_height() {
  calibration.densityRatio = 1.0f;
  calibration.zeroVoltage = Config::LEVEL_VOLTAGE_MIN;
  calibration.fullVoltage = Config::LEVEL_VOLTAGE_MAX;
  float hMid = voltageToHeightCm((calibration.zeroVoltage + calibration.fullVoltage) / 2.0f);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 250.0f, hMid);
  float hTop = voltageToHeightCm(calibration.fullVoltage);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, Config::LEVEL_RANGE_CM, hTop);
}

void setup() {
  UNITY_BEGIN();
  RUN_TEST(test_pulses_to_flow);
  RUN_TEST(test_voltage_to_height);
  UNITY_END();
}

void loop() {}

#endif
