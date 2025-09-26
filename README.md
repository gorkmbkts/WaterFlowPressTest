# Project Kalkan - Water Flow and Pressure Monitoring System

A comprehensive ESP32-based dual-core water flow and pressure monitoring system with real-time analytics, data logging, and Turkish language LCD interface.

## Features

### Hardware Support
- **YF-DN50 Hall-effect flow sensor** with interrupt-based pulse counting
- **Gravity KIT0139 pressure sensor** with 12-bit ADC and calibration
- **16x2 I2C LCD** with custom Greek characters (μ, η, θ, Σ, etc.)
- **Analog joystick** with deadband and acceleration
- **Two momentary buttons** with debouncing
- **SD card logging** via SPI interface
- **Real-time clock** using ESP32 built-in RTC

### Software Architecture
- **Dual-core FreeRTOS implementation**
  - Core 0: Sensor task with interrupt handling
  - Core 1: UI task with LCD management
- **Thread-safe data structures** with mutexes and queues
- **Circular buffers** for efficient memory usage
- **Real-time analytics** with running statistics

### User Interface
- **Turkish language support** for date/time display
- **Boot screen** with initialization status
- **Time/Date setting screens** with joystick navigation
- **Main screen** with scrolling metrics display
- **Statistics screens** for flow and pressure data
- **Calibration screen** for sensor adjustment

### Data Analytics
- **Flow metrics**: instantaneous, baseline, mean, median, percentiles
- **Pressure metrics**: height calculation, noise analysis, baselines
- **Pump detection** based on flow thresholds
- **Signal quality monitoring** for pressure sensor

### Logging System
- **Continuous logging** to daily CSV files
- **Event logging** with 20-minute pre-event buffer
- **Automatic space management** with old file cleanup
- **Robust error handling** with SD card recovery

## Hardware Connections

### ESP32 Pin Assignments
```
Flow Sensor:     GPIO25 (with voltage divider 5V→3.3V)
Pressure Sensor: GPIO36 (ADC1_CH0)
Joystick X:      GPIO32 (ADC1_CH4)
Joystick Y:      GPIO33 (ADC1_CH5)
Button 1:        GPIO14 (internal pull-up)
Button 2:        GPIO27 (internal pull-up)
SD Card SPI:     MOSI=23, MISO=19, SCK=18, CS=5
I2C LCD:         SDA=21, SCL=22 (default I2C pins)
```

### Circuit Notes
- Flow sensor requires voltage divider: 5V signal → 3.3V logic level
- Pressure sensor: 4-20mA → 0.48-2.4V converter module
- LCD uses I2C backpack at address 0x27
- All inputs use internal pull-ups where applicable

## Software Setup

### PlatformIO Configuration
```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
lib_deps = 
    marcoschwartz/LiquidCrystal_I2C@^1.1.4
    greiman/SdFat@^2.2.3
    thomasfredericks/Bounce2@^2.71
    bblanchon/ArduinoJson@^7.0.4
```

### Build and Upload
```bash
# Build the project
pio run

# Upload to ESP32
pio run --target upload

# Monitor serial output
pio device monitor
```

### Testing
```bash
# Run unit tests
pio test --environment test
```

## Usage

### Initial Setup
1. **Power on**: System shows "Project Kalkan" boot screen
2. **Set time**: Use joystick to set current time (24-hour format)
3. **Set date**: Use joystick to set current date (Turkish month names)
4. **Main operation**: System automatically starts monitoring and logging

### Navigation
- **Joystick X-axis**: Navigate between screens (Main → Flow Stats → Pressure Stats)
- **Joystick Y-axis**: Adjust values in setting screens
- **Button 1**: Trigger event logging
- **Both buttons (5s hold)**: Enter calibration mode

### Screen Descriptions

#### Main Screen
```
FLOW: Q=1.23L/s Qₙ=1.20L/s...
TANK: h=45.2cm hθ=5.1cm...
```
Scrolling display showing all metrics with Greek symbols.

#### Flow Statistics Screen
```
   Flow Statistics   
Avg:1.15 Med:1.18
```

