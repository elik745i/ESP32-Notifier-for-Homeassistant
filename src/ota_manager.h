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

    void runTask(bool applyAfterCheck);
    CheckResult checkNow();
    bool installNow(const CheckResult& result, String& message);
    int compareVersions(const String& left, const String& right) const;
    String normalizeVersion(const String& value) const;
};
