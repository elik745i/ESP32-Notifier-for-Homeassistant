#include "battery_monitor.h"

namespace {
constexpr uint16_t kBatteryWindowLimit = 32;

bool batteryDebugEnabled() {
#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
    return true;
#else
    return false;
#endif
}

uint16_t normalizeWindowSize(uint16_t size) {
    if (size < 1) {
        return 1;
    }
    if (size > kBatteryWindowLimit) {
        return kBatteryWindowLimit;
    }
    return size;
}
}  // namespace

void BatteryMonitor::begin(const BatterySettings& settings, uint8_t adcPin, AppState& appState) {
    appState_ = &appState;
    adcPin_ = adcPin;
    pinMode(adcPin_, INPUT);
    analogReadResolution(12);
    analogSetPinAttenuation(adcPin_, ADC_11db);
    applySettings(settings);
    latest_ = sampleNow();
    if (appState_ != nullptr) {
        appState_->setBattery(latest_.filteredVoltage, latest_.rawAdcVoltage, latest_.rawAdc);
    }
    lastSampleAt_ = millis();
}

void BatteryMonitor::applySettings(const BatterySettings& settings) {
    settings_ = settings;
    resetFilterState();
}

void BatteryMonitor::resetFilterState() {
    movingAverageSum_ = 0.0f;
    movingAverageCount_ = 0;
    movingAverageIndex_ = 0;
    for (uint16_t index = 0; index < kMaxWindowSize; ++index) {
        movingAverageSamples_[index] = 0.0f;
    }
}

BatteryReading BatteryMonitor::sampleNow() {
    const uint16_t raw = static_cast<uint16_t>(analogRead(adcPin_));
    const float rawVoltage = (static_cast<float>(raw) / 4095.0f) * 3.3f;
    const float correctedVoltage = rawVoltage * settings_.calibrationMultiplier;

    const uint16_t windowSize = normalizeWindowSize(settings_.movingAverageWindowSize);
    if (movingAverageCount_ < windowSize) {
        movingAverageSamples_[movingAverageIndex_] = correctedVoltage;
        movingAverageSum_ += correctedVoltage;
        ++movingAverageCount_;
    } else {
        movingAverageSum_ -= movingAverageSamples_[movingAverageIndex_];
        movingAverageSamples_[movingAverageIndex_] = correctedVoltage;
        movingAverageSum_ += correctedVoltage;
    }
    movingAverageIndex_ = (movingAverageIndex_ + 1U) % windowSize;

    const float filteredVoltage = movingAverageCount_ == 0 ? correctedVoltage : (movingAverageSum_ / movingAverageCount_);

    if (batteryDebugEnabled()) {
        Serial.printf("[battery] raw=%u raw_v=%.3f corrected_v=%.3f filtered_v=%.3f window=%u pin=%u\n", raw, rawVoltage,
                      correctedVoltage, filteredVoltage, windowSize, adcPin_);
    }

    BatteryReading reading;
    reading.filteredVoltage = filteredVoltage;
    reading.rawAdcVoltage = rawVoltage;
    reading.rawAdc = raw;
    return reading;
}

bool BatteryMonitor::loop() {
    const unsigned long now = millis();
    if (now - lastSampleAt_ < settings_.updateIntervalMs) {
        return false;
    }
    lastSampleAt_ = now;
    latest_ = sampleNow();
    if (appState_ != nullptr) {
        appState_->setBattery(latest_.filteredVoltage, latest_.rawAdcVoltage, latest_.rawAdc);
    }
    return true;
}

BatteryReading BatteryMonitor::latest() const {
    return latest_;
}
