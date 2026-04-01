#include <Arduino.h>
#include <esp_system.h>

#ifdef SAFE_BOOT_DIAGNOSTIC

#include "default_config.h"
#include "version.h"

namespace {
const char* resetReasonToString(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN:
            return "unknown";
        case ESP_RST_POWERON:
            return "poweron";
        case ESP_RST_EXT:
            return "external";
        case ESP_RST_SW:
            return "software";
        case ESP_RST_PANIC:
            return "panic";
        case ESP_RST_INT_WDT:
            return "interrupt watchdog";
        case ESP_RST_TASK_WDT:
            return "task watchdog";
        case ESP_RST_WDT:
            return "other watchdog";
        case ESP_RST_DEEPSLEEP:
            return "deepsleep";
        case ESP_RST_BROWNOUT:
            return "brownout";
        case ESP_RST_SDIO:
            return "sdio";
        default:
            return "unhandled";
    }
}

unsigned long lastHeartbeatAt = 0;
}

void setup() {
    Serial.begin(115200);
    delay(300);
    pinMode(DefaultConfig::STATUS_LED_PIN, OUTPUT);
    digitalWrite(DefaultConfig::STATUS_LED_PIN, LOW);

    Serial.printf("\n[safe-boot] app=%s version=%s built=%s\n", APP_NAME, APP_VERSION, APP_BUILD_DATE);
    Serial.printf("[safe-boot] reset reason=%s (%d)\n", resetReasonToString(esp_reset_reason()), static_cast<int>(esp_reset_reason()));
    Serial.printf("[safe-boot] free heap=%u\n", ESP.getFreeHeap());
    Serial.println("[safe-boot] minimal firmware is running");
    Serial.flush();
}

void loop() {
    const unsigned long now = millis();
    if (now - lastHeartbeatAt >= 1000UL) {
        lastHeartbeatAt = now;
        digitalWrite(DefaultConfig::STATUS_LED_PIN, !digitalRead(DefaultConfig::STATUS_LED_PIN));
        Serial.printf("[safe-boot] uptime=%lu free_heap=%u\n", now, ESP.getFreeHeap());
        Serial.flush();
    }
    delay(10);
}

#else

#include "app_state.h"
#include "battery_monitor.h"
#include "default_config.h"
#include "display_manager.h"
#include "mqtt_manager.h"
#include "ota_manager.h"
#include "settings_manager.h"
#include "sound_effects.h"
#include "version.h"
#include "web_server.h"
#include "wifi_manager.h"

#ifdef APP_DISABLE_AUDIO
class AudioPlayerStub {
  public:
    void begin(uint8_t, uint8_t, uint8_t, uint8_t initialVolumePercent, AppState& appState) {
        appState_ = &appState;
        volume_ = initialVolumePercent;
        state_ = "idle";
        type_ = "idle";
        title_ = "Audio disabled";
        url_ = "";
        source_ = "disabled";
        publish();
    }

    void loop() {}

    bool play(const String&, const String&, const String&, const String&) {
        return false;
    }

    void stop() {
    state_ = "idle";
    type_ = "idle";
    title_ = "Audio disabled";
    url_ = "";
    source_ = "disabled";
    publish();
    }

    void setVolumePercent(uint8_t volumePercent) {
        volume_ = volumePercent;
        publish();
    }

        uint8_t volumePercent() const { return volume_; }

    private:
    void publish() {
        if (appState_ != nullptr) {
            appState_->setPlayback(state_, type_, title_, url_, source_, volume_);
        }
    }

    AppState* appState_ = nullptr;
        uint8_t volume_ = DefaultConfig::DEFAULT_VOLUME_PERCENT;
    String state_ = "idle";
    String type_ = "idle";
    String title_ = "Audio disabled";
    String url_;
    String source_ = "disabled";
};

using AudioPlayerType = AudioPlayerStub;
#else
#include "audio_player.h"
using AudioPlayerType = AudioPlayer;
#endif

