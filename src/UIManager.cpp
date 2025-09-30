#include "UIManager.h"

#include <cmath>

namespace {
const char* kMonthNames[] = {"Ocak",  "Şubat", "Mart",   "Nisan",  "Mayıs",  "Haziran",
                              "Temmuz", "Ağustos", "Eylül", "Ekim",  "Kasım", "Aralık"};

const uint8_t kMuChar[8] = {0b00000, 0b00000, 0b10001, 0b10001, 0b10001, 0b10011, 0b10101,
                            0b10000};
const uint8_t kEtaChar[8] = {0b00100, 0b00100, 0b00100, 0b00110, 0b00101, 0b00101, 0b11111,
                             0b00000};
const uint8_t kThetaChar[8] = {0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b01110,
                               0b00000};
const uint8_t kSigmaChar[8] = {0b11111, 0b10000, 0b01000, 0b00100, 0b01000, 0b10000, 0b11111,
                               0b00000};

String formatTwoDigit(int value) {
  char buffer[3];
  snprintf(buffer, sizeof(buffer), "%02d", value);
  return String(buffer);
}

}  // namespace

UIManager::UIManager(LiquidCrystal_I2C& lcd) : lcd_(lcd) {}

void UIManager::begin() {
  lcd_.init();
  lcd_.backlight();
  createCustomChars();
  bootStartMs_ = millis();
  setScreen(Screen::Boot);
}

void UIManager::createCustomChars() {
  lcd_.createChar(1, const_cast<uint8_t*>(kMuChar));
  lcd_.createChar(2, const_cast<uint8_t*>(kEtaChar));
  lcd_.createChar(3, const_cast<uint8_t*>(kThetaChar));
  lcd_.createChar(4, const_cast<uint8_t*>(kSigmaChar));
}

void UIManager::setScreen(Screen screen) {
  screen_ = screen;
  lastRow0_ = "";
  lastRow1_ = "";
  lcd_.clear();
}

void UIManager::update(const AnalyticsState& state, const SensorSnapshot& latest) {
  switch (screen_) {
    case Screen::Boot:
      renderBoot();
      if (millis() - bootStartMs_ > 5000) {
        setScreen(Screen::TimeSetting);
      }
      break;
    case Screen::TimeSetting:
      renderTimeSetting();
      break;
    case Screen::DateSetting:
      renderDateSetting();
      break;
    case Screen::Main:
      renderMain(state);
      break;
    case Screen::LevelStats:
      renderStats(state.levelStats, "TANK IST");
      break;
    case Screen::FlowStats:
      renderStats(state.flowStats, "FLOW IST");
      break;
    case Screen::Calibration:
      renderCalibration();
      break;
  }
}

void UIManager::renderBoot() {
  lcd_.noBlink();
  pushToLcd(0, "Project Kalkan");
  pushToLcd(1, "Hazirlaniyor...");
}

void UIManager::renderTimeSetting() {
  pushToLcd(0, "  Zamanı Ayarla  ");
  int hour = editableTime_.tm_hour;
  int minute = editableTime_.tm_min;
  String line = "    " + formatTwoDigit(hour) + ":" + formatTwoDigit(minute) + "    ";
  pushToLcd(1, line);
  lcd_.setCursor(4 + timeCursor_ + (timeCursor_ >= 2 ? 1 : 0), 1);
  lcd_.blink();
}

void UIManager::renderDateSetting() {
  pushToLcd(0, "  Tarihi Ayarla  ");
  int day = editableTime_.tm_mday;
  int month = editableTime_.tm_mon;
  int year = editableTime_.tm_year + 1900;
  String line = String(day) + " " + kMonthNames[month] + " " + String(year);
  if (line.length() < 16) {
    line += String(' ', 16 - line.length());
  }
  pushToLcd(1, line);
  lcd_.setCursor(0, 1);
  lcd_.noBlink();
}

void UIManager::renderMain(const AnalyticsState& state) {
  lcd_.noBlink();
  advanceMainScroll();
  String flowMetrics = buildFlowMetricString(state);
  String levelMetrics = buildLevelMetricString(state);
  while (flowMetrics.length() < 40) {
    flowMetrics += flowMetrics;
  }
  while (levelMetrics.length() < 40) {
    levelMetrics += levelMetrics;
  }
  size_t flowSpan = (flowMetrics.length() > 10) ? flowMetrics.length() - 10 : flowMetrics.length();
  if (flowSpan == 0) flowSpan = 1;
  size_t scrollPos = scrollIndex_ % flowSpan;
  String row0 = "FLOW:" + flowMetrics.substring(scrollPos, scrollPos + 11);
  size_t levelSpan = (levelMetrics.length() > 10) ? levelMetrics.length() - 10 : levelMetrics.length();
  if (levelSpan == 0) levelSpan = 1;
  size_t scrollPosLevel = scrollIndex_ % levelSpan;
  String row1 = "TANK:" + levelMetrics.substring(scrollPosLevel, scrollPosLevel + 11);
  pushToLcd(0, row0);
  pushToLcd(1, row1);
}

