#ifndef LCD_MANAGER_H
#define LCD_MANAGER_H

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include "config.h"
#include "sensor_data.h"

// UI screens enumeration
enum UIScreen {
    SCREEN_BOOT,
    SCREEN_TIME_SET,
    SCREEN_DATE_SET,
    SCREEN_MAIN,
    SCREEN_FLOW_STATS,
    SCREEN_PRESSURE_STATS,
    SCREEN_CALIBRATION
};

// Time/Date setting state
struct TimeSetState {
    int hour;
    int minute;
    int day;
    int month;
    int year;
    int cursor_pos;
    bool blink_state;
    uint32_t last_blink;
};

// Scrolling text state
struct ScrollState {
    String text;
    int position;
    uint32_t last_scroll;
    bool active;
};

class LCDManager {
private:
    LiquidCrystal_I2C lcd;
    UIScreen current_screen;
    UIScreen previous_screen;
    
    // UI state
    TimeSetState time_state;
    ScrollState flow_scroll;
    ScrollState tank_scroll;
    
    // Display buffers to minimize flicker
    char display_buffer[LCD_ROWS][LCD_COLS + 1];
    char previous_buffer[LCD_ROWS][LCD_COLS + 1];
    
    // Turkish month names
    const char* turkish_months[12] = {
        "Ocak", "Subat", "Mart", "Nisan", "Mayis", "Haziran",
        "Temmuz", "Agustos", "Eylul", "Ekim", "Kasim", "Aralik"
    };
    
    // Private methods
    void createCustomCharacters();
    void updateDisplayBuffer();
    void renderBootScreen();
    void renderTimeSetScreen();
    void renderDateSetScreen();
    void renderMainScreen(const FlowAnalytics& flow, const PressureAnalytics& pressure);
    void renderFlowStatsScreen(const FlowAnalytics& flow);
    void renderPressureStatsScreen(const PressureAnalytics& pressure);
    void renderCalibrationScreen();
    
    void updateScrollingText(ScrollState& scroll, int row, int start_col, int max_width);
    void centerText(const char* text, int row);
    void formatNumber(char* buffer, float value, int decimals, const char* unit = "");
    void formatPercentage(char* buffer, float value);
    void formatTime(char* buffer, int hour, int minute);
    void formatDate(char* buffer, int day, int month, int year);
    
    // Character-level update to minimize flicker
    void updateChangedCharacters();
    void clearDisplayBuffer();
    void setBufferChar(int row, int col, char c);
    void setBufferString(int row, int col, const char* str);
    
public:
    LCDManager();
    
    // Initialization
    bool initialize();
    
    // Screen management
    void setScreen(UIScreen screen);
    UIScreen getCurrentScreen() { return current_screen; }
    
    // Update methods
    void update(const FlowAnalytics& flow, const PressureAnalytics& pressure);
    void updateTimeSet(int hour, int minute, int cursor_pos);
    void updateDateSet(int day, int month, int year, int cursor_pos);
    void updateCalibration(float current_value, int cursor_pos);
    
    // Boot sequence
    void showBootScreen();
    bool isBootComplete();
    
    // Utility
    void showMessage(const char* line1, const char* line2 = nullptr, int duration_ms = 2000);
    void showError(const char* error);
    void setBrightness(uint8_t brightness);
    
    // Custom character support
    void printCustomChar(uint8_t char_code);
    
    // Task function
    static void uiTask(void* parameter);
    void runUITask();
};

// Global instance declaration
extern LCDManager lcdManager;

#endif // LCD_MANAGER_H