#include <Arduino.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_sleep.h>
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
#include "playback_text.h"
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
    String currentState() const { return state_; }

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
    bool playAddToHistory = true;
    bool stopPending = false;
    bool volumePending = false;
    bool volumeSavePending = false;
    uint8_t pendingVolume = 0;
    unsigned long volumeSaveAt = 0;
    String playUrl;
    String playLabel;
    String playType;
    String playSource;
};

DeferredActions* deferredActions = nullptr;

struct PhysicalButtonState {
    uint8_t pin = 0;
    const char* label = "";
    bool lastSampledPressed = false;
    bool stablePressed = false;
    unsigned long lastTransitionAt = 0;
};

struct PlaybackHistoryEntry {
    String url;
    String label;
    String type;
    String source;
};

bool rebootRequested = false;
bool factoryResetRequested = false;
unsigned long rebootAt = 0;
unsigned long lastHeapUpdateAt = 0;
bool recoveryRebootScheduled = false;
bool wokeFromDeepSleep = false;
unsigned long lowBatteryWakeStartedAt = 0;
unsigned long lastOtaMqttPublishAt = 0;
bool previousWifiConnected = false;
bool previousMqttConnected = false;
String previousPlaybackState = "idle";
String lastPublishedOtaSignature;
bool transitionStateInitialized = false;
bool otaPendingVerification = false;
bool otaHealthConfirmed = false;
unsigned long otaBootStartedAt = 0;
String otaPendingVersion;
String lastRolledBackVersion;
String lastRollbackReason;

constexpr float kBatteryPercentEmptyVoltage = 3.2f;
constexpr float kBatteryPercentFullVoltage = 4.2f;
constexpr unsigned long kLowBatteryWakeWindowMs = 30000UL;
constexpr unsigned long kVolumePersistDelayMs = 750UL;
constexpr unsigned long kButtonDebounceMs = 30UL;
constexpr size_t kPlaybackHistoryLimit = 12;
constexpr unsigned long kOtaHealthConfirmDelayMs = 8000UL;

constexpr char kOtaRollbackNamespace[] = "ota_state";
constexpr char kOtaPendingVersionKey[] = "pend_ver";
constexpr char kOtaPendingReasonKey[] = "pend_reason";
constexpr char kOtaLastBadVersionKey[] = "bad_ver";
constexpr char kOtaLastBadReasonKey[] = "bad_reason";

bool playRequest(const String& url, const String& label, const String& type, const String& source, String& error, bool addToHistory);

PhysicalButtonState button1 { DefaultConfig::BUTTON1_PIN, "Button 1" };
PhysicalButtonState button2 { DefaultConfig::BUTTON2_PIN, "Button 2" };
PlaybackHistoryEntry playbackHistory[kPlaybackHistoryLimit];
size_t playbackHistoryCount = 0;
int playbackHistoryIndex = -1;

bool isBatterySamplingAllowed() {
    if (audioPlayer == nullptr) {
        return true;
    }

    const String audioState = audioPlayer->currentState();
    return audioState != "playing" && audioState != "buffering";
}

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

String normalizedAppVersion() {
    return String("v") + APP_VERSION;
}

void storeRollbackPendingInfo(const String& version, const String& reason) {
    Preferences prefs;
    if (!prefs.begin(kOtaRollbackNamespace, false)) {
        return;
    }
    prefs.putString(kOtaPendingVersionKey, version);
    prefs.putString(kOtaPendingReasonKey, reason);
    prefs.end();
}

void clearRollbackPendingInfo() {
    Preferences prefs;
    if (!prefs.begin(kOtaRollbackNamespace, false)) {
        return;
    }
    prefs.remove(kOtaPendingVersionKey);
    prefs.remove(kOtaPendingReasonKey);
    prefs.end();
}

void persistRollbackOutcome(const String& version, const String& reason) {
    Preferences prefs;
    if (!prefs.begin(kOtaRollbackNamespace, false)) {
        return;
    }
    prefs.putString(kOtaLastBadVersionKey, version);
    prefs.putString(kOtaLastBadReasonKey, reason);
    prefs.end();
}

