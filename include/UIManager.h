#pragma once

#include <LiquidCrystal_I2C.h>

#include "Config.h"
#include "SensorData.h"

class UIManager {
 public:
  enum class Screen {
    Boot,
    TimeSetting,
    DateSetting,
    Main,
    LevelStats,
    FlowStats,
    Calibration
  };

  UIManager(LiquidCrystal_I2C& lcd);

  void begin();
  void setScreen(Screen screen);
  Screen currentScreen() const { return screen_; }
  void update(const AnalyticsState& state, const SensorSnapshot& latest);
  void handleJoystick(float x, float y);
  void handleButtons(bool button1Pressed, bool button2Pressed, bool bothHeld);
  bool calibrationRequested() const { return calibrationRequested_; }
  void setCalibrationValue(float value);
  float calibrationInputValue() const { return calibrationInput_; }
  void resetCalibrationRequest();
  void setTimeSetting(struct tm timeinfo);
  struct tm editableTime() const { return editableTime_; }
  void commitTime(struct tm timeinfo);

 private:
  void createCustomChars();
  void renderBoot();
  void renderTimeSetting();
  void renderDateSetting();
  void renderMain(const AnalyticsState& state);
  void renderStats(const StatisticsSummary& stats, const char* title);
  void renderCalibration();
  void advanceMainScroll();
  String buildFlowMetricString(const AnalyticsState& state) const;
  String buildLevelMetricString(const AnalyticsState& state) const;
  void pushToLcd(uint8_t row, const String& content);
  void updateTimeEditing(int digitIndex, int delta);
  void updateDateEditing(int fieldIndex, int delta);

  LiquidCrystal_I2C& lcd_;
  Screen screen_ = Screen::Boot;
  uint32_t bootStartMs_ = 0;
  uint32_t lastScrollMs_ = 0;
  uint8_t scrollIndex_ = 0;
  String lastRow0_;
  String lastRow1_;
  bool calibrationRequested_ = false;
  float calibrationInput_ = 0.0f;
  struct tm editableTime_ {};
  uint8_t timeCursor_ = 0;
  uint8_t dateField_ = 0;
};

