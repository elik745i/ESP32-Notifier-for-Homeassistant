#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "app_state.h"
#include "settings_schema.h"

class OtaManager {
  public:
    void begin(const SettingsBundle& settings, AppState& appState);
    void applySettings(const SettingsBundle& settings);
    void loop();
    bool triggerCheck(bool applyAfterCheck);
    bool beginLocalUpload(const String& filename, size_t totalSize, String& error);
    bool writeLocalUploadChunk(const uint8_t* data, size_t len, String& error);
    bool finishLocalUpload(String& error);
    void abortLocalUpload(const String& error);
    void appendStatusJson(JsonObject root) const;

  private:
    struct CheckResult {
        bool updateAvailable = false;
        bool success = false;
        String latestVersion;
        String assetUrl;
        String assetName;
        String checksumSha256;
        String message;
    };

    SettingsBundle settings_;
    AppState* appState_ = nullptr;
    volatile bool pendingCheck_ = false;
    volatile bool pendingApply_ = false;
    bool busy_ = false;
    String lastMessage_ = "idle";
    String latestVersion_;
    bool updateAvailable_ = false;
    String selectedVersion_;
    String updatePhase_;
    size_t progressBytes_ = 0;
    size_t progressTotalBytes_ = 0;
    uint8_t progressPercent_ = 0;
    bool localUploadStarted_ = false;
    bool localUploadHadData_ = false;
    bool localUploadOk_ = false;
    bool rebootPending_ = false;
    unsigned long rebootAtMs_ = 0;

    void runTask(bool applyAfterCheck);
    CheckResult checkNow();
    bool installNow(const CheckResult& result, String& message);
    int compareVersions(const String& left, const String& right) const;
    String normalizeVersion(const String& value) const;
    bool hasBinExtension(const String& filename) const;
    void resetProgress();
    void scheduleReboot(unsigned long delayMs);
    void syncAppState(const String& lastResult, const String& lastError = "");
};
