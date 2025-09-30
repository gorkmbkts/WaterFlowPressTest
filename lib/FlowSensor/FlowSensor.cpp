#include "FlowSensor.h"

#include <cstring>

#include <driver/gpio.h>

namespace {
portMUX_TYPE flowSensorMux = portMUX_INITIALIZER_UNLOCKED;
}

FlowSensor::FlowSensor()
    : _pin(0),
      _unit(PCNT_UNIT_0),
      _pulseCount(0),
      _lastPeriodMicros(0),
      _lastTimestampMicros(0),
      _periodCount(0),
      _periodIndex(0) {
    memset((void*)_periodHistory, 0, sizeof(_periodHistory));
}

void FlowSensor::begin(uint8_t pin, pcnt_unit_t unit) {
    _pin = pin;
    _unit = unit;

    pinMode(_pin, INPUT);

    pcnt_config_t pcntConfig = {};
    pcntConfig.pulse_gpio_num = static_cast<gpio_num_t>(_pin);
    pcntConfig.ctrl_gpio_num = PCNT_PIN_NOT_USED;
    pcntConfig.unit = _unit;
    pcntConfig.channel = PCNT_CHANNEL_0;
    pcntConfig.counter_h_lim = 32000;
    pcntConfig.counter_l_lim = -1;
    pcntConfig.pos_mode = PCNT_COUNT_INC;
    pcntConfig.neg_mode = PCNT_COUNT_DIS;
    pcntConfig.lctrl_mode = PCNT_MODE_KEEP;
    pcntConfig.hctrl_mode = PCNT_MODE_KEEP;
    pcnt_unit_config(&pcntConfig);

    pcnt_set_filter_value(_unit, 1000);
    pcnt_filter_enable(_unit);
    pcnt_counter_pause(_unit);
    pcnt_counter_clear(_unit);
    pcnt_counter_resume(_unit);

    _pulseCount = 0;
    _lastPeriodMicros = 0;
    _lastTimestampMicros = micros();
    _periodCount = 0;
    _periodIndex = 0;

    attachInterruptArg(digitalPinToInterrupt(_pin), FlowSensor::isrHandler, this, RISING);
}

void FlowSensor::reset() {
    portENTER_CRITICAL(&flowSensorMux);
    _pulseCount = 0;
    _lastPeriodMicros = 0;
    _lastTimestampMicros = micros();
    _periodCount = 0;
    _periodIndex = 0;
    memset((void*)_periodHistory, 0, sizeof(_periodHistory));
    pcnt_counter_clear(_unit);
    portEXIT_CRITICAL(&flowSensorMux);
}

FlowSensor::Snapshot FlowSensor::takeSnapshot() const {
    Snapshot snap;
    updateFromCounter();

    portENTER_CRITICAL(&flowSensorMux);
    snap.totalPulses = _pulseCount;
    snap.lastPeriodMicros = _lastPeriodMicros;
    snap.lastTimestampMicros = _lastTimestampMicros;
    snap.periodCount = _periodCount;
    size_t count = _periodCount;
    for (size_t i = 0; i < count; ++i) {
        size_t index = (_periodIndex + PERIOD_HISTORY - count + i) % PERIOD_HISTORY;
        snap.recentPeriods[i] = _periodHistory[index];
    }
    for (size_t i = count; i < PERIOD_HISTORY; ++i) {
        snap.recentPeriods[i] = 0;
    }
    portEXIT_CRITICAL(&flowSensorMux);
    return snap;
}

void FlowSensor::updateFromCounter() const {
    int16_t current = 0;
    pcnt_get_counter_value(_unit, &current);
    if (current != 0) {
        pcnt_counter_clear(_unit);
        portENTER_CRITICAL(&flowSensorMux);
        if (current > 0) {
            _pulseCount += static_cast<uint64_t>(current);
        }
        portEXIT_CRITICAL(&flowSensorMux);
    }
}

void IRAM_ATTR FlowSensor::isrHandler(void* arg) {
    if (arg == nullptr) {
        return;
    }
    static_cast<FlowSensor*>(arg)->handlePulse();
}

void IRAM_ATTR FlowSensor::handlePulse() {
    uint32_t now = micros();
    portENTER_CRITICAL_ISR(&flowSensorMux);
    _lastPeriodMicros = now - _lastTimestampMicros;
    _lastTimestampMicros = now;
    _periodHistory[_periodIndex] = _lastPeriodMicros;
    _periodIndex = (_periodIndex + 1) % PERIOD_HISTORY;
    if (_periodCount < PERIOD_HISTORY) {
        _periodCount++;
    }
    portEXIT_CRITICAL_ISR(&flowSensorMux);
}

