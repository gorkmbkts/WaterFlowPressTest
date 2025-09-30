#pragma once

#include <Arduino.h>
#include <driver/pcnt.h>
#include <array>

#include <../Utils/Utils.h>

class FlowSensor {
  public:
    static constexpr size_t PERIOD_HISTORY = 16;

    struct Snapshot {
        uint64_t totalPulses = 0;
        uint32_t lastPeriodMicros = 0;
        uint32_t lastTimestampMicros = 0;
        std::array<uint32_t, PERIOD_HISTORY> recentPeriods{};
        size_t periodCount = 0;
    };

    FlowSensor();

    void begin(uint8_t pin, pcnt_unit_t unit = PCNT_UNIT_0);
    void reset();
    Snapshot takeSnapshot() const;

  private:
    static void IRAM_ATTR isrHandler(void* arg);
    void IRAM_ATTR handlePulse();
    void updateFromCounter() const;

    uint8_t _pin;
    pcnt_unit_t _unit;
    mutable volatile uint64_t _pulseCount;
    volatile uint32_t _lastPeriodMicros;
    volatile uint32_t _lastTimestampMicros;
    volatile uint32_t _periodHistory[PERIOD_HISTORY];
    volatile size_t _periodCount;
    volatile size_t _periodIndex;
};

