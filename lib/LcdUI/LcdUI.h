#pragma once

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <functional>
#include <vector>

#include <../Utils/Utils.h>
#include "../Buttons/Buttons.h"
#include "../Joystick/Joystick.h"

class SdLogger;
class ConfigService;

class LcdUI {
  public:
    using CalibrationCallback = std::function<void(float)>;

    void begin(LiquidCrystal_I2C* lcd, Buttons* buttons, Joystick* joystick, SdLogger* logger, ConfigService* config);
    void update();
    void setMetrics(const utils::SensorMetrics& metrics);
    void setCalibrationCallback(CalibrationCallback cb);

  private:
    enum class ScreenState {
        Boot,
        SetTime,
        SetDate,
        Main,
        LevelStats,
        FlowStats,
        Calibration
    };

    struct ScrollState {
        std::vector<String> flowLines;
        std::vector<String> tankLines;
        size_t flowIndex = 0;
        size_t tankIndex = 0;
        size_t flowOffset = 0;
        size_t tankOffset = 0;
        unsigned long lastScrollMillis = 0;
        unsigned long scrollInterval = 2000;
    };

    struct DateTimeEditor {
        struct tm tmData;
        uint8_t cursorIndex = 0;
        bool editingTime = true;
    };

    struct CalibrationEditor {
        float value = 0.0f;
        uint8_t cursorIndex = 0;
        bool active = false;
    };

    void renderBoot();
    void renderTimeEditor();
    void renderDateEditor();
    void renderMainScreen();
    void renderLevelStats();
    void renderFlowStats();
    void renderCalibration();
    void renderScrollLine(uint8_t row, const String& label, const std::vector<String>& items, size_t index, size_t offset);
    void ensureCustomGlyphs();
    void updateScrollState();
    void rebuildScrollBuffers();
    void transition(ScreenState next);
    void handleTimeEditing(float joyX, float joyY);
    void handleDateEditing(float joyX, float joyY);
    void handleMainNavigation(float joyX);
    void applyDateTime();
    void updateCalibration(float joyX, float joyY);

    LiquidCrystal_I2C* _lcd = nullptr;
    Buttons* _buttons = nullptr;
    Joystick* _joystick = nullptr;
    SdLogger* _logger = nullptr;
    ConfigService* _config = nullptr;
    CalibrationCallback _calibrationCallback;

    ScreenState _state = ScreenState::Boot;
    unsigned long _bootStart = 0;
    bool _glyphsReady = false;
    bool _hasMetrics = false;
    utils::SensorMetrics _metrics;
    ScrollState _scroll;
    DateTimeEditor _editor;
    CalibrationEditor _calEditor;
    time_t _lastMetricsTimestamp = 0;
    unsigned long _lastInputMillis = 0;

    uint8_t _glyphMu = 0;
    uint8_t _glyphEta = 1;
    uint8_t _glyphTheta = 2;
    uint8_t _glyphSigma = 3;
};

