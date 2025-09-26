#include "lcd_manager.h"
#include "time_manager.h"
#include "sensor_manager.h"
#include <stdio.h>

// External references
extern TimeManager timeManager;
extern SensorManager sensorManager;

// Custom character definitions for Greek symbols
const uint8_t char_mu[8] = {     // μ (mu)
  0b00000,
  0b00000,
  0b10001,
  0b10001,
  0b10001,
  0b11001,
  0b10000,
  0b10000
};

const uint8_t char_eta[8] = {    // η (eta)
  0b00000,
  0b00000,
  0b10010,
  0b11110,
  0b10010,
  0b10010,
  0b10010,
  0b00000
};

const uint8_t char_theta[8] = {  // θ (theta)
  0b00000,
  0b01110,
  0b10001,
  0b11111,
  0b10001,
  0b10001,
  0b01110,
  0b00000
};

const uint8_t char_sigma[8] = {  // Σ (sigma)
  0b00000,
  0b11111,
  0b10000,
  0b11110,
  0b10000,
  0b10000,
  0b11111,
  0b00000
};

const uint8_t char_omega[8] = {  // Ω (omega)
  0b00000,
  0b01110,
  0b10001,
  0b10001,
  0b10001,
  0b01010,
  0b11011,
  0b00000
};

const uint8_t char_alpha[8] = {  // α (alpha)
  0b00000,
  0b00000,
  0b01110,
  0b00001,
  0b01111,
  0b10001,
  0b01111,
  0b00000
};

const uint8_t char_beta[8] = {   // β (beta)
  0b00000,
  0b10000,
  0b10000,
  0b11110,
  0b10001,
  0b11110,
  0b10000,
  0b10000
};

const uint8_t char_gamma[8] = {  // γ (gamma)
  0b00000,
  0b00000,
  0b11111,
  0b10000,
  0b10000,
  0b10000,
  0b10000,
  0b00000
};

LCDManager::LCDManager() : lcd(LCD_ADDRESS, LCD_COLS, LCD_ROWS) {
    current_screen = SCREEN_BOOT;
    previous_screen = SCREEN_BOOT;
    
    // Initialize display buffers
    clearDisplayBuffer();
    
    // Initialize time state
    memset(&time_state, 0, sizeof(time_state));
    time_state.hour = 12;
    time_state.minute = 0;
    time_state.day = 1;
    time_state.month = 1;
    time_state.year = 2025;
    
    // Initialize scroll states
    flow_scroll.active = false;
    flow_scroll.position = 0;
    flow_scroll.last_scroll = 0;
    tank_scroll.active = false;
    tank_scroll.position = 0;
    tank_scroll.last_scroll = 0;
}

bool LCDManager::initialize() {
    DEBUG_PRINTLN(F("Initializing LCD..."));
    
    lcd.init();
    lcd.backlight();
    lcd.clear();
    
    // Test LCD communication
    lcd.setCursor(0, 0);
    lcd.print(F("LCD Test"));
    delay(500);
    
    createCustomCharacters();
    clearDisplayBuffer();
    
    DEBUG_PRINTLN(F("LCD initialized"));
    return true;
}

void LCDManager::createCustomCharacters() {
    lcd.createChar(CHAR_MU, (uint8_t*)char_mu);
    lcd.createChar(CHAR_ETA, (uint8_t*)char_eta);
    lcd.createChar(CHAR_THETA, (uint8_t*)char_theta);
    lcd.createChar(CHAR_SIGMA, (uint8_t*)char_sigma);
    lcd.createChar(CHAR_OMEGA, (uint8_t*)char_omega);
    lcd.createChar(CHAR_ALPHA, (uint8_t*)char_alpha);
    lcd.createChar(CHAR_BETA, (uint8_t*)char_beta);
    lcd.createChar(CHAR_GAMMA, (uint8_t*)char_gamma);
}

void LCDManager::setScreen(UIScreen screen) {
    if (screen != current_screen) {
        previous_screen = current_screen;
        current_screen = screen;
        clearDisplayBuffer();
        
        // Reset scroll states when changing screens
        flow_scroll.active = false;
        flow_scroll.position = 0;
        tank_scroll.active = false;
        tank_scroll.position = 0;
    }
}

void LCDManager::showBootScreen() {
    current_screen = SCREEN_BOOT;
    clearDisplayBuffer();
    centerText("Project Kalkan", 0);
    setBufferString(1, 0, "Initializing...");
    updateChangedCharacters();
}

void LCDManager::update(const FlowAnalytics& flow, const PressureAnalytics& pressure) {
    switch (current_screen) {
        case SCREEN_BOOT:
            renderBootScreen();
            break;
        case SCREEN_TIME_SET:
            renderTimeSetScreen();
            break;
        case SCREEN_DATE_SET:
            renderDateSetScreen();
            break;
        case SCREEN_MAIN:
            renderMainScreen(flow, pressure);
            break;
        case SCREEN_FLOW_STATS:
            renderFlowStatsScreen(flow);
            break;
        case SCREEN_PRESSURE_STATS:
            renderPressureStatsScreen(pressure);
            break;
        case SCREEN_CALIBRATION:
            renderCalibrationScreen();
            break;
    }
    
    updateChangedCharacters();
}

