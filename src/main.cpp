#include <Arduino.h>

#include "app_state.h"
#include "audio_player.h"
#include "battery_monitor.h"
#include "default_config.h"
#include "display_manager.h"
#include "mqtt_manager.h"
#include "ota_manager.h"
#include "settings_manager.h"
#include "version.h"
#include "web_server.h"
#include "wifi_manager.h"

namespace {
AppState appState;
SettingsManager settingsManager;
SettingsBundle settings;
WiFiManager wifiManager;
BatteryMonitor batteryMonitor;
DisplayManager displayManager;
AudioPlayer audioPlayer;
OtaManager otaManager;
MqttManager mqttManager;
WebServerManager webServer;

bool rebootRequested = false;
bool factoryResetRequested = false;
unsigned long rebootAt = 0;
unsigned long lastHeapUpdateAt = 0;
unsigned long lastMqttBatteryAt = 0;

void scheduleReboot(uint32_t delayMs) {
    rebootRequested = true;
    rebootAt = millis() + delayMs;
}

void applyRuntimeSettings() {
    appState.setDevice(settings.device.deviceName, settings.device.friendlyName, settings.usingSavedSettings);
    wifiManager.applySettings(settings);
    batteryMonitor.applySettings(settings.battery);
    displayManager.applySettings(settings.oled);
    audioPlayer.setVolumePercent(settings.device.savedVolumePercent);
    mqttManager.applySettings(settings);
    otaManager.applySettings(settings);
}

bool saveSettingsFromJson(JsonVariantConst root, String& error) {
    SettingsBundle updated = settings;
    if (!settingsManager.updateFromJson(updated, root, error)) {
        return false;
    }
    settingsManager.save(updated);
    settings = settingsManager.load();
    applyRuntimeSettings();
    return true;
}

bool playRequest(const String& url, const String& label, const String& type, String& error) {
    if (url.isEmpty()) {
        error = "URL is required";
        return false;
    }
    if (!audioPlayer.play(url, label, type, type == "tts" ? "home-assistant" : "manual")) {
        error = "Failed to start playback";
        return false;
    }
    mqttManager.publishState();
    return true;
}

void handleMqttCommand(const PlaybackCommand& command) {
    if (command.action == "stop") {
        audioPlayer.stop();
    } else if (command.action == "volume") {
        settings.device.savedVolumePercent = command.volumePercent;
        audioPlayer.setVolumePercent(command.volumePercent);
        settingsManager.save(settings);
    } else {
        String ignored;
        playRequest(command.url, command.label, command.mediaType.isEmpty() ? command.action : command.mediaType, ignored);
    }
    mqttManager.publishState();
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);

    pinMode(DefaultConfig::STATUS_LED_PIN, OUTPUT);
    digitalWrite(DefaultConfig::STATUS_LED_PIN, LOW);

    settingsManager.begin();
    settings = settingsManager.load();

    displayManager.begin(settings.oled);
    displayManager.setBootMessage("Booting");

    appState.setDevice(settings.device.deviceName, settings.device.friendlyName, settings.usingSavedSettings);
    wifiManager.begin(settings, appState);
    batteryMonitor.begin(settings.battery, DefaultConfig::BATTERY_ADC_PIN, appState);
    audioPlayer.begin(DefaultConfig::I2S_BCLK_PIN, DefaultConfig::I2S_WS_PIN, DefaultConfig::I2S_DOUT_PIN, settings.device.savedVolumePercent, appState);
    otaManager.begin(settings, appState);
    mqttManager.begin(settings, appState, wifiManager, handleMqttCommand);
    webServer.begin(
        appState,
        wifiManager,
        settingsManager,
        otaManager,
        []() { return settings; },
        saveSettingsFromJson,
        playRequest,
        []() {
            audioPlayer.stop();
            mqttManager.publishState();
        },
        [](uint8_t volume) {
            settings.device.savedVolumePercent = volume;
            audioPlayer.setVolumePercent(volume);
            settingsManager.save(settings);
            mqttManager.publishState();
        },
        [](bool apply) { return otaManager.triggerCheck(apply); },
        []() { scheduleReboot(500); },
        []() {
            factoryResetRequested = true;
            scheduleReboot(500);
        });

    displayManager.setBootMessage("Idle");
}

void loop() {
    wifiManager.loop();
    audioPlayer.loop();
    batteryMonitor.loop();
    otaManager.loop();
    mqttManager.loop();

    const AppStateSnapshot snapshot = appState.snapshot();
    displayManager.loop(snapshot);

    if (millis() - lastHeapUpdateAt > 5000UL) {
        lastHeapUpdateAt = millis();
        appState.setFreeHeap(ESP.getFreeHeap());
        digitalWrite(DefaultConfig::STATUS_LED_PIN, (wifiManager.isConnected() && mqttManager.isConnected()) ? HIGH : (millis() / 300UL) % 2);
    }

    if (millis() - lastMqttBatteryAt > settings.battery.updateIntervalMs) {
        lastMqttBatteryAt = millis();
        const BatteryReading reading = batteryMonitor.latest();
        mqttManager.publishBattery(reading.filteredVoltage, reading.rawAdc);
        mqttManager.publishState();
    }

    if (factoryResetRequested) {
        settingsManager.reset();
        factoryResetRequested = false;
    }

    if (rebootRequested && static_cast<long>(millis() - rebootAt) >= 0) {
        ESP.restart();
    }
}
