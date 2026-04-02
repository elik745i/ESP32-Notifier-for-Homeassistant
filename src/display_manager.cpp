#include "display_manager.h"

#include <Wire.h>

namespace {
uint8_t charsForWidth(int16_t width, uint8_t textSize) {
    const int16_t charWidth = 6 * textSize;
    return width <= charWidth ? 1 : static_cast<uint8_t>(width / charWidth);
}
}

uint8_t DisplayManager::rotationIndex() const {
    switch (settings_.rotation) {
        case 90:
            return 1;
        case 180:
            return 2;
        case 270:
            return 3;
        default:
            return 0;
    }
}

int16_t DisplayManager::topDividerY(int16_t displayHeight) const {
    return min<int16_t>(11, displayHeight / 4);
}

int16_t DisplayManager::bottomDividerY(int16_t displayHeight) const {
    return max<int16_t>(displayHeight - 12, displayHeight - 12);
}

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
        sh1106_->setRotation(rotationIndex());
        sh1106_->clearDisplay();
        sh1106_->display();
    } else {
        ssd1306_.reset(new Adafruit_SSD1306(settings_.width, settings_.height, &Wire, settings_.resetPin));
        ssd1306_->begin(SSD1306_SWITCHCAPVCC, settings_.i2cAddress);
        ssd1306_->setRotation(rotationIndex());
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
    if (state.ota.busy) {
        return state.ota.phase.isEmpty() ? String("OTA updating") : state.ota.phase;
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

void DisplayManager::drawOtaProgress(Adafruit_GFX& display, const AppStateSnapshot& state) {
    const int16_t displayWidth = display.width();
    const int16_t displayHeight = display.height();
    const int16_t upperDivider = topDividerY(displayHeight);
    const int16_t lowerDivider = bottomDividerY(displayHeight);
    const int16_t centerTop = upperDivider + 6;
    const int16_t centerBottom = lowerDivider - 5;
    const int16_t centerHeight = max<int16_t>(18, centerBottom - centerTop);
    const uint8_t progress = state.ota.progressPercent;
    String label = state.ota.phase.isEmpty() ? String("Updating") : state.ota.phase;
    label += " ";
    label += progress;
    label += "%";

    display.setTextSize(1);
    const int16_t labelY = centerTop;
    drawWrappedLine(display, label, labelY, charsForWidth(displayWidth, 1), false);

    const int16_t barX = 8;
    const int16_t barHeight = 12;
    const int16_t barY = min<int16_t>(centerBottom - barHeight, labelY + 12);
    const int16_t barWidth = displayWidth - 16;
    display.drawRect(barX, barY, barWidth, barHeight, SSD1306_WHITE);
    const int16_t innerWidth = max<int16_t>(0, barWidth - 2);
    const int16_t fillWidth = (innerWidth * progress) / 100;
    if (fillWidth > 0) {
        display.fillRect(barX + 1, barY + 1, fillWidth, barHeight - 2, SSD1306_WHITE);
    }
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
                          String(state.battery.voltage, 2) + "V " +
                          (state.network.mqttConnected ? "MQTT" : "noMQTT");

    const String signature = top + "|" + center + "|" + bottom + "|" + String(scrollOffset_);
    if (signature == lastSignature_ && now - lastDrawAt_ < 800) {
        return;
    }
    lastSignature_ = signature;
    lastDrawAt_ = now;
    scrollOffset_++;

    Adafruit_GFX* display = gfx();
    const int16_t displayWidth = display->width();
    const int16_t displayHeight = display->height();
    const uint8_t topChars = charsForWidth(displayWidth, 1);
    const uint8_t centerChars = charsForWidth(displayWidth, 2);
    const uint8_t bottomChars = charsForWidth(displayWidth, 1);
    const int16_t upperDivider = topDividerY(displayHeight);
    const int16_t lowerDivider = bottomDividerY(displayHeight);
    const int16_t centerY = max<int16_t>(upperDivider + 12, (displayHeight / 2) - 8);
    clearDisplay();
    display->setTextColor(SSD1306_WHITE);
    display->setTextSize(1);
    drawWrappedLine(*display, top, 2, topChars, false);
    display->drawLine(0, upperDivider, displayWidth - 1, upperDivider, SSD1306_WHITE);
    if (state.ota.busy) {
        drawOtaProgress(*display, state);
    } else {
        display->setTextSize(2);
        drawWrappedLine(*display, center, centerY, centerChars, true);
    }
    display->setTextSize(1);
    display->drawLine(0, lowerDivider, displayWidth - 1, lowerDivider, SSD1306_WHITE);
    drawWrappedLine(*display, bottom, displayHeight - 9, bottomChars, false);
    flushDisplay();
}