void LCDManager::renderBootScreen() {
    static uint32_t last_update = 0;
    static int dots = 0;
    
    if (millis() - last_update > 500) {
        centerText("Project Kalkan", 0);
        
        char loading[17] = "Loading";
        for (int i = 0; i < dots; i++) {
            if (7 + i < 16) loading[7 + i] = '.';
        }
        loading[7 + dots] = '\0';
        
        setBufferString(1, 0, loading);
        
        dots = (dots + 1) % 4;
        last_update = millis();
    }
}

void LCDManager::renderTimeSetScreen() {
    centerText("Zamani Ayarla", 0);
    
    char time_str[17];
    formatTime(time_str, time_state.hour, time_state.minute);
    
    // Add blinking cursor
    static uint32_t last_blink = 0;
    if (millis() - last_blink > 500) {
        time_state.blink_state = !time_state.blink_state;
        last_blink = millis();
    }
    
    // Show cursor at current position
    if (!time_state.blink_state) {
        int cursor_pos = (time_state.cursor_pos < 2) ? time_state.cursor_pos : time_state.cursor_pos + 1;
        time_str[cursor_pos] = '_';
    }
    
    centerText(time_str, 1);
}

void LCDManager::renderDateSetScreen() {
    centerText("Tarihi Ayarla", 0);
    
    char date_str[17];
    formatDate(date_str, time_state.day, time_state.month, time_state.year);
    
    // Add blinking cursor (simplified)
    static uint32_t last_blink = 0;
    if (millis() - last_blink > 500) {
        time_state.blink_state = !time_state.blink_state;
        last_blink = millis();
    }
    
    centerText(date_str, 1);
}

void LCDManager::renderMainScreen(const FlowAnalytics& flow, const PressureAnalytics& pressure) {
    // Prepare scrolling text for flow metrics
    if (!flow_scroll.active) {
        char flow_metrics[200];
        snprintf(flow_metrics, sizeof(flow_metrics), 
            "Q=%.2fL/s Qn=%.2fL/s Qdif=%+.0f%% Qmin=%.2fL/s Q%c=%.2fL/s Q%c=%.2fL/s",
            flow.instantaneous, flow.healthy_baseline, flow.difference_percent,
            flow.minimum_healthy, CHAR_MU, flow.mean, CHAR_ETA, flow.median);
        flow_scroll.text = String(flow_metrics);
        flow_scroll.active = true;
        flow_scroll.position = 0;
    }
    
    // Prepare scrolling text for tank metrics
    if (!tank_scroll.active) {
        char tank_metrics[200];
        char quality[10];
        if (pressure.signal_quality < 5.0f) strcpy(quality, "good");
        else if (pressure.signal_quality < 15.0f) strcpy(quality, "fair");
        else strcpy(quality, "poor");
        
        snprintf(tank_metrics, sizeof(tank_metrics), 
            "h=%.1fcm h%c=%.1fcm h%c=%.1fcm hdif=%+.0f%% signal:%s",
            pressure.instantaneous, CHAR_THETA, pressure.empty_baseline, 
            CHAR_SIGMA, pressure.full_height, pressure.difference_percent, quality);
        tank_scroll.text = String(tank_metrics);
        tank_scroll.active = true;
        tank_scroll.position = 0;
    }
    
    // Static labels
    setBufferString(0, 0, "FLOW:");
    setBufferString(1, 0, "TANK:");
    
    // Update scrolling text
    updateScrollingText(flow_scroll, 0, 5, 11);
    updateScrollingText(tank_scroll, 1, 5, 11);
}

void LCDManager::renderFlowStatsScreen(const FlowAnalytics& flow) {
    centerText("Flow Statistics", 0);
    
    char stats[17];
    snprintf(stats, sizeof(stats), "Avg:%.2f Med:%.2f", flow.mean, flow.median);
    setBufferString(1, 0, stats);
}

void LCDManager::renderPressureStatsScreen(const PressureAnalytics& pressure) {
    centerText("Tank Statistics", 0);
    
    char stats[17];
    snprintf(stats, sizeof(stats), "Avg:%.1f SD:%.1f", 
        pressure.stats.mean, pressure.stats.std_dev);
    setBufferString(1, 0, stats);
}

void LCDManager::renderCalibrationScreen() {
    centerText("Calibration", 0);
    setBufferString(1, 0, "Enter height(cm)");
}