void UIManager::advanceMainScroll() {
  if (millis() - lastScrollMs_ > 2000) {
    lastScrollMs_ = millis();
    scrollIndex_++;
    if (scrollIndex_ > 100) scrollIndex_ = 0;
  }
}

String UIManager::buildFlowMetricString(const AnalyticsState& state) const {
  String result = " Q=";
  result += String(state.flow.instantaneousLps, 2);
  result += " Qn=";
  result += String(state.flow.baselineLps, 2);
  result += " Q";
  result += (state.flow.differencePct >= 0 ? "+" : "");
  result += String(state.flow.differencePct, 1);
  result += "% Qmin=";
  result += String(state.flow.minimumHealthyLps, 2);
  result += " Q";
  result += String((char)1);
  result += "=";
  result += String(state.flow.meanLps, 2);
  result += " Q";
  result += String((char)2);
  result += "=";
  result += String(state.flow.medianLps, 2);
  result += "   ";
  return result;
}

String UIManager::buildLevelMetricString(const AnalyticsState& state) const {
  String result = " h=";
  result += String(state.level.instantaneousCm, 1);
  result += " h";
  result += String((char)3);
  result += "=";
  result += String(state.level.baselineCm, 1);
  result += " h";
  result += String((char)4);
  result += "=";
  result += String(state.level.fullTankCm, 1);
  result += " h";
  result += (state.level.differencePct >= 0 ? "+" : "");
  result += String(state.level.differencePct, 1);
  result += "% noise=";
  result += String(state.level.noiseMetric, 1);
  result += "%   ";
  return result;
}

void UIManager::renderStats(const StatisticsSummary& stats, const char* title) {
  lcd_.noBlink();
  char buffer0[17];
  snprintf(buffer0, sizeof(buffer0), "%.4s mn%.1f mx%.1f", title, stats.minValue, stats.maxValue);
  pushToLcd(0, String(buffer0));
  char buffer1[17];
  snprintf(buffer1, sizeof(buffer1), "μ=%.1f η=%.1f σ=%.1f", stats.meanValue, stats.medianValue,
           stats.stddevValue);
  pushToLcd(1, String(buffer1));
}

void UIManager::renderCalibration() {
  lcd_.noBlink();
  pushToLcd(0, " Kalibrasyon h(cm)");
  char buffer[17];
  snprintf(buffer, sizeof(buffer), "   %.1f cm", calibrationInput_);
  pushToLcd(1, String(buffer));
}

void UIManager::pushToLcd(uint8_t row, const String& content) {
  String trimmed = content;
  if (trimmed.length() > config::LCD_COLS) {
    trimmed = trimmed.substring(0, config::LCD_COLS);
  }
  while (trimmed.length() < config::LCD_COLS) {
    trimmed += ' ';
  }
  if (row == 0 && trimmed == lastRow0_) {
    return;
  }
  if (row == 1 && trimmed == lastRow1_) {
    return;
  }
  lcd_.setCursor(0, row);
  lcd_.print(trimmed);
  if (row == 0) {
    lastRow0_ = trimmed;
  } else {
    lastRow1_ = trimmed;
  }
}

