#include "LcdUI.h"

#include <sys/time.h>

#include "../ConfigService/ConfigService.h"
#include "../SdLogger/SdLogger.h"

namespace {
const char* MONTH_NAMES_TR[] = {"Ocak",   "Subat",  "Mart",   "Nisan",  "Mayis",  "Haziran",
                                "Temmuz", "Agustos", "Eylul",  "Ekim",   "Kasim",  "Aralik"};

const uint8_t GLYPH_MU[] = {0b00100, 0b01010, 0b01010, 0b01010, 0b01010, 0b11011, 0b00000, 0b00000};
const uint8_t GLYPH_ETA[] = {0b11011, 0b01010, 0b01010, 0b01110, 0b01010, 0b01010, 0b01010, 0b00000};
const uint8_t GLYPH_THETA[] = {0b00100, 0b01010, 0b11111, 0b01010, 0b01010, 0b11111, 0b00100, 0b00000};
const uint8_t GLYPH_SIGMA[] = {0b11111, 0b10000, 0b01000, 0b00100, 0b01000, 0b10000, 0b11111, 0b00000};
}

void LcdUI::begin(LiquidCrystal_I2C* lcd, Buttons* buttons, Joystick* joystick, SdLogger* logger, ConfigService* config) {
    _lcd = lcd;
    _buttons = buttons;
    _joystick = joystick;
    _logger = logger;
    _config = config;
    _bootStart = millis();
    if (_lcd) {
        _lcd->init();
        _lcd->backlight();
        _lcd->clear();
    }
    ensureCustomGlyphs();
    transition(ScreenState::Boot);
}

void LcdUI::setCalibrationCallback(CalibrationCallback cb) {
    _calibrationCallback = cb;
}

void LcdUI::setMetrics(const utils::SensorMetrics& metrics) {
    _metrics = metrics;
    _hasMetrics = true;
    _lastMetricsTimestamp = metrics.timestamp;
    rebuildScrollBuffers();
}

void LcdUI::ensureCustomGlyphs() {
    if (_glyphsReady || !_lcd) {
        return;
    }
    _lcd->createChar(_glyphMu, const_cast<uint8_t*>(GLYPH_MU));
    _lcd->createChar(_glyphEta, const_cast<uint8_t*>(GLYPH_ETA));
    _lcd->createChar(_glyphTheta, const_cast<uint8_t*>(GLYPH_THETA));
    _lcd->createChar(_glyphSigma, const_cast<uint8_t*>(GLYPH_SIGMA));
    _glyphsReady = true;
}

void LcdUI::transition(ScreenState next) {
    if (_state == next) {
        return;
    }
    _state = next;
    _lastInputMillis = millis();
    if (_lcd) {
        _lcd->clear();
    }
    switch (_state) {
        case ScreenState::SetTime: {
            time_t now = time(nullptr);
            localtime_r(&now, &_editor.tmData);
            _editor.cursorIndex = 0;
            _editor.editingTime = true;
            if (_lcd) {
                _lcd->blink();
            }
            break;
        }
        case ScreenState::SetDate:
            _editor.editingTime = false;
            if (_lcd) {
                _lcd->blink();
            }
            break;
        case ScreenState::Calibration:
            _calEditor.cursorIndex = 0;
            _calEditor.active = true;
            if (_lcd) {
                _lcd->blink();
            }
            break;
        default:
            if (_lcd) {
                _lcd->noBlink();
            }
            break;
    }
    if (_state == ScreenState::Main) {
        rebuildScrollBuffers();
    }
}

