#include "LcdUI.h"

#include <sys/time.h>

#include <cmath>

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

void LcdUI::showTemporaryMessage(const char* msg, uint32_t durationMs) {
    _overlayText = msg ? msg : "";
    _overlayEndMs = millis() + durationMs;
    _overlayActive = true;
    _overlayCachedLine[0] = "";
    _overlayCachedLine[1] = "";
    if (_lcd) {
        _lcd->clear();
    }
    _scroll.cachedLine[0] = "";
    _scroll.cachedLine[1] = "";
}

void LcdUI::setMetrics(const utils::SensorMetrics& metrics) {
    _metrics = metrics;
    _hasMetrics = true;
    _lastMetricsTimestamp = metrics.timestamp;
    rebuildScrollBuffers();
}

String LcdUI::centerText(const String& text) const {
    String line = text;
    if (line.length() > 16) {
        line = line.substring(0, 16);
    }
    size_t len = line.length();
    if (len >= 16) {
        return line;
    }
    size_t totalPad = 16 - len;
    size_t leftPad = totalPad / 2;
    size_t rightPad = totalPad - leftPad;
    String padded;
    for (size_t i = 0; i < leftPad; ++i) {
        padded += ' ';
    }
    padded += line;
    for (size_t i = 0; i < rightPad; ++i) {
        padded += ' ';
    }
    return padded;
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
            _calEditor.active = true;
            selectCalibrationItem(CalibrationEditor::Item::MeasuredDepth);
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

    uint32_t now = millis();
    if (_overlayActive && now >= _overlayEndMs) {
        _overlayActive = false;
        _overlayText = "";
        _overlayCachedLine[0] = "";
        _overlayCachedLine[1] = "";
        if (_lcd) {
            _lcd->clear();
        }
        _scroll.cachedLine[0] = "";
        _scroll.cachedLine[1] = "";
    }

    if (!_overlayActive && _buttons->bothHeldFor(3000)) {
        if (_logger) {
            _logger->pause();
            _logger->safeRemove();
        }
        showTemporaryMessage("SD removed safely", 3000);
    }

    if (_overlayActive) {
        renderOverlay();
        return;
    }

    float joyX = _joystick->readX();
    float joyY = _joystick->readY();

    bool calibrationHold = _buttons->bothHeldFor(5000);
    if (calibrationHold) {
        transition(ScreenState::Calibration);
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
    if (!_lcd) {
        return;
    }
    String fullLine;
    if (!items.empty()) {
        String content = items[index % items.size()];
        size_t available = (label.length() >= 16) ? 0 : (16 - label.length());
        if (available > 0 && content.length() > available) {
            size_t start = offset % content.length();
            String scrolled = content.substring(start) + ' ' + content.substring(0, start);
            content = scrolled.substring(0, available);
        }
        fullLine = label + content;
    } else {
        fullLine = label;
    }
    if (fullLine.length() < 16) {
        while (fullLine.length() < 16) {
            fullLine += ' ';
        }
    } else if (fullLine.length() > 16) {
        fullLine = fullLine.substring(0, 16);
    }
    if (_scroll.cachedLine[row] != fullLine) {
        _lcd->setCursor(0, row);
        _lcd->print(fullLine);
        _scroll.cachedLine[row] = fullLine;
    }
}

void LcdUI::renderOverlay() {
    if (!_lcd) {
        return;
    }
    String text = _overlayText;
    String line1;
    String line2;
    if (text.length() <= 16) {
        line1 = centerText(text);
        line2 = centerText("");
    } else {
        int splitIndex = text.lastIndexOf(' ', 16);
        if (splitIndex < 0) {
            splitIndex = 16;
        }
        line1 = text.substring(0, splitIndex);
        line1.trim();
        line2 = text.substring(splitIndex);
        line2.trim();
        if (line1.length() > 16) {
            line1 = line1.substring(0, 16);
        }
        if (line2.length() > 16) {
            line2 = line2.substring(0, 16);
        }
        line1 = centerText(line1);
        line2 = centerText(line2);
    }
    if (_overlayCachedLine[0] != line1) {
        _lcd->setCursor(0, 0);
        _lcd->print(line1);
        _overlayCachedLine[0] = line1;
    }
    if (_overlayCachedLine[1] != line2) {
        _lcd->setCursor(0, 1);
        _lcd->print(line2);
        _overlayCachedLine[1] = line2;
    }
}

void LcdUI::renderMainScreen() {
    renderScrollLine(0, "FLOW ", _scroll.flowLines, _scroll.flowIndex, _scroll.flowOffset);
    renderScrollLine(1, "TANK ", _scroll.tankLines, _scroll.tankIndex, _scroll.tankOffset);
}

void LcdUI::renderLevelStats() {
    if (!_lcd) {
        return;
    }
    char line[17];
    snprintf(line, sizeof(line), "MED %5.1f N %4.1f", _metrics.tankMedianCm, _metrics.tankNoisePercent);
    _lcd->setCursor(0, 0);
    _lcd->print(line);
    snprintf(line, sizeof(line), "E%4.0f F%4.0f d%4.0f", _metrics.tankEmptyEstimateCm, _metrics.tankFullEstimateCm,
             _metrics.tankDiffPercent);
    _lcd->setCursor(0, 1);
    _lcd->print(line);
}

void LcdUI::renderFlowStats() {
    if (!_lcd) {
        return;
    }
    float flowCv = (!isnan(_metrics.flowMeanLps) && _metrics.flowMeanLps > 0.001f)
                       ? (_metrics.flowStdDevLps / _metrics.flowMeanLps) * 100.0f
                       : NAN;
    char line[17];
    snprintf(line, sizeof(line), "MED %4.2f CV%4.1f", _metrics.flowMedianLps, flowCv);
    _lcd->setCursor(0, 0);
    _lcd->print(line);
    snprintf(line, sizeof(line), "P10 %4.2f P90 %4.2f", _metrics.flowMinHealthyLps, _metrics.flowBaselineLps);
    _lcd->setCursor(0, 1);
    _lcd->print(line);
}

void LcdUI::renderCalibration() {
    if (!_lcd) {
        return;
    }
    const __FlashStringHelper* label = calibrationLabel(_calEditor.item);
    String title = String(F("CAL ")) + String(label);
    while (title.length() < 16) {
        title += ' ';
    }
    if (title.length() > 16) {
        title = title.substring(0, 16);
    }
    _lcd->setCursor(0, 0);
    _lcd->print(title);

    float step = calibrationStep(_calEditor.item);
    uint8_t decimals = 0;
    if (step < 0.01f) {
        decimals = 3;
    } else if (step < 0.1f) {
        decimals = 2;
    } else if (step < 1.0f) {
        decimals = 1;
    }
    char valueBuffer[16];
    dtostrf(_calEditor.value, 0, decimals, valueBuffer);
    String units;
    switch (_calEditor.item) {
        case CalibrationEditor::Item::MeasuredDepth:
            units = F("cm");
            break;
        case CalibrationEditor::Item::Density:
            units = F("rho");
            break;
        case CalibrationEditor::Item::ZeroCurrent:
        case CalibrationEditor::Item::FullCurrent:
            units = F("mA");
            break;
        case CalibrationEditor::Item::FullScaleHeight:
            units = F("mm");
            break;
        case CalibrationEditor::Item::PulsesPerLiter:
            units = F("p/L");
            break;
        case CalibrationEditor::Item::SensorInterval:
        case CalibrationEditor::Item::LoggingInterval:
            units = F("ms");
            break;
        case CalibrationEditor::Item::SenseResistor:
            units = F("ohm");
            break;
        case CalibrationEditor::Item::SenseGain:
            units = F("x");
            break;
    }
    String valueLine = String(valueBuffer) + units;
    while (valueLine.length() < 11) {
        valueLine += ' ';
    }
    valueLine += F("1:OK 2:EX");
    if (valueLine.length() > 16) {
        valueLine = valueLine.substring(0, 16);
    }
    _lcd->setCursor(0, 1);
    _lcd->print(valueLine);
}

void LcdUI::selectCalibrationItem(CalibrationEditor::Item item) {
    _calEditor.item = item;
    _calEditor.value = calibrationValue(item);
}

const __FlashStringHelper* LcdUI::calibrationLabel(CalibrationEditor::Item item) const {
    switch (item) {
        case CalibrationEditor::Item::MeasuredDepth:
            return F("Depth cm");
        case CalibrationEditor::Item::Density:
            return F("Density");
        case CalibrationEditor::Item::ZeroCurrent:
            return F("Zero mA");
        case CalibrationEditor::Item::FullCurrent:
            return F("Full mA");
        case CalibrationEditor::Item::FullScaleHeight:
            return F("Full mm");
        case CalibrationEditor::Item::PulsesPerLiter:
            return F("Pulse/L");
        case CalibrationEditor::Item::SensorInterval:
            return F("Sensor ms");
        case CalibrationEditor::Item::LoggingInterval:
            return F("Log ms");
        case CalibrationEditor::Item::SenseResistor:
            return F("Shunt ohm");
        case CalibrationEditor::Item::SenseGain:
            return F("Gain");
    }
    return F("Cal");
}

float LcdUI::calibrationValue(CalibrationEditor::Item item) const {
    if (!_config) {
        return 0.0f;
    }
    switch (item) {
        case CalibrationEditor::Item::MeasuredDepth:
            return _hasMetrics ? _metrics.tankHeightCm : _calEditor.value;
        case CalibrationEditor::Item::Density:
            return _config->densityFactor();
        case CalibrationEditor::Item::ZeroCurrent:
            return _config->zeroCurrentMa();
        case CalibrationEditor::Item::FullCurrent:
            return _config->fullScaleCurrentMa();
        case CalibrationEditor::Item::FullScaleHeight:
            return _config->fullScaleHeightMm();
        case CalibrationEditor::Item::PulsesPerLiter:
            return _config->pulsesPerLiter();
        case CalibrationEditor::Item::SensorInterval:
            return static_cast<float>(_config->sensorIntervalMs());
        case CalibrationEditor::Item::LoggingInterval:
            return static_cast<float>(_config->loggingIntervalMs());
        case CalibrationEditor::Item::SenseResistor:
            return _config->currentSenseResistorOhms();
        case CalibrationEditor::Item::SenseGain:
            return _config->currentSenseGain();
    }
    return 0.0f;
}

float LcdUI::calibrationStep(CalibrationEditor::Item item) const {
    switch (item) {
        case CalibrationEditor::Item::MeasuredDepth:
            return 0.5f;
        case CalibrationEditor::Item::Density:
            return 0.01f;
        case CalibrationEditor::Item::ZeroCurrent:
        case CalibrationEditor::Item::FullCurrent:
            return 0.1f;
        case CalibrationEditor::Item::FullScaleHeight:
            return 10.0f;
        case CalibrationEditor::Item::PulsesPerLiter:
            return 0.2f;
        case CalibrationEditor::Item::SensorInterval:
        case CalibrationEditor::Item::LoggingInterval:
            return 100.0f;
        case CalibrationEditor::Item::SenseResistor:
            return 1.0f;
        case CalibrationEditor::Item::SenseGain:
            return 0.05f;
    }
    return 1.0f;
}

void LcdUI::commitCalibrationValue() {
    if (!_config) {
        return;
    }
    switch (_calEditor.item) {
        case CalibrationEditor::Item::MeasuredDepth:
            if (_calibrationCallback) {
                _calibrationCallback(_calEditor.value);
            }
            break;
        case CalibrationEditor::Item::Density:
            _config->setDensityFactor(_calEditor.value);
            break;
        case CalibrationEditor::Item::ZeroCurrent:
            _config->setZeroCurrentMa(_calEditor.value);
            break;
        case CalibrationEditor::Item::FullCurrent:
            _config->setFullScaleCurrentMa(_calEditor.value);
            break;
        case CalibrationEditor::Item::FullScaleHeight:
            _config->setFullScaleHeightMm(_calEditor.value);
            break;
        case CalibrationEditor::Item::PulsesPerLiter:
            _config->setPulsesPerLiter(_calEditor.value);
            break;
        case CalibrationEditor::Item::SensorInterval:
            _config->setSensorIntervalMs(static_cast<uint32_t>(roundf(_calEditor.value)));
            break;
        case CalibrationEditor::Item::LoggingInterval:
            _config->setLoggingIntervalMs(static_cast<uint32_t>(roundf(_calEditor.value)));
            break;
        case CalibrationEditor::Item::SenseResistor:
            _config->setCurrentSenseResistorOhms(_calEditor.value);
            break;
        case CalibrationEditor::Item::SenseGain:
            _config->setCurrentSenseGain(_calEditor.value);
            break;
    }
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
        if (_scroll.flowLines.size() > 1 && _scroll.flowOffset >= _scroll.flowLines[_scroll.flowIndex].length()) {
            _scroll.flowOffset = 0;
            _scroll.flowIndex = (_scroll.flowIndex + 1) % _scroll.flowLines.size();
            _scroll.cachedLine[0] = "";
        }
        if (_scroll.tankLines.size() > 1 && _scroll.tankOffset >= _scroll.tankLines[_scroll.tankIndex].length()) {
            _scroll.tankOffset = 0;
            _scroll.tankIndex = (_scroll.tankIndex + 1) % _scroll.tankLines.size();
            _scroll.cachedLine[1] = "";
        }
    }
}

