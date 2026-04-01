#pragma once

#include <Arduino.h>

#include "app_state.h"
#include "settings_schema.h"

struct BatteryReading {
    float filteredVoltage = 0.0f;
    float rawAdcVoltage = 0.0f;
    uint16_t rawAdc = 0;
};

class BatteryMonitor {
  public:
    void begin(const BatterySettings& settings, uint8_t adcPin, AppState& appState);
    void applySettings(const BatterySettings& settings);
    void loop();
    BatteryReading latest() const;

  private:
    BatterySettings settings_;
    AppState* appState_ = nullptr;
    uint8_t adcPin_ = 36;
    unsigned long lastSampleAt_ = 0;
    float filteredVoltage_ = 0.0f;
    BatteryReading latest_;
    bool initialized_ = false;

    BatteryReading sampleNow();
};