void LcdUI::update() {
    if (!_lcd || !_buttons || !_joystick) {
        return;
    }

    ensureCustomGlyphs();
    _buttons->update();

    float joyX = _joystick->readX();
    float joyY = _joystick->readY();

    bool calibrationHold = _buttons->bothHeldFor(5000);
    if (calibrationHold) {
        transition(ScreenState::Calibration);
        _calEditor.value = _metrics.tankHeightCm;
    } else if (_buttons->wasPressed(Buttons::ButtonId::One) && !_buttons->isPressed(Buttons::ButtonId::Two) && _logger) {
        _logger->requestEventSnapshot();
    }

    switch (_state) {
        case ScreenState::Boot:
            renderBoot();
            if (millis() - _bootStart > 5000) {
                transition(ScreenState::SetTime);
            }
            break;
        case ScreenState::SetTime:
            handleTimeEditing(joyX, joyY);
            renderTimeEditor();
            break;
        case ScreenState::SetDate:
            handleDateEditing(joyX, joyY);
            renderDateEditor();
            break;
        case ScreenState::Main:
            handleMainNavigation(joyX);
            updateScrollState();
            renderMainScreen();
            break;
        case ScreenState::LevelStats:
            handleMainNavigation(joyX);
            renderLevelStats();
            break;
        case ScreenState::FlowStats:
            handleMainNavigation(joyX);
            renderFlowStats();
            break;
        case ScreenState::Calibration:
            updateCalibration(joyX, joyY);
            renderCalibration();
            break;
    }
}

void LcdUI::renderBoot() {
    _lcd->setCursor(0, 0);
    _lcd->print("Project Kalkan");
    _lcd->setCursor(0, 1);
    _lcd->print("Hazirlaniyor...");
}

void LcdUI::renderTimeEditor() {
    _lcd->setCursor(0, 0);
    _lcd->print("Zamani Ayarla   ");

    char buffer[6];
    snprintf(buffer, sizeof(buffer), "%02d:%02d", _editor.tmData.tm_hour, _editor.tmData.tm_min);
    _lcd->setCursor(5, 1);
    _lcd->print(buffer);

    uint8_t cursorPositions[] = {5, 6, 8, 9};
    _lcd->setCursor(cursorPositions[std::min<uint8_t>(_editor.cursorIndex, 3)], 1);
}

void LcdUI::renderDateEditor() {
    _lcd->setCursor(0, 0);
    _lcd->print("Tarihi Ayarla   ");

    char buffer[17];
    const char* month = MONTH_NAMES_TR[_editor.tmData.tm_mon % 12];
    snprintf(buffer, sizeof(buffer), "%2d %s %4d", _editor.tmData.tm_mday, month, 1900 + _editor.tmData.tm_year);
    _lcd->setCursor(0, 1);
    _lcd->print(buffer);

    uint8_t cursorPositions[] = {0, 3, 13};
    _lcd->setCursor(cursorPositions[std::min<uint8_t>(_editor.cursorIndex, 2)], 1);
}

void LcdUI::renderScrollLine(uint8_t row, const String& label, const std::vector<String>& items, size_t index, size_t offset) {
    if (items.empty()) {
        return;
    }
    String content = items[index];
    size_t space = (label.length() >= 16) ? 0 : (16 - label.length());
    if (content.length() > space && space > 0) {
        size_t start = offset % content.length();
        String scrolled = content.substring(start) + " " + content.substring(0, start);
        content = scrolled.substring(0, space);
    }
    _lcd->setCursor(0, row);
    _lcd->print(label);
    _lcd->setCursor(label.length(), row);
    _lcd->print(content);
    size_t clearStart = label.length() + content.length();
    while (clearStart < 16) {
        _lcd->setCursor(clearStart++, row);
        _lcd->print(' ');
    }
}

void LcdUI::renderMainScreen() {
    renderScrollLine(0, "FLOW ", _scroll.flowLines, _scroll.flowIndex, _scroll.flowOffset);
    renderScrollLine(1, "TANK ", _scroll.tankLines, _scroll.tankIndex, _scroll.tankOffset);
}

void LcdUI::renderLevelStats() {
    char buffer[17];
    snprintf(buffer, sizeof(buffer), "MIN %5.1f MAX %5.1f", _metrics.tankMinObservedCm, _metrics.tankMaxObservedCm);
    _lcd->setCursor(0, 0);
    _lcd->print(buffer);
    snprintf(buffer, sizeof(buffer), "MU  %5.1f SD  %5.1f", _metrics.tankMeanCm, _metrics.tankStdDevCm);
    _lcd->setCursor(0, 1);
    _lcd->print(buffer);
}

