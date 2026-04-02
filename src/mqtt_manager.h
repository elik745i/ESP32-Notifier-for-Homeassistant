#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncMqttClient.h>
#include <functional>

#include "app_state.h"
#include "ha_bridge.h"
#include "settings_schema.h"
#include "wifi_manager.h"

class MqttManager {
  public:
    using CommandHandler = std::function<void(const PlaybackCommand& command)>;

    void begin(const SettingsBundle& settings, AppState& appState, WiFiManager& wifiManager, CommandHandler commandHandler);
    void applySettings(const SettingsBundle& settings);
    void loop();
    void publishState();
    void publishBattery(float voltage, float rawAdcVoltage, uint16_t rawAdc);
    void publishDiscovery();
    bool isConnected() const;
    bool requestConnect(String& error);
    bool requestDisconnect(String& error);

  private:
    AsyncMqttClient client_;
    SettingsBundle settings_;
    AppState* appState_ = nullptr;
    WiFiManager* wifiManager_ = nullptr;
    CommandHandler commandHandler_;
    bool configured_ = false;
    bool connectionEnabled_ = true;
    unsigned long lastConnectAttemptAt_ = 0;
    unsigned long lastStatePublishAt_ = 0;

    void configureClient();
    void connectIfNeeded();
    void handleConnected(bool sessionPresent);
    void handleDisconnected(AsyncMqttClientDisconnectReason reason);
    void handleMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total);
    void publishJson(const String& topic, const JsonDocument& doc, bool retained);
};