namespace {
AppState* appState = nullptr;
SettingsManager* settingsManager = nullptr;
SettingsBundle* settings = nullptr;
WiFiManager* wifiManager = nullptr;
BatteryMonitor* batteryMonitor = nullptr;
DisplayManager* displayManager = nullptr;
AudioPlayerType* audioPlayer = nullptr;
OtaManager* otaManager = nullptr;
MqttManager* mqttManager = nullptr;
WebServerManager* webServer = nullptr;
SoundEffectsManager* soundEffects = nullptr;

struct DeferredActions {
    bool settingsApplyPending = false;
    SettingsBundle pendingSettings;
    bool playPending = false;
    bool stopPending = false;
    bool volumePending = false;
    uint8_t pendingVolume = 0;
    String playUrl;
    String playLabel;
    String playType;
    String playSource;
};

DeferredActions* deferredActions = nullptr;

bool rebootRequested = false;
bool factoryResetRequested = false;
unsigned long rebootAt = 0;
unsigned long lastHeapUpdateAt = 0;
bool previousWifiConnected = false;
bool previousMqttConnected = false;
String previousPlaybackState = "idle";
bool transitionStateInitialized = false;

const char* resetReasonToString(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN:
            return "unknown";
        case ESP_RST_POWERON:
            return "poweron";
        case ESP_RST_EXT:
            return "external";
        case ESP_RST_SW:
            return "software";
        case ESP_RST_PANIC:
            return "panic";
        case ESP_RST_INT_WDT:
            return "interrupt watchdog";
        case ESP_RST_TASK_WDT:
            return "task watchdog";
        case ESP_RST_WDT:
            return "other watchdog";
        case ESP_RST_DEEPSLEEP:
            return "deepsleep";
        case ESP_RST_BROWNOUT:
            return "brownout";
        case ESP_RST_SDIO:
            return "sdio";
        default:
            return "unhandled";
    }
}

void logBootStage(const char* stage) {
    Serial.printf("[boot] %s\n", stage);
    Serial.flush();
}

void scheduleReboot(uint32_t delayMs) {
    rebootRequested = true;
    rebootAt = millis() + delayMs;
}

bool initializeRuntimeObjects() {
    if (appState == nullptr) {
        appState = new AppState();
    }
    if (settingsManager == nullptr) {
        settingsManager = new SettingsManager();
    }
    if (settings == nullptr) {
        settings = new SettingsBundle();
    }
    if (wifiManager == nullptr) {
        wifiManager = new WiFiManager();
    }
    if (batteryMonitor == nullptr) {
        batteryMonitor = new BatteryMonitor();
    }
    if (displayManager == nullptr) {
        displayManager = new DisplayManager();
    }
    if (audioPlayer == nullptr) {
        audioPlayer = new AudioPlayerType();
    }
    if (otaManager == nullptr) {
        otaManager = new OtaManager();
    }
    if (mqttManager == nullptr) {
        mqttManager = new MqttManager();
    }
    if (webServer == nullptr) {
        webServer = new WebServerManager();
    }
    if (soundEffects == nullptr) {
        soundEffects = new SoundEffectsManager();
    }
    if (deferredActions == nullptr) {
        deferredActions = new DeferredActions();
    }

    return appState != nullptr && settingsManager != nullptr && settings != nullptr && wifiManager != nullptr &&
           batteryMonitor != nullptr && displayManager != nullptr && audioPlayer != nullptr && otaManager != nullptr &&
           mqttManager != nullptr && webServer != nullptr && soundEffects != nullptr && deferredActions != nullptr;
}

void applyRuntimeSettings() {
    appState->setDevice(settings->device.deviceName, settings->device.friendlyName, settings->usingSavedSettings);
    wifiManager->applySettings(*settings);
    batteryMonitor->applySettings(settings->battery);
    displayManager->applySettings(settings->oled);
    audioPlayer->setVolumePercent(settings->device.savedVolumePercent);
    soundEffects->applySettings(*settings);
    mqttManager->applySettings(*settings);
    otaManager->applySettings(*settings);
}

