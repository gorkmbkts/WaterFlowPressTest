#pragma once

#include <Arduino.h>
#include <Utils/Utils.h>

class FlowSensor {
  public:
    struct Snapshot {
        uint32_t totalPulses;
        uint32_t lastPeriodMicros;
        uint32_t lastTimestampMicros;
    };

    FlowSensor();

    void begin(uint8_t pin);
    void reset();
    Snapshot takeSnapshot() const;

  private:
    static void IRAM_ATTR isrHandler();
    void IRAM_ATTR handlePulse();

    static FlowSensor* instance;

    uint8_t _pin;
    volatile uint32_t _pulseCount;
    volatile uint32_t _lastPeriodMicros;
    volatile uint32_t _lastTimestampMicros;
};