void loadRollbackState() {
    Preferences prefs;
    if (!prefs.begin(kOtaRollbackNamespace, false)) {
        return;
    }

    otaPendingVersion = prefs.getString(kOtaPendingVersionKey, "");
    String pendingReason = prefs.getString(kOtaPendingReasonKey, "");
    lastRolledBackVersion = prefs.getString(kOtaLastBadVersionKey, "");
    lastRollbackReason = prefs.getString(kOtaLastBadReasonKey, "");

    const String currentVersion = normalizedAppVersion();
    const esp_partition_t* runningPartition = esp_ota_get_running_partition();
    esp_ota_img_states_t otaState = ESP_OTA_IMG_UNDEFINED;
    if (runningPartition != nullptr && esp_ota_get_state_partition(runningPartition, &otaState) == ESP_OK && otaState == ESP_OTA_IMG_PENDING_VERIFY) {
        otaPendingVerification = true;
        otaBootStartedAt = millis();
        otaHealthConfirmed = false;
        otaPendingVersion = currentVersion;
        if (pendingReason.isEmpty()) {
            pendingReason = "App booted but did not finish health confirmation.";
        }
        prefs.putString(kOtaPendingVersionKey, otaPendingVersion);
        prefs.putString(kOtaPendingReasonKey, pendingReason);
    } else {
        otaPendingVerification = false;
        otaHealthConfirmed = true;
        if (!otaPendingVersion.isEmpty() && otaPendingVersion != currentVersion) {
            lastRolledBackVersion = otaPendingVersion;
            if (pendingReason.isEmpty()) {
                pendingReason = "New firmware rebooted before health confirmation; bootloader rolled back.";
            }
            lastRollbackReason = pendingReason;
            prefs.putString(kOtaLastBadVersionKey, lastRolledBackVersion);
            prefs.putString(kOtaLastBadReasonKey, lastRollbackReason);
        }
        prefs.remove(kOtaPendingVersionKey);
        prefs.remove(kOtaPendingReasonKey);
        otaPendingVersion = "";
    }

    prefs.end();
}

void refreshRollbackStateInOtaManager() {
    if (otaManager == nullptr) {
        return;
    }
    otaManager->setRollbackState(otaPendingVerification && !otaHealthConfirmed, otaPendingVersion, lastRolledBackVersion, lastRollbackReason);
}

[[noreturn]] void rollbackAndReboot(const String& reason) {
    const String version = otaPendingVersion.isEmpty() ? normalizedAppVersion() : otaPendingVersion;
    storeRollbackPendingInfo(version, reason);
    persistRollbackOutcome(version, reason);
    Serial.printf("[rollback] %s\n", reason.c_str());
    Serial.flush();
    const esp_err_t result = esp_ota_mark_app_invalid_rollback_and_reboot();
    Serial.printf("[rollback] esp_ota_mark_app_invalid_rollback_and_reboot failed: %d\n", static_cast<int>(result));
    Serial.flush();
    delay(1000);
    ESP.restart();
    for (;;) {
        delay(1000);
    }
}

void confirmOtaHealthIfReady() {
    if (!otaPendingVerification || otaHealthConfirmed) {
        return;
    }
    if ((millis() - otaBootStartedAt) < kOtaHealthConfirmDelayMs) {
        return;
    }
    if (rebootRequested || factoryResetRequested || recoveryRebootScheduled) {
        return;
    }

    const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
    if (result == ESP_OK) {
        otaHealthConfirmed = true;
        otaPendingVerification = false;
        otaPendingVersion = "";
        clearRollbackPendingInfo();
        refreshRollbackStateInOtaManager();
        Serial.println("[rollback] OTA firmware marked healthy");
        Serial.flush();
    } else {
        Serial.printf("[rollback] failed to confirm OTA app: %d\n", static_cast<int>(result));
        Serial.flush();
    }
}

void scheduleReboot(uint32_t delayMs) {
    rebootRequested = true;
    rebootAt = millis() + delayMs;
}