#### Pressure Statistics Screen
```
  Tank Statistics    
Avg:42.3 SD:2.1
```

### Calibration
1. Hold both buttons for 5 seconds
2. Enter actual water depth using joystick
3. System updates calibration offset
4. Return to main screen

## Data Logging

### File Structure
```
/logs/
  ├── 2025-01-15.csv    # Daily continuous logs
  ├── 2025-01-16.csv
  └── ...
/events/
  ├── event_2025-01-15T14-32-10.csv    # Event snapshots
  └── ...
```

### CSV Format
```csv
Timestamp,DateTime,PulseCount,FlowFreq,FlowRate,PressureV,WaterHeight,FlowMean,FlowMedian,FlowMin,FlowMax,FlowBaseline,PressureMean,PressureMedian,PressureMin,PressureMax,SignalQuality
1642248730,2025-01-15T14:32:10,1234,12.5,1.04,1.85,47.3,1.02,1.03,0.95,1.15,1.12,46.8,47.1,45.2,48.9,3.2
```

## Configuration

### Sensor Calibration
- **Flow sensor**: Factory calibrated (0.2 Hz per L/min)
- **Pressure sensor**: User calibratable via UI
- **Density compensation**: Adjustable for different liquids

### System Parameters
```cpp
#define SENSOR_TASK_FREQ   100    // Hz
#define UI_TASK_FREQ       20     // Hz
#define LOG_INTERVAL_MS    1000   // Default logging interval
#define FLOW_WINDOW_SIZE   100    // Samples for statistics
#define PRESSURE_WINDOW_SIZE 100
#define EVENT_BUFFER_MINUTES 20   // Pre-event data retention
```

## Troubleshooting

### Common Issues
1. **LCD not displaying**: Check I2C connections and address (0x27)
2. **No flow readings**: Verify voltage divider and GPIO25 connection
3. **Pressure readings unstable**: Check 4-20mA converter and GPIO36
4. **SD card errors**: Ensure proper SPI wiring and card formatting (FAT32)
5. **Time resets**: Set time/date after each power cycle

### Debug Mode
Enable debug output by setting `DEBUG_MODE=1` in build flags:
```cpp
#define DEBUG_MODE 1
```

### Memory Usage
- **Heap usage**: ~50KB for buffers and statistics
- **Stack usage**: 8KB per task (sensor + UI)
- **Flash usage**: ~1MB for code and libraries

## Performance Specifications

### Sensor Performance
- **Flow rate range**: 0-100 L/s (limited by sensor)
- **Flow accuracy**: ±2% (sensor dependent)
- **Pressure range**: 0-500 cm water column
- **Pressure resolution**: 12-bit ADC (~0.1 cm)
- **Update rate**: 100 Hz sensor sampling, 20 Hz display

### System Performance
- **Response time**: <100ms for user inputs
- **Data throughput**: 1 reading/second continuous logging
- **SD card speed**: ~100 KB/s write speed
- **Memory efficiency**: Circular buffers prevent memory fragmentation

## Development Notes

### Code Structure
```
src/
├── main.cpp              # Main application and task coordination
├── sensor_manager.cpp    # Flow/pressure sensor handling
├── lcd_manager.cpp       # Display and UI management
├── input_manager.cpp     # Joystick and button handling
├── sd_manager.cpp        # Data logging and file management
└── time_manager.cpp      # RTC and time formatting

include/
├── config.h              # Hardware pins and constants
├── sensor_data.h         # Data structures and buffers
└── *.h                   # Manager class headers
```

### Adding New Features
1. **New sensors**: Extend `SensorManager` class
2. **UI screens**: Add to `UIScreen` enum and `LCDManager`
3. **Data fields**: Update `SensorReading` structure and CSV format
4. **Analytics**: Add to analytics structures and calculation functions

## License

This project is developed for industrial water monitoring applications. Please ensure proper electrical safety when working with pumps and water systems.

## Support

For technical issues or feature requests, please create an issue in the project repository with:
- Hardware configuration details
- Serial debug output (if available)
- Description of expected vs. actual behavior