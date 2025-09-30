#pragma once

#include <Arduino.h>

namespace Config {
constexpr gpio_num_t FLOW_SENSOR_PIN = GPIO_NUM_25;
constexpr gpio_num_t LEVEL_SENSOR_PIN = GPIO_NUM_36;  // ADC1_CH0
constexpr gpio_num_t JOYSTICK_X_PIN = GPIO_NUM_32;
constexpr gpio_num_t JOYSTICK_Y_PIN = GPIO_NUM_33;
constexpr gpio_num_t BUTTON1_PIN = GPIO_NUM_14;
constexpr gpio_num_t BUTTON2_PIN = GPIO_NUM_27;
constexpr uint8_t LCD_I2C_ADDRESS = 0x27;
constexpr uint8_t LCD_COLS = 16;
constexpr uint8_t LCD_ROWS = 2;
constexpr uint8_t ADC_OVERSAMPLE = 10;
constexpr float ADC_ATTENUATION_DB = 11.0f;
constexpr uint16_t FLOW_SAMPLE_PERIOD_MS = 100;
constexpr uint16_t LEVEL_SAMPLE_PERIOD_MS = 200;
constexpr uint16_t LOG_INTERVAL_MS = 1000;
constexpr size_t SENSOR_QUEUE_DEPTH = 10;
constexpr size_t LOG_BUFFER_MINUTES = 20;
constexpr size_t LOG_BUFFER_CAPACITY = (LOG_BUFFER_MINUTES * 60 * 2);  // assuming max 2 Hz logging
constexpr float LEVEL_VOLTAGE_MIN = 0.48f;
constexpr float LEVEL_VOLTAGE_MAX = 2.4f;
constexpr float LEVEL_RANGE_CM = 500.0f;
constexpr float EMA_ALPHA = 0.2f;
constexpr float JOYSTICK_DEADBAND = 0.1f;
constexpr float JOYSTICK_ACCEL_THRESHOLD = 0.8f;
constexpr float JOYSTICK_ACCEL_FACTOR = 1.6f;
constexpr TickType_t BUTTON_HOLD_TIME = pdMS_TO_TICKS(5000);
constexpr size_t FLOW_WINDOW_SIZE = 120;   // 12 seconds at 100 ms updates
constexpr size_t LEVEL_WINDOW_SIZE = 120;
constexpr size_t SENSOR_HISTORY_SIZE = 256;
constexpr float LEVEL_NOISE_GOOD = 1.0f;   // percent
constexpr float LEVEL_NOISE_FAIR = 3.0f;
constexpr char const *LOG_DIRECTORY = "/logs";
constexpr char const *EVENT_DIRECTORY = "/events";
constexpr uint64_t SD_FREE_SPACE_THRESHOLD_BYTES = 4ULL * 1024ULL * 1024ULL * 1024ULL;
}  // namespace Config