uint8_t estimateBatteryPercent(float voltage) {
    const float normalized = (voltage - kBatteryPercentEmptyVoltage) / (kBatteryPercentFullVoltage - kBatteryPercentEmptyVoltage);
    const float clamped = normalized < 0.0f ? 0.0f : (normalized > 1.0f ? 1.0f : normalized);
    return static_cast<uint8_t>((clamped * 100.0f) + 0.5f);
}

String normalizedButtonAction(String action, const char* fallback) {
    action.trim();
    action.toLowerCase();
    action.replace('-', '_');
    action.replace(' ', '_');

    if (action == "none" || action == "previous" || action == "next" || action == "play_pause" ||
        action == "replay_current" || action == "stop" || action == "volume_up" || action == "volume_down" ||
        action == "ha_previous" || action == "ha_next") {
        return action;
    }

    return String(fallback);
}

String buttonActionFor(const PhysicalButtonState& button) {
    if (settings == nullptr) {
        return button.pin == DefaultConfig::BUTTON1_PIN ? String(DefaultConfig::BUTTON1_DEFAULT_ACTION) : String(DefaultConfig::BUTTON2_DEFAULT_ACTION);
    }

    return button.pin == DefaultConfig::BUTTON1_PIN
        ? normalizedButtonAction(settings->device.button1Action, DefaultConfig::BUTTON1_DEFAULT_ACTION)
        : normalizedButtonAction(settings->device.button2Action, DefaultConfig::BUTTON2_DEFAULT_ACTION);
}

String buttonActionDisplayLabel(const String& action) {
    if (action == "none") {
        return "Disabled";
    }
    if (action == "previous") {
        return "Previous";
    }
    if (action == "next") {
        return "Next";
    }
    if (action == "play_pause") {
        return "Play/Pause";
    }
    if (action == "replay_current") {
        return "Replay";
    }
    if (action == "stop") {
        return "Stop";
    }
    if (action == "volume_up") {
        return "Volume +";
    }
    if (action == "volume_down") {
        return "Volume -";
    }
    if (action == "ha_previous") {
        return "HA Prev";
    }
    if (action == "ha_next") {
        return "HA Next";
    }
    return action;
}

String buttonOverlayText(const String& action) {
    if ((action == "volume_up" || action == "volume_down") && settings != nullptr) {
        String label = "Volume ";
        label += settings->device.savedVolumePercent;
        label += "%";
        return label;
    }

    return buttonActionDisplayLabel(action);
}

void showS3ButtonActionOnDisplay(const String& action) {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    if (displayManager != nullptr) {
        displayManager->showTemporaryCenterText(buttonOverlayText(action));
    }
#else
    (void)action;
#endif
}

void initializeButtons() {
    pinMode(button1.pin, INPUT);
    pinMode(button2.pin, INPUT);

    const unsigned long now = millis();
    button1.lastSampledPressed = digitalRead(button1.pin) == HIGH;
    button1.stablePressed = button1.lastSampledPressed;
    button1.lastTransitionAt = now;

    button2.lastSampledPressed = digitalRead(button2.pin) == HIGH;
    button2.stablePressed = button2.lastSampledPressed;
    button2.lastTransitionAt = now;
}

