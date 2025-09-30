#include <Arduino.h>
#include <unity.h>

#include "Config.h"
#include "Conversions.h"

void test_flow_conversion() {
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, convertPulseToFlowLps(12.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.5f, convertPulseToFlowLps(6.0f));
}

void test_voltage_to_height() {
  CalibrationFactors cal;
  cal.vMin = 0.48f;
  cal.vMax = 2.4f;
  cal.referenceHeightCm = 500.0f;
  cal.densityRatio = 1.0f;

  float midVoltage = (cal.vMin + cal.vMax) / 2.0f;
  float height = convertVoltageToHeight(midVoltage, cal);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 250.0f, height);
}

void setup() {
  UNITY_BEGIN();
  RUN_TEST(test_flow_conversion);
  RUN_TEST(test_voltage_to_height);
  UNITY_END();
}

void loop() {}

