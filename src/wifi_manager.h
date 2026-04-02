#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <IPAddress.h>
#include <WiFi.h>

#include "app_state.h"
#include "settings_schema.h"

class WiFiManager {
  public:
    struct ScanSnapshot {
        bool active = false;
        bool complete = false;
        bool failed = false;
        uint32_t ageMs = 0;
    };

    void begin(const SettingsBundle& settings, AppState& appState);
    void applySettings(const SettingsBundle& settings);
    void loop();

    bool isConnected() const;
    bool isApMode() const;
    IPAddress localIp() const;
    IPAddress apIp() const;
    String currentSsid() const;
    String apSsid() const;
    int32_t rssi() const;
    ScanSnapshot getScanSnapshot() const;
    bool startScan();
    void appendScanResultsJson(JsonArray networks);
    bool shouldRedirectCaptivePortal(const String& hostHeader) const;

  private:
    static constexpr uint16_t DNS_PORT = 53;
    static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
    static constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 15000;

    SettingsBundle settings_;
    AppState* appState_ = nullptr;
    DNSServer dnsServer_;
    bool initialized_ = false;
    bool dnsStarted_ = false;
    bool apMode_ = false;
    bool stationAttemptActive_ = false;
    bool hadConnection_ = false;
    bool lastScanCompleted_ = false;
    unsigned long connectAttemptStartedAt_ = 0;
    unsigned long lastConnectAttemptAt_ = 0;
    unsigned long lastScanStartedAt_ = 0;
    String apSsid_;

    void startStation();
    void startAccessPoint();
    void stopAccessPoint();
    void updateAppState();
    bool hasStaCredentials() const;
};