void UIManager::handleJoystick(float x, float y) {
  if (screen_ == Screen::TimeSetting) {
    int direction = 0;
    if (y > 0.2f) direction = 1;
    if (y < -0.2f) direction = -1;
    if (direction != 0) {
      int step = (fabs(y) > config::JOYSTICK_ACCEL_THRESHOLD)
                     ? static_cast<int>(ceil(config::JOYSTICK_ACCEL_MULTIPLIER))
                     : 1;
      updateTimeEditing(timeCursor_, direction * step);
    }
    if (fabs(x) > 0.5f) {
      timeCursor_ = (x > 0) ? (timeCursor_ + 1) : (timeCursor_ == 0 ? 0 : timeCursor_ - 1);
      if (timeCursor_ > 3) {
        setScreen(Screen::DateSetting);
      }
    }
  } else if (screen_ == Screen::DateSetting) {
    int direction = 0;
    if (y > 0.2f) direction = 1;
    if (y < -0.2f) direction = -1;
    if (direction != 0) {
      int step = (fabs(y) > config::JOYSTICK_ACCEL_THRESHOLD)
                     ? static_cast<int>(ceil(config::JOYSTICK_ACCEL_MULTIPLIER))
                     : 1;
      updateDateEditing(dateField_, direction * step);
    }
    if (fabs(x) > 0.5f) {
      dateField_ = (x > 0) ? (dateField_ + 1) : (dateField_ == 0 ? 0 : dateField_ - 1);
      if (dateField_ > 2) {
        setScreen(Screen::Main);
      }
    }
  } else if (screen_ == Screen::Calibration) {
    int direction = 0;
    if (y > 0.1f) direction = 1;
    if (y < -0.1f) direction = -1;
    if (direction != 0) {
      float accel = (fabs(y) > config::JOYSTICK_ACCEL_THRESHOLD)
                        ? config::JOYSTICK_ACCEL_MULTIPLIER
                        : 1.0f;
      calibrationInput_ += direction * accel;
    }
    if (calibrationInput_ < 0) {
      calibrationInput_ = 0;
    }
  } else {
    if (x > 0.5f) {
      if (screen_ == Screen::Main) {
        setScreen(Screen::LevelStats);
      } else if (screen_ == Screen::LevelStats) {
        setScreen(Screen::FlowStats);
      } else if (screen_ == Screen::FlowStats) {
        setScreen(Screen::Main);
      }
    } else if (x < -0.5f) {
      if (screen_ == Screen::Main) {
        setScreen(Screen::FlowStats);
      } else if (screen_ == Screen::LevelStats) {
        setScreen(Screen::Main);
      } else if (screen_ == Screen::FlowStats) {
        setScreen(Screen::LevelStats);
      }
    }
  }
}

void UIManager::handleButtons(bool button1Pressed, bool button2Pressed, bool bothHeld) {
  if (bothHeld) {
    setScreen(Screen::Calibration);
    calibrationRequested_ = true;
    return;
  }
  if (screen_ == Screen::Calibration) {
    if (!button1Pressed && !button2Pressed) {
      setScreen(Screen::Main);
    }
    return;
  }
}

void UIManager::setCalibrationValue(float value) { calibrationInput_ = value; }

void UIManager::resetCalibrationRequest() { calibrationRequested_ = false; }

void UIManager::setTimeSetting(struct tm timeinfo) {
  editableTime_ = timeinfo;
  timeCursor_ = 0;
  dateField_ = 0;
}

void UIManager::commitTime(struct tm timeinfo) { editableTime_ = timeinfo; }

void UIManager::updateTimeEditing(int digitIndex, int delta) {
  if (digitIndex < 0 || digitIndex > 3) {
    return;
  }
  int values[4] = {editableTime_.tm_hour / 10, editableTime_.tm_hour % 10,
                   editableTime_.tm_min / 10, editableTime_.tm_min % 10};
  values[digitIndex] += delta;
  if (values[digitIndex] < 0) {
    values[digitIndex] = 9;
  } else if (values[digitIndex] > 9) {
    values[digitIndex] = 0;
  }
  int newHour = values[0] * 10 + values[1];
  int newMinute = values[2] * 10 + values[3];
  if (newHour >= 24) {
    newHour = 0;
  }
  if (newMinute >= 60) {
    newMinute = 0;
  }
  editableTime_.tm_hour = newHour;
  editableTime_.tm_min = newMinute;
}

void UIManager::updateDateEditing(int fieldIndex, int delta) {
  if (fieldIndex == 0) {
    editableTime_.tm_mday += delta;
    if (editableTime_.tm_mday < 1) editableTime_.tm_mday = 31;
    if (editableTime_.tm_mday > 31) editableTime_.tm_mday = 1;
  } else if (fieldIndex == 1) {
    editableTime_.tm_mon += delta;
    if (editableTime_.tm_mon < 0) editableTime_.tm_mon = 11;
    if (editableTime_.tm_mon > 11) editableTime_.tm_mon = 0;
  } else if (fieldIndex == 2) {
    editableTime_.tm_year += delta;
    if (editableTime_.tm_year < 120) editableTime_.tm_year = 120;
    if (editableTime_.tm_year > 200) editableTime_.tm_year = 200;
  }
}