void LcdUI::renderFlowStats() {
    char buffer[17];
    snprintf(buffer, sizeof(buffer), "MIN %4.2f MAX %4.2f", _metrics.flowMinLps, _metrics.flowMaxLps);
    _lcd->setCursor(0, 0);
    _lcd->print(buffer);
    snprintf(buffer, sizeof(buffer), "MU  %4.2f SD  %4.2f", _metrics.flowMeanLps, _metrics.flowStdDevLps);
    _lcd->setCursor(0, 1);
    _lcd->print(buffer);
}

void LcdUI::renderCalibration() {
    _lcd->setCursor(0, 0);
    _lcd->print("Kalibrasyon     ");
    _lcd->setCursor(0, 1);
    _lcd->print("cm: ");
    char buffer[10];
    dtostrf(_calEditor.value, 0, 1, buffer);
    _lcd->print(buffer);
    _lcd->print("       ");
    _lcd->setCursor(4 + _calEditor.cursorIndex, 1);
}

void LcdUI::handleTimeEditing(float joyX, float joyY) {
    unsigned long now = millis();
    float accel = (fabs(joyY) > 0.8f) ? 1.6f : 1.0f;
    if (fabs(joyY) > 0.2f && now - _lastInputMillis > 150) {
        int delta = (joyY > 0) ? 1 : -1;
        delta = static_cast<int>(delta * accel);
        if (_editor.cursorIndex < 2) {
            int hour = _editor.tmData.tm_hour;
            hour += (_editor.cursorIndex == 0 ? 10 : 1) * delta;
            while (hour < 0) {
                hour += 24;
            }
            hour %= 24;
            _editor.tmData.tm_hour = hour;
        } else {
            int minute = _editor.tmData.tm_min;
            minute += (_editor.cursorIndex == 2 ? 10 : 1) * delta;
            while (minute < 0) {
                minute += 60;
            }
            minute %= 60;
            _editor.tmData.tm_min = minute;
        }
        _lastInputMillis = now;
    }

    if (fabs(joyX) > 0.4f && now - _lastInputMillis > 200) {
        if (joyX > 0) {
            _editor.cursorIndex++;
            if (_editor.cursorIndex > 3) {
                transition(ScreenState::SetDate);
            }
        } else if (_editor.cursorIndex > 0) {
            _editor.cursorIndex--;
        }
        _lastInputMillis = now;
    }
}

void LcdUI::handleDateEditing(float joyX, float joyY) {
    unsigned long now = millis();
    float accel = (fabs(joyY) > 0.8f) ? 1.6f : 1.0f;
    if (fabs(joyY) > 0.2f && now - _lastInputMillis > 150) {
        int delta = (joyY > 0) ? 1 : -1;
        delta = static_cast<int>(delta * accel);
        switch (_editor.cursorIndex) {
            case 0: {
                int day = _editor.tmData.tm_mday + delta;
                if (day < 1) day = 31;
                if (day > 31) day = 1;
                _editor.tmData.tm_mday = day;
                break;
            }
            case 1: {
                int month = _editor.tmData.tm_mon + delta;
                while (month < 0) month += 12;
                month %= 12;
                _editor.tmData.tm_mon = month;
                break;
            }
            case 2: {
                int year = _editor.tmData.tm_year + delta;
                year = utils::clampValue(year, 120, 200);  // 2020-2100
                _editor.tmData.tm_year = year;
                break;
            }
        }
        _lastInputMillis = now;
    }

    if (fabs(joyX) > 0.4f && now - _lastInputMillis > 200) {
        if (joyX > 0) {
            _editor.cursorIndex++;
            if (_editor.cursorIndex > 2) {
                applyDateTime();
                transition(ScreenState::Main);
            }
        } else if (_editor.cursorIndex > 0) {
            _editor.cursorIndex--;
        }
        _lastInputMillis = now;
    }
}

void LcdUI::applyDateTime() {
    time_t newTime = mktime(&_editor.tmData);
    if (newTime <= 0) {
        return;
    }
    struct timeval tv;
    tv.tv_sec = newTime;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
}

