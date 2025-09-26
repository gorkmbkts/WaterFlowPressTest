#ifndef CONFIG_H
#define CONFIG_H

// Hardware pin assignments
#define FLOW_PIN           25    // Hall effect flow sensor (with voltage divider)
#define PRESSURE_PIN       36    // Pressure sensor analog input (ADC1)
#define JOYSTICK_X_PIN     32    // Joystick X-axis
#define JOYSTICK_Y_PIN     33    // Joystick Y-axis
#define BUTTON1_PIN        14    // Button 1 (event logging)
#define BUTTON2_PIN        27    // Button 2 (calibration when both pressed)

// SD Card SPI pins (VSPI)
#define SD_MOSI_PIN        23
#define SD_MISO_PIN        19
#define SD_SCK_PIN         18
#define SD_CS_PIN          5

// I2C LCD address
#define LCD_ADDRESS        0x27
#define LCD_COLS           16
#define LCD_ROWS           2

// Sensor calibration constants
#define PRESSURE_V_MIN     0.48f  // Voltage at 4mA (0m water column)
#define PRESSURE_V_MAX     2.4f   // Voltage at 20mA (5m water column)
#define PRESSURE_HEIGHT_MAX 500.0f // Max height in cm (5m)
#define FLOW_FREQ_FACTOR   0.2f   // Hz per L/min from datasheet
#define FLOW_CONVERSION    (1.0f/12.0f) // Convert Hz to L/s (f/0.2/60)

// System timing
#define SENSOR_TASK_FREQ   100    // Hz
#define UI_TASK_FREQ       20     // Hz
#define LOG_INTERVAL_MS    1000   // Default logging interval
#define BOOT_DISPLAY_MS    5000   // Boot screen display time
#define SCROLL_DELAY_MS    2000   // Character scroll delay
#define CALIBRATION_HOLD_MS 5000  // Button hold time for calibration

// Buffer sizes
#define FLOW_WINDOW_SIZE   100    // Samples for rolling statistics
#define PRESSURE_WINDOW_SIZE 100
#define EVENT_BUFFER_MINUTES 20   // Minutes of data to keep for events
#define EVENT_BUFFER_SIZE  (EVENT_BUFFER_MINUTES * 60 * 1000 / LOG_INTERVAL_MS)

// ADC settings
#define ADC_SAMPLES        10     // Oversampling count
#define JOYSTICK_DEADBAND  0.1f   // Joystick deadband
#define JOYSTICK_ACCEL_THRESHOLD 0.8f // Acceleration threshold
#define JOYSTICK_ACCEL_FACTOR 1.6f    // Acceleration multiplier

// SD card management
#define MIN_FREE_SPACE_GB  4      // Minimum free space in GB
#define MIN_FREE_SPACE_BYTES (MIN_FREE_SPACE_GB * 1024ULL * 1024ULL * 1024ULL)

// Custom LCD characters (Greek symbols)
#define CHAR_MU      0  // μ
#define CHAR_ETA     1  // η  
#define CHAR_THETA   2  // θ
#define CHAR_SIGMA   3  // Σ
#define CHAR_OMEGA   4  // Ω
#define CHAR_ALPHA   5  // α
#define CHAR_BETA    6  // β
#define CHAR_GAMMA   7  // γ

// Debug mode
#ifdef DEBUG_MODE
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#endif

#endif // CONFIG_H