bool saveSettingsFromJson(JsonVariantConst root, String& error) {
    SettingsBundle updated = *settings;
    if (!settingsManager->updateFromJson(updated, root, error)) {
        return false;
    }
    deferredActions->pendingSettings = updated;
    deferredActions->settingsApplyPending = true;
    return true;
}

bool playRequest(const String& url, const String& label, const String& type, String& error) {
    if (url.isEmpty()) {
        error = "URL is required";
        return false;
    }
#ifdef APP_DISABLE_AUDIO
    error = "Audio disabled in diagnostic build";
    return false;
#endif
    deferredActions->playUrl = url;
    deferredActions->playLabel = label;
    deferredActions->playType = type;
    deferredActions->playSource = type == "tts" ? "home-assistant" : "manual";
    deferredActions->playPending = true;
    deferredActions->stopPending = false;
    return true;
}

void handleMqttCommand(const PlaybackCommand& command) {
    if (command.action == "stop") {
        audioPlayer->stop();
    } else if (command.action == "volume") {
        settings->device.savedVolumePercent = command.volumePercent;
        audioPlayer->setVolumePercent(command.volumePercent);
        soundEffects->setVolumePercent(command.volumePercent);
        settingsManager->save(*settings);
    } else {
        String ignored;
        playRequest(command.url, command.label, command.mediaType.isEmpty() ? command.action : command.mediaType, ignored);
    }
    mqttManager->publishState();
}

