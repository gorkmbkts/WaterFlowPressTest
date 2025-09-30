#include "FlowSensor.h"

#include <driver/gpio.h>

namespace {
portMUX_TYPE flowSensorMux = portMUX_INITIALIZER_UNLOCKED;
}

FlowSensor* FlowSensor::instance = nullptr;

FlowSensor::FlowSensor() : _pin(0), _pulseCount(0), _lastPeriodMicros(0), _lastTimestampMicros(0) {}

void FlowSensor::begin(uint8_t pin) {
    _pin = pin;
    pinMode(_pin, INPUT);
    _pulseCount = 0;
    _lastPeriodMicros = 0;
    _lastTimestampMicros = micros();
    instance = this;
    attachInterrupt(digitalPinToInterrupt(_pin), FlowSensor::isrHandler, RISING);
}

void FlowSensor::reset() {
    portENTER_CRITICAL(&flowSensorMux);
    _pulseCount = 0;
    _lastPeriodMicros = 0;
    _lastTimestampMicros = micros();
    portEXIT_CRITICAL(&flowSensorMux);
}

FlowSensor::Snapshot FlowSensor::takeSnapshot() const {
    Snapshot snap;
    portENTER_CRITICAL(&flowSensorMux);
    snap.totalPulses = _pulseCount;
    snap.lastPeriodMicros = _lastPeriodMicros;
    snap.lastTimestampMicros = _lastTimestampMicros;
    portEXIT_CRITICAL(&flowSensorMux);
    return snap;
}

void IRAM_ATTR FlowSensor::isrHandler() {
    if (instance != nullptr) {
        instance->handlePulse();
    }
}

void IRAM_ATTR FlowSensor::handlePulse() {
    uint32_t now = micros();
    _pulseCount++;
    _lastPeriodMicros = now - _lastTimestampMicros;
    _lastTimestampMicros = now;
}