void rememberPlaybackSelection(const String& url, const String& label, const String& type, const String& source) {
    const String normalizedUrl = PlaybackText::normalizeUrl(url);
    const String normalizedLabel = PlaybackText::normalizeTitle(label, normalizedUrl);

    if (normalizedUrl.isEmpty()) {
        return;
    }

    if (playbackHistoryIndex >= 0 && playbackHistoryIndex < static_cast<int>(playbackHistoryCount)) {
        PlaybackHistoryEntry& current = playbackHistory[playbackHistoryIndex];
        if (current.url == normalizedUrl && current.type == type) {
            current.label = normalizedLabel;
            current.source = source;
            return;
        }
    }

    if (playbackHistoryIndex >= 0 && playbackHistoryIndex < static_cast<int>(playbackHistoryCount - 1)) {
        playbackHistoryCount = static_cast<size_t>(playbackHistoryIndex + 1);
    }

    if (playbackHistoryCount > 0) {
        PlaybackHistoryEntry& last = playbackHistory[playbackHistoryCount - 1];
        if (last.url == normalizedUrl && last.type == type) {
            last.label = normalizedLabel;
            last.source = source;
            playbackHistoryIndex = static_cast<int>(playbackHistoryCount - 1);
            return;
        }
    }

    if (playbackHistoryCount == kPlaybackHistoryLimit) {
        for (size_t index = 1; index < playbackHistoryCount; ++index) {
            playbackHistory[index - 1] = playbackHistory[index];
        }
        playbackHistoryCount -= 1;
    }

    playbackHistory[playbackHistoryCount] = {normalizedUrl, normalizedLabel, type, source};
    playbackHistoryCount += 1;
    playbackHistoryIndex = static_cast<int>(playbackHistoryCount - 1);
}

bool replayPlaybackEntry(const PlaybackHistoryEntry& entry, bool addToHistory) {
    if (entry.url.isEmpty()) {
        return false;
    }

    String error;
    return playRequest(entry.url, entry.label, entry.type, entry.source, error, addToHistory);
}

bool replayCurrentPlaybackSelection(bool addToHistory) {
    if (playbackHistoryIndex >= 0 && playbackHistoryIndex < static_cast<int>(playbackHistoryCount)) {
        return replayPlaybackEntry(playbackHistory[playbackHistoryIndex], addToHistory);
    }

    if (appState == nullptr) {
        return false;
    }

    const AppStateSnapshot snapshot = appState->snapshot();
    if (snapshot.playback.url.isEmpty()) {
        return false;
    }

    String error;
    return playRequest(snapshot.playback.url, snapshot.playback.title, snapshot.playback.type, snapshot.playback.source, error, addToHistory);
}

bool stepPlaybackHistory(int direction) {
    if (playbackHistoryCount == 0 || direction == 0) {
        return false;
    }

    int nextIndex = playbackHistoryIndex;
    if (nextIndex < 0 || nextIndex >= static_cast<int>(playbackHistoryCount)) {
        nextIndex = direction > 0 ? 0 : static_cast<int>(playbackHistoryCount - 1);
    } else {
        nextIndex = (nextIndex + direction + static_cast<int>(playbackHistoryCount)) % static_cast<int>(playbackHistoryCount);
    }

    playbackHistoryIndex = nextIndex;
    return replayPlaybackEntry(playbackHistory[nextIndex], false);
}

void applyVolumePercent(uint8_t volumePercent) {
    if (settings == nullptr || audioPlayer == nullptr || soundEffects == nullptr || deferredActions == nullptr) {
        return;
    }

    settings->device.savedVolumePercent = volumePercent;
    audioPlayer->setVolumePercent(volumePercent);
    soundEffects->setVolumePercent(volumePercent);
    deferredActions->pendingVolume = volumePercent;
    deferredActions->volumePending = false;
    deferredActions->volumeSavePending = true;
    deferredActions->volumeSaveAt = millis() + kVolumePersistDelayMs;
    if (mqttManager != nullptr) {
        mqttManager->publishState();
    }
}

void changeVolumeBy(int delta) {
    if (settings == nullptr) {
        return;
    }

    int nextVolume = static_cast<int>(settings->device.savedVolumePercent) + delta;
    if (nextVolume < 0) {
        nextVolume = 0;
    }
    if (nextVolume > 100) {
        nextVolume = 100;
    }

    applyVolumePercent(static_cast<uint8_t>(nextVolume));
}