void LcdUI::rebuildScrollBuffers() {
    if (!_hasMetrics) {
        return;
    }
    _scroll.flowLines.clear();
    _scroll.tankLines.clear();

    _scroll.flowLines.push_back(String("Q ") + utils::formatFloat(_metrics.flowLps, 2) + "L/s");
    _scroll.flowLines.push_back(String("Med ") + utils::formatFloat(_metrics.flowMedianLps, 2));
    _scroll.flowLines.push_back(String("P10 ") + utils::formatFloat(_metrics.flowMinHealthyLps, 2));
    _scroll.flowLines.push_back(String("P90 ") + utils::formatFloat(_metrics.flowBaselineLps, 2));
    _scroll.flowLines.push_back(String("d ") + utils::formatFloat(_metrics.flowDiffPercent, 1) + "%");
    if (!isnan(_metrics.flowPulseCv)) {
        _scroll.flowLines.push_back(String("CV ") + utils::formatFloat(_metrics.flowPulseCv, 1) + "%");
    }

    _scroll.tankLines.push_back(String("h ") + utils::formatFloat(_metrics.tankHeightCm, 1) + "cm");
    _scroll.tankLines.push_back(String("Med ") + utils::formatFloat(_metrics.tankMedianCm, 1));
    _scroll.tankLines.push_back(String("Empty ") + utils::formatFloat(_metrics.tankEmptyEstimateCm, 1));
    _scroll.tankLines.push_back(String("Full ") + utils::formatFloat(_metrics.tankFullEstimateCm, 1));
    _scroll.tankLines.push_back(String("d ") + utils::formatFloat(_metrics.tankDiffPercent, 1) + "%");
    _scroll.tankLines.push_back(String("Noise ") + utils::formatFloat(_metrics.tankNoisePercent, 1) + "%");
    _scroll.tankLines.push_back(String("Sig ") + utils::qualitativeNoise(_metrics.tankNoisePercent));

    _scroll.flowIndex = 0;
    _scroll.tankIndex = 0;
    _scroll.flowOffset = 0;
    _scroll.tankOffset = 0;
    _scroll.lastScrollMillis = millis();
    _scroll.cachedLine[0] = "";
    _scroll.cachedLine[1] = "";
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
    unsigned long now = millis();
    float step = calibrationStep(_calEditor.item);
    float accel = (fabs(joyY) > 0.8f) ? 5.0f : 1.0f;
    if (fabs(joyY) > 0.2f && now - _lastInputMillis > 120) {
        _calEditor.value += (joyY > 0 ? step : -step) * accel;
        if (_calEditor.item == CalibrationEditor::Item::MeasuredDepth && _calEditor.value < 0.0f) {
            _calEditor.value = 0.0f;
        }
        _lastInputMillis = now;
    }

    if (fabs(joyX) > 0.4f && now - _lastInputMillis > 200) {
        int direction = joyX > 0 ? 1 : -1;
        constexpr uint8_t itemCount = static_cast<uint8_t>(CalibrationEditor::Item::SenseGain) + 1;
        uint8_t index = static_cast<uint8_t>(_calEditor.item);
        index = (index + itemCount + direction) % itemCount;
        selectCalibrationItem(static_cast<CalibrationEditor::Item>(index));
        _lastInputMillis = now;
    }

    if (_buttons->wasPressed(Buttons::ButtonId::One)) {
        commitCalibrationValue();
        selectCalibrationItem(_calEditor.item);
        _lastInputMillis = now;
    }
    if (_buttons->wasPressed(Buttons::ButtonId::Two)) {
        transition(ScreenState::Main);
    }
}