void processDeferredActions() {
    if (deferredActions == nullptr) {
        return;
    }

    if (deferredActions->settingsApplyPending) {
        settingsManager->save(deferredActions->pendingSettings);
        *settings = settingsManager->load();
        applyRuntimeSettings();
        deferredActions->settingsApplyPending = false;
        mqttManager->publishState();
    }

    if (deferredActions->stopPending) {
        audioPlayer->stop();
        deferredActions->stopPending = false;
        mqttManager->publishState();
    }

    if (deferredActions->volumePending) {
        settings->device.savedVolumePercent = deferredActions->pendingVolume;
        audioPlayer->setVolumePercent(deferredActions->pendingVolume);
        soundEffects->setVolumePercent(deferredActions->pendingVolume);
        settingsManager->save(*settings);
        deferredActions->volumePending = false;
        mqttManager->publishState();
    }

    if (deferredActions->playPending) {
        audioPlayer->play(
            deferredActions->playUrl,
            deferredActions->playLabel,
            deferredActions->playType,
            deferredActions->playSource);
        deferredActions->playPending = false;
        mqttManager->publishState();
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.printf("\n[boot] app=%s version=%s built=%s\n", APP_NAME, APP_VERSION, APP_BUILD_DATE);
    Serial.printf("[boot] reset reason=%s (%d)\n", resetReasonToString(esp_reset_reason()), static_cast<int>(esp_reset_reason()));
    Serial.flush();

    logBootStage("construct runtime objects");
    if (!initializeRuntimeObjects()) {
        Serial.println("[boot] failed to construct runtime objects");
        Serial.flush();
        delay(1000);
        ESP.restart();
    }

    logBootStage("app state mutex");
    if (!appState->begin()) {
        Serial.println("[boot] failed to initialize app state mutex");
        Serial.flush();
    }

    pinMode(DefaultConfig::STATUS_LED_PIN, OUTPUT);
    digitalWrite(DefaultConfig::STATUS_LED_PIN, LOW);

    logBootStage("settings begin");
    settingsManager->begin();
    *settings = settingsManager->load();

    logBootStage("display begin");
    displayManager->begin(settings->oled);
    displayManager->setBootMessage("Booting");

    logBootStage("app state init");
    appState->setDevice(settings->device.deviceName, settings->device.friendlyName, settings->usingSavedSettings);

    logBootStage("wifi begin");
    wifiManager->begin(*settings, *appState);

    logBootStage("battery begin");
    batteryMonitor->begin(settings->battery, DefaultConfig::BATTERY_ADC_PIN, *appState);

    logBootStage("audio begin");
    audioPlayer->begin(DefaultConfig::I2S_BCLK_PIN, DefaultConfig::I2S_WS_PIN, DefaultConfig::I2S_DOUT_PIN, settings->device.savedVolumePercent, *appState);

    logBootStage("sound effects begin");
    soundEffects->begin(*settings);

    logBootStage("ota begin");
    otaManager->begin(*settings, *appState);

    logBootStage("mqtt begin");
    mqttManager->begin(*settings, *appState, *wifiManager, handleMqttCommand);

    logBootStage("web begin");
    webServer->begin(
        *appState,
        *wifiManager,
        *settingsManager,
        *otaManager,
        []() { return *settings; },
        saveSettingsFromJson,
        playRequest,
        []() {
            deferredActions->stopPending = true;
            deferredActions->playPending = false;
        },
        [](uint8_t volume) {
            deferredActions->pendingVolume = volume;
            deferredActions->volumePending = true;
        },
        [](bool apply) { return otaManager->triggerCheck(apply); },
        []() { scheduleReboot(500); },
        []() {
            factoryResetRequested = true;
            scheduleReboot(500);
        });

    displayManager->setBootMessage("Idle");
    soundEffects->playBoot();
    logBootStage("setup complete");
}

void processSoundEffectTransitions(const AppStateSnapshot& snapshot) {
    if (soundEffects == nullptr) {
        return;
    }
    if (!transitionStateInitialized) {
        previousWifiConnected = snapshot.network.wifiConnected;
        previousMqttConnected = snapshot.network.mqttConnected;
        previousPlaybackState = snapshot.playback.state;
        transitionStateInitialized = true;
        return;
    }

    if (!previousWifiConnected && snapshot.network.wifiConnected) {
        soundEffects->playWifiConnected();
    } else if (previousWifiConnected && !snapshot.network.wifiConnected) {
        soundEffects->playWifiDisconnected();
    }

    if (!previousMqttConnected && snapshot.network.mqttConnected) {
        soundEffects->playMqttConnected();
    }

    if (previousPlaybackState != "playing" && snapshot.playback.state == "playing") {
        soundEffects->playPlaybackStart();
    } else if (previousPlaybackState == "playing" && snapshot.playback.state != "playing") {
        soundEffects->playPlaybackStop();
    }

    previousWifiConnected = snapshot.network.wifiConnected;
    previousMqttConnected = snapshot.network.mqttConnected;
    previousPlaybackState = snapshot.playback.state;
}

void loop() {
    if (appState == nullptr || settingsManager == nullptr || wifiManager == nullptr || batteryMonitor == nullptr ||
        displayManager == nullptr || audioPlayer == nullptr || otaManager == nullptr || mqttManager == nullptr ||
        webServer == nullptr) {
        delay(100);
        return;
    }

    processDeferredActions();
    wifiManager->loop();
    audioPlayer->loop();
    const bool batteryUpdated = batteryMonitor->loop();
    otaManager->loop();
    mqttManager->loop();

    const AppStateSnapshot snapshot = appState->snapshot();
    processSoundEffectTransitions(snapshot);
    displayManager->loop(snapshot);

    if (millis() - lastHeapUpdateAt > 5000UL) {
        lastHeapUpdateAt = millis();
        appState->setFreeHeap(ESP.getFreeHeap());
        digitalWrite(DefaultConfig::STATUS_LED_PIN, (wifiManager->isConnected() && mqttManager->isConnected()) ? HIGH : (millis() / 300UL) % 2);
    }

    if (batteryUpdated) {
        const BatteryReading reading = batteryMonitor->latest();
        mqttManager->publishBattery(reading.filteredVoltage, reading.rawAdcVoltage, reading.rawAdc);
        mqttManager->publishState();
    }

    if (factoryResetRequested) {
        settingsManager->reset();
        factoryResetRequested = false;
    }

    if (rebootRequested && static_cast<long>(millis() - rebootAt) >= 0) {
        ESP.restart();
    }
}

#endif