bool executeButtonAction(const PhysicalButtonState& button, const String& action) {
    if (action == "none") {
        return false;
    }

    if (displayManager != nullptr) {
        displayManager->markActivity();
    }

    if (action == "previous") {
        return stepPlaybackHistory(-1);
    }
    if (action == "next") {
        return stepPlaybackHistory(1);
    }
    if (action == "play_pause") {
        const AppStateSnapshot snapshot = appState->snapshot();
        if (snapshot.playback.state == "playing" || snapshot.playback.state == "buffering") {
            audioPlayer->stop();
            if (mqttManager != nullptr) {
                mqttManager->publishState();
            }
            return true;
        }
        return replayCurrentPlaybackSelection(false);
    }
    if (action == "replay_current") {
        return replayCurrentPlaybackSelection(false);
    }
    if (action == "stop") {
        audioPlayer->stop();
        if (mqttManager != nullptr) {
            mqttManager->publishState();
        }
        return true;
    }
    if (action == "volume_up") {
        changeVolumeBy(DefaultConfig::BUTTON_VOLUME_STEP_PERCENT);
        return true;
    }
    if (action == "volume_down") {
        changeVolumeBy(-static_cast<int>(DefaultConfig::BUTTON_VOLUME_STEP_PERCENT));
        return true;
    }
    if (action == "ha_previous" || action == "ha_next") {
        if (mqttManager == nullptr) {
            return false;
        }
        return mqttManager->publishButtonActionEvent(button.label, button.pin, action == "ha_previous" ? "previous" : "next");
    }

    return false;
}

void pollPhysicalButton(PhysicalButtonState& button) {
    const bool pressed = digitalRead(button.pin) == HIGH;
    const unsigned long now = millis();

    if (pressed != button.lastSampledPressed) {
        button.lastSampledPressed = pressed;
        button.lastTransitionAt = now;
    }

    if ((now - button.lastTransitionAt) < kButtonDebounceMs || pressed == button.stablePressed) {
        return;
    }

    button.stablePressed = pressed;
    if (!button.stablePressed) {
        return;
    }

    const String action = buttonActionFor(button);
    const bool handled = executeButtonAction(button, action);
    if (handled || action == "none") {
        showS3ButtonActionOnDisplay(action);
    }
    Serial.printf("[input] %s on GPIO%u action=%s handled=%s\n",
                  button.label,
                  static_cast<unsigned>(button.pin),
                  action.c_str(),
                  handled ? "yes" : "no");
}

void pollPhysicalButtons() {
    pollPhysicalButton(button1);
    pollPhysicalButton(button2);
}

void enterLowBatteryDeepSleep(uint8_t batteryPercent, float voltage, const char* reason) {
    if (settings == nullptr) {
        return;
    }

    const uint16_t wakeIntervalMinutes = settings->device.lowBatteryWakeIntervalMinutes;
    Serial.printf("[power] entering deep sleep reason=%s battery=%u%% voltage=%.3f wake_interval_min=%u\n",
                  reason,
                  static_cast<unsigned>(batteryPercent),
                  voltage,
                  static_cast<unsigned>(wakeIntervalMinutes));
    Serial.flush();

    if (audioPlayer != nullptr) {
        audioPlayer->stop();
    }
    if (displayManager != nullptr) {
        displayManager->powerOff();
    }

    delay(100);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    if (wakeIntervalMinutes > 0) {
        const uint64_t wakeIntervalUs = static_cast<uint64_t>(wakeIntervalMinutes) * 60ULL * 1000000ULL;
        esp_sleep_enable_timer_wakeup(wakeIntervalUs);
    }

    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    delay(100);
    esp_deep_sleep_start();
}

