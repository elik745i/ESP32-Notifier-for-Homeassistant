#include "battery_monitor.h"

void BatteryMonitor::begin(const BatterySettings& settings, uint8_t adcPin, AppState& appState) {
    appState_ = &appState;
    adcPin_ = adcPin;
    analogReadResolution(12);
    analogSetPinAttenuation(adcPin_, ADC_11db);
    applySettings(settings);
    latest_ = sampleNow();
    lastSampleAt_ = millis();
}

void BatteryMonitor::applySettings(const BatterySettings& settings) {
    settings_ = settings;
}

BatteryReading BatteryMonitor::sampleNow() {
    uint32_t accumulator = 0;
    for (uint16_t index = 0; index < settings_.sampleCount; ++index) {
        accumulator += analogRead(adcPin_);
        delayMicroseconds(400);
    }
    const uint16_t raw = static_cast<uint16_t>(accumulator / settings_.sampleCount);
    const float rawVoltage = (static_cast<float>(raw) / 4095.0f) * 3.3f;
    float scaledVoltage = rawVoltage * settings_.dividerRatio * settings_.calibrationMultiplier;
    if (scaledVoltage < settings_.minVoltageClamp - 0.5f || scaledVoltage > settings_.maxVoltageClamp + 0.5f) {
        scaledVoltage = constrain(scaledVoltage, settings_.minVoltageClamp, settings_.maxVoltageClamp);
    }
    if (!initialized_) {
        filteredVoltage_ = scaledVoltage;
        initialized_ = true;
    } else {
        filteredVoltage_ = (settings_.smoothingAlpha * scaledVoltage) + ((1.0f - settings_.smoothingAlpha) * filteredVoltage_);
    }
    filteredVoltage_ = constrain(filteredVoltage_, settings_.minVoltageClamp, settings_.maxVoltageClamp);

    BatteryReading reading;
    reading.filteredVoltage = filteredVoltage_;
    reading.rawAdcVoltage = rawVoltage;
    reading.rawAdc = raw;
    return reading;
}

void BatteryMonitor::loop() {
    const unsigned long now = millis();
    if (now - lastSampleAt_ < settings_.updateIntervalMs) {
        return;
    }
    lastSampleAt_ = now;
    latest_ = sampleNow();
    if (appState_ != nullptr) {
        appState_->setBattery(latest_.filteredVoltage, latest_.rawAdcVoltage, latest_.rawAdc);
    }
}

BatteryReading BatteryMonitor::latest() const {
    return latest_;
}
