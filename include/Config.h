#pragma once

#include <Arduino.h>

namespace config {

// Flow sensor pin
constexpr gpio_num_t FLOW_SENSOR_PIN = GPIO_NUM_25;

// Analog inputs
constexpr gpio_num_t LEVEL_SENSOR_PIN = GPIO_NUM_36;  // ADC1 channel 0
constexpr gpio_num_t JOYSTICK_X_PIN = GPIO_NUM_32;
constexpr gpio_num_t JOYSTICK_Y_PIN = GPIO_NUM_33;

// Buttons
constexpr gpio_num_t BUTTON1_PIN = GPIO_NUM_14;
constexpr gpio_num_t BUTTON2_PIN = GPIO_NUM_27;

// SD Card pins (VSPI)
constexpr int SD_MOSI = 23;
constexpr int SD_MISO = 19;
constexpr int SD_SCK = 18;
constexpr int SD_CS = 5;

// LCD
constexpr uint8_t LCD_ADDR = 0x27;
constexpr uint8_t LCD_COLS = 16;
constexpr uint8_t LCD_ROWS = 2;

// Sampling and timing
constexpr uint32_t FLOW_WINDOW_MS = 1000;  // sliding window length
constexpr size_t FLOW_STATS_WINDOW = 120;  // last N samples for statistics
constexpr size_t LEVEL_STATS_WINDOW = 120;
constexpr TickType_t SENSOR_TASK_DELAY = pdMS_TO_TICKS(200);
constexpr uint32_t ANALOG_OVERSAMPLE = 10;
constexpr float ANALOG_ALPHA = 0.2f;  // EMA smoothing factor

// Logging
constexpr uint32_t DEFAULT_LOG_INTERVAL_MS = 1000;
constexpr size_t RAM_LOG_CAPACITY = 1200;  // 20 minutes at 1 Hz
constexpr uint64_t SD_MIN_FREE_BYTES = 4ULL * 1024ULL * 1024ULL * 1024ULL;  // 4 GB

// Calibration defaults
constexpr float LEVEL_V_MIN = 0.48f;
constexpr float LEVEL_V_MAX = 2.40f;
constexpr float LEVEL_RANGE_CM = 500.0f;
constexpr float WATER_DENSITY = 1.0f;  // relative

// Joystick
constexpr int JOYSTICK_DEADBAND = 200;
constexpr int JOYSTICK_MAX = 4095;
constexpr float JOYSTICK_ACCEL_THRESHOLD = 0.8f;
constexpr float JOYSTICK_ACCEL_MULTIPLIER = 1.6f;

// Buttons
constexpr uint32_t BUTTON_DEBOUNCE_MS = 25;
constexpr uint32_t BUTTON_HOLD_MS = 5000;

// Queue sizes
constexpr size_t SENSOR_QUEUE_LENGTH = 10;

}  // namespace config

#ifdef DEBUG_PROJECT_KALKAN
constexpr bool kDebugMode = true;
#else
constexpr bool kDebugMode = false;
#endif

