#include "display_manager.h"

#include <Wire.h>

void DisplayManager::begin(const OledSettings& settings) {
    applySettings(settings);
}

void DisplayManager::applySettings(const OledSettings& settings) {
    settings_ = settings;
    ssd1306_.reset();
    sh1106_.reset();
    if (!settings_.enabled) {
        return;
    }

    Wire.begin(settings_.sdaPin, settings_.sclPin);
    if (settings_.driver == "sh1106") {
        sh1106_.reset(new Adafruit_SH1106G(settings_.width, settings_.height, &Wire, settings_.resetPin));
        sh1106_->begin(settings_.i2cAddress, true);
        sh1106_->clearDisplay();
        sh1106_->display();
    } else {
        ssd1306_.reset(new Adafruit_SSD1306(settings_.width, settings_.height, &Wire, settings_.resetPin));
        ssd1306_->begin(SSD1306_SWITCHCAPVCC, settings_.i2cAddress);
        ssd1306_->clearDisplay();
        ssd1306_->display();
    }
    lastSignature_ = "";
}

void DisplayManager::setBootMessage(const String& message) {
    bootMessage_ = message;
    lastSignature_ = "";
}

bool DisplayManager::isEnabled() const {
    return settings_.enabled && (ssd1306_ || sh1106_);
}

Adafruit_GFX* DisplayManager::gfx() {
    if (ssd1306_) {
        return ssd1306_.get();
    }
    if (sh1106_) {
        return sh1106_.get();
    }
    return nullptr;
}

void DisplayManager::clearDisplay() {
    if (ssd1306_) {
        ssd1306_->clearDisplay();
    }
    if (sh1106_) {
        sh1106_->clearDisplay();
    }
}

void DisplayManager::flushDisplay() {
    if (ssd1306_) {
        ssd1306_->display();
    }
    if (sh1106_) {
        sh1106_->display();
    }
}

void DisplayManager::setDimmed(bool dimmed) {
    if (ssd1306_) {
        ssd1306_->dim(dimmed);
    }
}

String DisplayManager::centerTextForState(const AppStateSnapshot& state) const {
    if (state.ota.lastResult == "updating") {
        return "OTA updating";
    }
    if (!state.system.lastError.isEmpty()) {
        return state.system.lastError;
    }
    if (state.playback.state == "playing") {
        return state.playback.title.length() ? state.playback.title : state.playback.url;
    }
    if (state.network.apMode && !state.network.wifiConnected) {
        return "AP setup mode";
    }
    if (!state.network.wifiConnected) {
        return "Connecting Wi-Fi";
    }
    return bootMessage_.isEmpty() ? String("Idle") : bootMessage_;
}

void DisplayManager::drawWrappedLine(Adafruit_GFX& display, const String& text, int16_t y, uint8_t maxChars, bool scroll) {
    String toShow = text;
    if (toShow.length() <= maxChars || !scroll) {
        if (toShow.length() > maxChars) {
            toShow = toShow.substring(0, maxChars - 1) + "~";
        }
        display.setCursor(0, y);
        display.print(toShow);
        return;
    }

    const String padded = toShow + "   ";
    const uint16_t length = padded.length();
    const uint16_t start = scrollOffset_ % length;
    String window = padded.substring(start) + padded.substring(0, start);
    window = window.substring(0, maxChars);
    display.setCursor(0, y);
    display.print(window);
}

void DisplayManager::loop(const AppStateSnapshot& state) {
    if (!isEnabled()) {
        return;
    }
    const unsigned long now = millis();
    if (now - lastDrawAt_ < 300) {
        return;
    }

    if (state.playback.state == "playing" || state.network.wifiConnected || state.network.apMode) {
        lastActivityAt_ = now;
    }
    const bool shouldDim = settings_.dimTimeoutSeconds > 0 && (now - lastActivityAt_) > (settings_.dimTimeoutSeconds * 1000UL);
    setDimmed(shouldDim);

    const String top = state.network.wifiConnected ? state.network.ip : (state.network.apMode ? state.network.apSsid : String("Booting"));
    const String center = centerTextForState(state);
    const String bottom = String(state.network.wifiConnected ? "WiFi" : "AP") + " " +
                          (state.network.mqttConnected ? "MQTT" : "noMQTT") + " " +
                          state.playback.state;

    const String signature = top + "|" + center + "|" + bottom + "|" + String(scrollOffset_);
    if (signature == lastSignature_ && now - lastDrawAt_ < 800) {
        return;
    }
    lastSignature_ = signature;
    lastDrawAt_ = now;
    scrollOffset_++;

    Adafruit_GFX* display = gfx();
    clearDisplay();
    display->setTextColor(SSD1306_WHITE);
    display->setTextSize(1);
    drawWrappedLine(*display, top, 2, 21, false);
    display->drawLine(0, 11, settings_.width - 1, 11, SSD1306_WHITE);
    display->setTextSize(2);
    drawWrappedLine(*display, center, 24, 10, true);
    display->setTextSize(1);
    display->drawLine(0, settings_.height - 12, settings_.width - 1, settings_.height - 12, SSD1306_WHITE);
    drawWrappedLine(*display, bottom, settings_.height - 9, 21, false);
    flushDisplay();
}
