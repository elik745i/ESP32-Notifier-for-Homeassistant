#pragma once

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_SH110X.h>
#include <memory>

#include "app_state.h"
#include "settings_schema.h"

class DisplayManager {
  public:
    void begin(const OledSettings& settings);
    void applySettings(const OledSettings& settings);
    void setBootMessage(const String& message);
    void loop(const AppStateSnapshot& state);

  private:
    OledSettings settings_;
    std::unique_ptr<Adafruit_SSD1306> ssd1306_;
    std::unique_ptr<Adafruit_SH1106G> sh1106_;
    String bootMessage_ = "Booting";
    unsigned long lastDrawAt_ = 0;
    unsigned long lastActivityAt_ = 0;
    uint16_t scrollOffset_ = 0;
    String lastSignature_;

    bool isEnabled() const;
    Adafruit_GFX* gfx();
    void clearDisplay();
    void flushDisplay();
    void setDimmed(bool dimmed);
    uint8_t rotationIndex() const;
    void drawWrappedLine(Adafruit_GFX& gfx, const String& text, int16_t y, uint8_t maxChars, bool scroll);
    void drawOtaProgress(Adafruit_GFX& gfx, const AppStateSnapshot& state);
    String centerTextForState(const AppStateSnapshot& state) const;
};