void LCDManager::updateScrollingText(ScrollState& scroll, int row, int start_col, int max_width) {
    if (!scroll.active || scroll.text.length() == 0) return;
    
    // Check if scrolling is needed
    if (scroll.text.length() <= max_width) {
        // Text fits, no scrolling needed
        setBufferString(row, start_col, scroll.text.c_str());
        return;
    }
    
    // Update scroll position
    if (millis() - scroll.last_scroll > SCROLL_DELAY_MS) {
        scroll.position++;
        if (scroll.position >= scroll.text.length()) {
            scroll.position = 0;
        }
        scroll.last_scroll = millis();
    }
    
    // Extract visible portion
    String visible = scroll.text.substring(scroll.position);
    if (visible.length() < max_width) {
        visible += " " + scroll.text.substring(0, max_width - visible.length());
    }
    visible = visible.substring(0, max_width);
    
    // Set the text in buffer
    for (int i = 0; i < max_width && i < visible.length(); i++) {
        char c = visible.charAt(i);
        if (c >= CHAR_MU && c <= CHAR_GAMMA) {
            // Special handling for custom characters would be needed
            setBufferChar(row, start_col + i, c);
        } else {
            setBufferChar(row, start_col + i, c);
        }
    }
}

void LCDManager::centerText(const char* text, int row) {
    int len = strlen(text);
    int start_col = (LCD_COLS - len) / 2;
    if (start_col < 0) start_col = 0;
    
    // Clear the row first
    for (int i = 0; i < LCD_COLS; i++) {
        setBufferChar(row, i, ' ');
    }
    
    setBufferString(row, start_col, text);
}

void LCDManager::formatTime(char* buffer, int hour, int minute) {
    snprintf(buffer, 6, "%02d:%02d", hour, minute);
}

void LCDManager::formatDate(char* buffer, int day, int month, int year) {
    snprintf(buffer, 17, "%d %s %d", day, turkish_months[month-1], year);
}

void LCDManager::updateTimeSet(int hour, int minute, int cursor_pos) {
    time_state.hour = hour;
    time_state.minute = minute;
    time_state.cursor_pos = cursor_pos;
}

void LCDManager::updateDateSet(int day, int month, int year, int cursor_pos) {
    time_state.day = day;
    time_state.month = month;
    time_state.year = year;
    time_state.cursor_pos = cursor_pos;
}

void LCDManager::clearDisplayBuffer() {
    for (int row = 0; row < LCD_ROWS; row++) {
        for (int col = 0; col < LCD_COLS; col++) {
            display_buffer[row][col] = ' ';
            previous_buffer[row][col] = '\0'; // Force update
        }
        display_buffer[row][LCD_COLS] = '\0';
        previous_buffer[row][LCD_COLS] = '\0';
    }
}

void LCDManager::setBufferChar(int row, int col, char c) {
    if (row >= 0 && row < LCD_ROWS && col >= 0 && col < LCD_COLS) {
        display_buffer[row][col] = c;
    }
}

void LCDManager::setBufferString(int row, int col, const char* str) {
    int len = strlen(str);
    for (int i = 0; i < len && (col + i) < LCD_COLS; i++) {
        setBufferChar(row, col + i, str[i]);
    }
}

void LCDManager::updateChangedCharacters() {
    for (int row = 0; row < LCD_ROWS; row++) {
        for (int col = 0; col < LCD_COLS; col++) {
            if (display_buffer[row][col] != previous_buffer[row][col]) {
                lcd.setCursor(col, row);
                
                // Handle custom characters
                char c = display_buffer[row][col];
                if (c >= CHAR_MU && c <= CHAR_GAMMA) {
                    lcd.write((uint8_t)c);
                } else {
                    lcd.write(c);
                }
                
                previous_buffer[row][col] = display_buffer[row][col];
            }
        }
    }
}

void LCDManager::showMessage(const char* line1, const char* line2, int duration_ms) {
    UIScreen saved_screen = current_screen;
    
    clearDisplayBuffer();
    centerText(line1, 0);
    if (line2) {
        centerText(line2, 1);
    }
    updateChangedCharacters();
    
    delay(duration_ms);
    
    current_screen = saved_screen;
    clearDisplayBuffer();
}

void LCDManager::showError(const char* error) {
    showMessage("ERROR", error, 3000);
}

void LCDManager::uiTask(void* parameter) {
    LCDManager* manager = (LCDManager*)parameter;
    manager->runUITask();
}

void LCDManager::runUITask() {
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t task_period = pdMS_TO_TICKS(1000 / UI_TASK_FREQ);
    
    DEBUG_PRINTLN(F("UI task started on core 1"));
    
    while (true) {
        // Get latest sensor data
        FlowAnalytics flow = sensorManager.getFlowAnalytics();
        PressureAnalytics pressure = sensorManager.getPressureAnalytics();
        
        // Update display
        update(flow, pressure);
        
        vTaskDelayUntil(&last_wake_time, task_period);
    }
}