void handleLowBatterySleepPolicy(const AppStateSnapshot& snapshot) {
    if (settings == nullptr || snapshot.ota.busy || !settings->device.lowBatterySleepEnabled) {
        if (settings == nullptr || !settings->device.lowBatterySleepEnabled) {
            wokeFromDeepSleep = false;
            lowBatteryWakeStartedAt = 0;
        }
        return;
    }

    const uint8_t batteryPercent = estimateBatteryPercent(snapshot.battery.voltage);
    const uint8_t thresholdPercent = settings->device.lowBatterySleepThresholdPercent;
    if (batteryPercent > thresholdPercent) {
        wokeFromDeepSleep = false;
        lowBatteryWakeStartedAt = 0;
        return;
    }

    if (wokeFromDeepSleep) {
        if (lowBatteryWakeStartedAt == 0) {
            lowBatteryWakeStartedAt = millis();
            Serial.printf("[power] low-battery wake window started battery=%u%% threshold=%u%%\n",
                          static_cast<unsigned>(batteryPercent),
                          static_cast<unsigned>(thresholdPercent));
        }
        if (millis() - lowBatteryWakeStartedAt < kLowBatteryWakeWindowMs) {
            return;
        }
        enterLowBatteryDeepSleep(batteryPercent, snapshot.battery.voltage, "wake window elapsed while battery still low");
        return;
    }

    enterLowBatteryDeepSleep(batteryPercent, snapshot.battery.voltage, "battery threshold reached");
}

void publishOtaStateIfNeeded(const AppStateSnapshot& snapshot) {
    if (mqttManager == nullptr || !mqttManager->isConnected()) {
        return;
    }

    const String otaSignature = String(snapshot.ota.busy ? '1' : '0') + "|" + snapshot.ota.latestVersion + "|" +
        snapshot.ota.lastResult + "|" + snapshot.ota.lastError + "|" + snapshot.ota.phase + "|" + snapshot.ota.progressPercent;
    if (otaSignature == lastPublishedOtaSignature) {
        return;
    }

    const unsigned long now = millis();
    if ((now - lastOtaMqttPublishAt) < 500UL && snapshot.ota.busy) {
        return;
    }

    lastPublishedOtaSignature = otaSignature;
    lastOtaMqttPublishAt = now;
    mqttManager->publishState();
}

