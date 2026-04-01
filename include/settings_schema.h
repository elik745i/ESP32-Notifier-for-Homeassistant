#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <stdint.h>

struct WiFiSettings {
    String ssid;
    String password;
    bool apFallbackEnabled = true;
    bool useStaticIp = false;
    String staticIp;
    String gateway;
    String subnet;
    String dns1;
    String dns2;
};

struct MqttSettings {
    String host;
    uint16_t port = 1883;
    String username;
    String password;
    String clientId;
    String baseTopic;
    bool discoveryEnabled = true;
};

struct OtaSettings {
    String owner;
    String repository;
    String channel;
    String assetTemplate;
    String manifestUrl;
    bool allowInsecureTls = true;
    bool autoCheck = false;
};

struct BatterySettings {
    float dividerRatio = 2.0f;
    float calibrationMultiplier = 1.0f;
    float smoothingAlpha = 0.2f;
    float minVoltageClamp = 2.8f;
    float maxVoltageClamp = 4.35f;
    uint32_t updateIntervalMs = 10000;
    uint16_t sampleCount = 8;
};

struct WebAuthSettings {
    bool enabled = false;
    String username;
    String password;
};

struct OledSettings {
    bool enabled = true;
    String driver;
    uint8_t i2cAddress = 0x3C;
    uint8_t width = 128;
    uint8_t height = 64;
    uint8_t sdaPin = 21;
    uint8_t sclPin = 19;
    int8_t resetPin = -1;
    uint16_t dimTimeoutSeconds = 0;
};

struct DeviceSettings {
    String deviceName;
    String friendlyName;
    uint8_t savedVolumePercent = 60;
};

struct SettingsBundle {
    WiFiSettings wifi;
    MqttSettings mqtt;
    OtaSettings ota;
    BatterySettings battery;
    WebAuthSettings webAuth;
    OledSettings oled;
    DeviceSettings device;
    bool usingSavedSettings = false;
};

inline bool parseIp(const String& raw, IPAddress& address) {
    if (raw.isEmpty()) {
        return false;
    }
    return address.fromString(raw);
}