void LcdUI::updateScrollState() {
    if (!_hasMetrics) {
        return;
    }
    unsigned long now = millis();
    if (now - _scroll.lastScrollMillis > _scroll.scrollInterval) {
        _scroll.lastScrollMillis = now;
        _scroll.flowOffset++;
        _scroll.tankOffset++;
        if (_scroll.flowLines.size() > 1 && _scroll.flowOffset > _scroll.flowLines[_scroll.flowIndex].length()) {
            _scroll.flowOffset = 0;
            _scroll.flowIndex = (_scroll.flowIndex + 1) % _scroll.flowLines.size();
        }
        if (_scroll.tankLines.size() > 1 && _scroll.tankOffset > _scroll.tankLines[_scroll.tankIndex].length()) {
            _scroll.tankOffset = 0;
            _scroll.tankIndex = (_scroll.tankIndex + 1) % _scroll.tankLines.size();
        }
    }
}

void LcdUI::rebuildScrollBuffers() {
    if (!_hasMetrics) {
        return;
    }
    String mu(1, static_cast<char>(_glyphMu));
    String eta(1, static_cast<char>(_glyphEta));
    String theta(1, static_cast<char>(_glyphTheta));
    String sigma(1, static_cast<char>(_glyphSigma));

    _scroll.flowLines = {
        String("Q=") + utils::formatFloat(_metrics.flowLps, 2) + " L/s",
        String("Qn=") + utils::formatFloat(_metrics.flowBaselineLps, 2) + " L/s",
        String("Qdif=") + utils::formatFloat(_metrics.flowDiffPercent, 1) + "%",
        String("Qmin=") + utils::formatFloat(_metrics.flowMinHealthyLps, 2) + " L/s",
        String("Q") + mu + "=" + utils::formatFloat(_metrics.flowMeanLps, 2) + " L/s",
        String("Q") + eta + "=" + utils::formatFloat(_metrics.flowMedianLps, 2) + " L/s"};

    _scroll.tankLines = {
        String("h=") + utils::formatFloat(_metrics.tankHeightCm, 1) + " cm",
        String("h") + theta + "=" + utils::formatFloat(_metrics.tankEmptyEstimateCm, 1) + " cm",
        String("h") + sigma + "=" + utils::formatFloat(_metrics.tankFullEstimateCm, 1) + " cm",
        String("hdif=") + utils::formatFloat(_metrics.tankDiffPercent, 1) + "%",
        String("signal:") + utils::qualitativeNoise(_metrics.tankNoisePercent)};

    _scroll.flowIndex = 0;
    _scroll.tankIndex = 0;
    _scroll.flowOffset = 0;
    _scroll.tankOffset = 0;
    _scroll.lastScrollMillis = millis();
}

void LcdUI::handleMainNavigation(float joyX) {
    unsigned long now = millis();
    if (fabs(joyX) < 0.6f || now - _lastInputMillis < 400) {
        return;
    }
    if (_state == ScreenState::Main) {
        if (joyX > 0) {
            transition(ScreenState::FlowStats);
        } else {
            transition(ScreenState::LevelStats);
        }
    } else if (_state == ScreenState::LevelStats) {
        if (joyX > 0) {
            transition(ScreenState::Main);
        } else {
            transition(ScreenState::FlowStats);
        }
    } else if (_state == ScreenState::FlowStats) {
        if (joyX > 0) {
            transition(ScreenState::Main);
        } else {
            transition(ScreenState::LevelStats);
        }
    }
    _lastInputMillis = now;
}

void LcdUI::updateCalibration(float joyX, float joyY) {
    (void)joyX;
    unsigned long now = millis();
    if (fabs(joyY) > 0.2f && now - _lastInputMillis > 150) {
        float step = (fabs(joyY) > 0.8f) ? 1.6f : 0.5f;
        _calEditor.value += (joyY > 0 ? step : -step);
        if (_calEditor.value < 0.0f) {
            _calEditor.value = 0.0f;
        }
        _lastInputMillis = now;
    }

    if (_buttons->wasPressed(Buttons::ButtonId::One)) {
        if (_calibrationCallback) {
            _calibrationCallback(_calEditor.value);
        }
        transition(ScreenState::Main);
    } else if (_buttons->wasPressed(Buttons::ButtonId::Two)) {
        transition(ScreenState::Main);
    }
}