void pumpOtaDisplayProgress() {
    if (appState == nullptr || displayManager == nullptr) {
        return;
    }
    const AppStateSnapshot snapshot = appState->snapshot();
    displayManager->loop(snapshot);
    publishOtaStateIfNeeded(snapshot);
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

bool playRequest(const String& url, const String& label, const String& type, const String& source, String& error, bool addToHistory) {
    const String normalizedUrl = PlaybackText::normalizeUrl(url);
    if (normalizedUrl.isEmpty()) {
        error = "URL is required";
        return false;
    }
#ifdef APP_DISABLE_AUDIO
    error = "Audio disabled in diagnostic build";
    return false;
#endif
    deferredActions->playUrl = normalizedUrl;
    deferredActions->playLabel = PlaybackText::normalizeTitle(label, normalizedUrl);
    deferredActions->playType = type;
    deferredActions->playAddToHistory = addToHistory;
    if (!source.isEmpty()) {
        deferredActions->playSource = source;
    } else {
        deferredActions->playSource = type == "tts" ? "home-assistant" : "manual";
    }
    deferredActions->playPending = true;
    deferredActions->stopPending = false;
    return true;
}

void handleMqttCommand(const PlaybackCommand& command) {
    if (command.action == "ota_check") {
        String error;
        if (otaManager != nullptr && otaManager->triggerReleaseRefresh(error)) {
            appState->setLastError("");
        } else {
            const String message = error.isEmpty() ? String("OTA release refresh request was rejected.") : error;
            if (otaManager != nullptr) {
                otaManager->reportError(message);
            }
            appState->setLastError(message);
        }
    } else if (command.action == "ota_select_version") {
        String error;
        if (otaManager != nullptr && otaManager->selectReleaseOption(command.label, error)) {
            appState->setLastError("");
        } else {
            const String message = error.isEmpty() ? String("OTA firmware selection was rejected.") : error;
            if (otaManager != nullptr) {
                otaManager->reportError(message);
            }
            appState->setLastError(message);
        }
    } else if (command.action == "ota_install_latest") {
        if (otaManager != nullptr && otaManager->triggerCheck(true)) {
            appState->setLastError("");
        } else {
            const String message = "OTA install request was rejected.";
            if (otaManager != nullptr) {
                otaManager->reportError(message);
            }
            appState->setLastError(message);
        }
    } else if (command.action == "ota_install_selected") {
        String error;
        if (otaManager != nullptr && otaManager->triggerInstallSelected(error)) {
            appState->setLastError("");
        } else {
            const String message = error.isEmpty() ? String("OTA install request was rejected.") : error;
            if (otaManager != nullptr) {
                otaManager->reportError(message);
            }
            appState->setLastError(message);
        }
    } else if (command.action == "ota_install") {
        String error;
        if (otaManager != nullptr && otaManager->triggerInstallVersion(command.version, command.assetName, error)) {
            appState->setLastError("");
        } else {
            const String message = error.isEmpty() ? String("OTA install request was rejected.") : error;
            if (otaManager != nullptr) {
                otaManager->reportError(message);
            }
            appState->setLastError(message);
        }
    } else if (command.action == "stop" || command.action == "pause") {
        audioPlayer->stop();
    } else if (command.action == "volume") {
        settings->device.savedVolumePercent = command.volumePercent;
        audioPlayer->setVolumePercent(command.volumePercent);
        soundEffects->setVolumePercent(command.volumePercent);
        deferredActions->pendingVolume = command.volumePercent;
        deferredActions->volumePending = false;
        deferredActions->volumeSavePending = true;
        deferredActions->volumeSaveAt = millis() + kVolumePersistDelayMs;
    } else if (command.action == "play" && command.url.isEmpty()) {
        const AppStateSnapshot snapshot = appState->snapshot();
        if (!snapshot.playback.url.isEmpty()) {
            String ignored;
            playRequest(snapshot.playback.url, snapshot.playback.title, snapshot.playback.type, snapshot.playback.source, ignored, true);
        }
    } else if (command.action == "playpause") {
        const AppStateSnapshot snapshot = appState->snapshot();
        if (snapshot.playback.state == "playing" || snapshot.playback.state == "buffering") {
            audioPlayer->stop();
        } else if (!snapshot.playback.url.isEmpty()) {
            String ignored;
            playRequest(snapshot.playback.url, snapshot.playback.title, snapshot.playback.type, snapshot.playback.source, ignored, true);
        }
    } else if (command.action == "next") {
        stepPlaybackHistory(1);
    } else if (command.action == "previous") {
        stepPlaybackHistory(-1);
    } else {
        String ignored;
        playRequest(command.url, command.label, command.mediaType.isEmpty() ? command.action : command.mediaType, command.source, ignored, true);
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
        deferredActions->volumePending = false;
        deferredActions->volumeSavePending = true;
        deferredActions->volumeSaveAt = millis() + kVolumePersistDelayMs;
        mqttManager->publishState();
    }

    if (deferredActions->volumeSavePending && static_cast<long>(millis() - deferredActions->volumeSaveAt) >= 0) {
        settingsManager->save(*settings);
        deferredActions->volumeSavePending = false;
    }

    if (deferredActions->playPending) {
        audioPlayer->play(
            deferredActions->playUrl,
            deferredActions->playLabel,
            deferredActions->playType,
            deferredActions->playSource);
        if (deferredActions->playAddToHistory) {
            rememberPlaybackSelection(
                deferredActions->playUrl,
                deferredActions->playLabel,
                deferredActions->playType,
                deferredActions->playSource);
        }
        deferredActions->playPending = false;
        mqttManager->publishState();
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);

    const esp_reset_reason_t resetReason = esp_reset_reason();
    loadRollbackState();
    wokeFromDeepSleep = resetReason == ESP_RST_DEEPSLEEP;
    Serial.printf("\n[boot] app=%s version=%s built=%s\n", APP_NAME, APP_VERSION, APP_BUILD_DATE);
    Serial.printf("[boot] reset reason=%s (%d)\n", resetReasonToString(resetReason), static_cast<int>(resetReason));
    if (!lastRolledBackVersion.isEmpty()) {
        Serial.printf("[rollback] previous OTA rollback detected: version=%s reason=%s\n", lastRolledBackVersion.c_str(), lastRollbackReason.c_str());
    }
    if (otaPendingVerification) {
        Serial.printf("[rollback] running pending-verify image %s\n", otaPendingVersion.c_str());
    }
    Serial.flush();

    if (!initializeRuntimeObjects()) {
        Serial.println("[boot] failed to construct runtime objects");
        Serial.flush();
        if (otaPendingVerification) {
            rollbackAndReboot("Runtime objects failed to initialize during OTA health check.");
        }
        delay(1000);
        ESP.restart();
    }

    if (!appState->begin()) {
        Serial.println("[boot] failed to initialize app state mutex");
        Serial.flush();
    }

    initializeButtons();

    pinMode(DefaultConfig::STATUS_LED_PIN, OUTPUT);
    digitalWrite(DefaultConfig::STATUS_LED_PIN, LOW);

    settingsManager->begin();
    *settings = settingsManager->load();

    displayManager->begin(settings->oled);
    displayManager->setBootMessage("Booting");

    appState->setDevice(settings->device.deviceName, settings->device.friendlyName, settings->usingSavedSettings);

    wifiManager->begin(*settings, *appState);

    batteryMonitor->begin(settings->battery, DefaultConfig::BATTERY_ADC_PIN, *appState);

    audioPlayer->begin(DefaultConfig::I2S_BCLK_PIN, DefaultConfig::I2S_WS_PIN, DefaultConfig::I2S_DOUT_PIN, settings->device.savedVolumePercent, *appState);

    soundEffects->begin(*settings);
    otaManager->begin(*settings, *appState);
    refreshRollbackStateInOtaManager();
    otaManager->setProgressCallback(pumpOtaDisplayProgress);

    mqttManager->begin(*settings, *appState, *wifiManager, *otaManager, handleMqttCommand);

    webServer->begin(
        *appState,
        *wifiManager,
        *settingsManager,
        *otaManager,
        []() { return *settings; },
        saveSettingsFromJson,
        [](const String& url, const String& label, const String& type, String& error) {
            return playRequest(url, label, type, "", error, true);
        },
        []() {
            deferredActions->stopPending = true;
            deferredActions->playPending = false;
        },
        [](uint8_t volume) {
            deferredActions->pendingVolume = volume;
            deferredActions->volumePending = true;
        },
        [](bool apply) { return otaManager->triggerCheck(apply); },
        [](bool connect, String& error) {
            return connect ? mqttManager->requestConnect(error) : mqttManager->requestDisconnect(error);
        },
        []() { scheduleReboot(500); },
        []() {
            factoryResetRequested = true;
            scheduleReboot(500);
        });

    displayManager->setBootMessage("Idle");
    soundEffects->playBoot();
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
    pollPhysicalButtons();
    wifiManager->loop();
    audioPlayer->loop();
    const bool batteryUpdated = batteryMonitor->loop(isBatterySamplingAllowed());
    otaManager->loop();
    mqttManager->loop();

    const AppStateSnapshot snapshot = appState->snapshot();
    processSoundEffectTransitions(snapshot);
    displayManager->loop(snapshot);
    handleLowBatterySleepPolicy(snapshot);
    confirmOtaHealthIfReady();

    publishOtaStateIfNeeded(snapshot);

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

    if (!recoveryRebootScheduled && wifiManager->shouldRebootForRecovery()) {
        recoveryRebootScheduled = true;
        Serial.printf("[recovery] scheduling reboot after %u failed Wi-Fi attempts\n",
                      static_cast<unsigned>(wifiManager->consecutiveFailureCount()));
        Serial.flush();
        scheduleReboot(1000);
    }

    if (!recoveryRebootScheduled && mqttManager->shouldRebootForRecovery()) {
        recoveryRebootScheduled = true;
        Serial.printf("[recovery] scheduling reboot after %u failed MQTT attempts\n",
                      static_cast<unsigned>(mqttManager->consecutiveFailureCount()));
        Serial.flush();
        scheduleReboot(1000);
    }

    if (rebootRequested && static_cast<long>(millis() - rebootAt) >= 0) {
        ESP.restart();
    }
}

#endif
