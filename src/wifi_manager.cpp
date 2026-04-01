#include "wifi_manager.h"

#include "default_config.h"

namespace {
String defaultApName() {
    uint64_t chipId = ESP.getEfuseMac();
    uint32_t shortId = static_cast<uint32_t>(chipId & 0xFFFFFF);
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%s-%06lX", DefaultConfig::WIFI_AP_SSID_PREFIX, static_cast<unsigned long>(shortId));
    return String(buffer);
}
}  // namespace

void WiFiManager::begin(const SettingsBundle& settings, AppState& appState) {
    appState_ = &appState;
    WiFi.mode(WIFI_MODE_APSTA);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);
    applySettings(settings);
}

void WiFiManager::applySettings(const SettingsBundle& settings) {
    settings_ = settings;
    if (settings_.device.deviceName.length() > 0) {
        WiFi.setHostname(settings_.device.deviceName.c_str());
    }
    apSsid_ = defaultApName();
    stopAccessPoint();
    startStation();
}

void WiFiManager::startStation() {
    WiFi.disconnect(true, true);
    delay(100);
    if (!hasStaCredentials()) {
        stationAttemptActive_ = false;
        startAccessPoint();
        updateAppState();
        return;
    }

    if (settings_.wifi.useStaticIp) {
        IPAddress ip;
        IPAddress gateway;
        IPAddress subnet;
        IPAddress dns1;
        IPAddress dns2;
        if (parseIp(settings_.wifi.staticIp, ip) && parseIp(settings_.wifi.gateway, gateway) && parseIp(settings_.wifi.subnet, subnet)) {
            const bool hasDns1 = parseIp(settings_.wifi.dns1, dns1);
            const bool hasDns2 = parseIp(settings_.wifi.dns2, dns2);
            WiFi.config(ip, gateway, subnet, hasDns1 ? dns1 : gateway, hasDns2 ? dns2 : gateway);
        }
    } else {
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
    }

    WiFi.begin(settings_.wifi.ssid.c_str(), settings_.wifi.password.c_str());
    stationAttemptActive_ = true;
    connectAttemptStartedAt_ = millis();
    lastConnectAttemptAt_ = connectAttemptStartedAt_;
    updateAppState();
}

void WiFiManager::startAccessPoint() {
    if (!settings_.wifi.apFallbackEnabled && hasStaCredentials()) {
        return;
    }
    if (apMode_) {
        return;
    }
    WiFi.softAP(apSsid_.c_str(), DefaultConfig::WIFI_AP_PASSWORD);
    dnsServer_.start(DNS_PORT, "*", WiFi.softAPIP());
    dnsStarted_ = true;
    apMode_ = true;
    updateAppState();
}

void WiFiManager::stopAccessPoint() {
    if (dnsStarted_) {
        dnsServer_.stop();
        dnsStarted_ = false;
    }
    if (apMode_) {
        WiFi.softAPdisconnect(true);
        apMode_ = false;
    }
}

void WiFiManager::loop() {
    if (dnsStarted_) {
        dnsServer_.processNextRequest();
    }

    const bool connected = WiFi.status() == WL_CONNECTED;
    if (connected) {
        if (!hadConnection_) {
            stopAccessPoint();
        }
        hadConnection_ = true;
        stationAttemptActive_ = false;
        updateAppState();
        return;
    }

    if (hadConnection_) {
        hadConnection_ = false;
        if (settings_.wifi.apFallbackEnabled) {
            startAccessPoint();
        }
    }

    const unsigned long now = millis();
    if (stationAttemptActive_ && now - connectAttemptStartedAt_ > WIFI_CONNECT_TIMEOUT_MS) {
        stationAttemptActive_ = false;
        startAccessPoint();
    }

    if (!stationAttemptActive_ && hasStaCredentials() && now - lastConnectAttemptAt_ > WIFI_RETRY_INTERVAL_MS) {
        startStation();
    }

    updateAppState();
}

bool WiFiManager::hasStaCredentials() const {
    return !settings_.wifi.ssid.isEmpty();
}

void WiFiManager::updateAppState() {
    if (appState_ == nullptr) {
        return;
    }
    const bool connected = isConnected();
    appState_->setWiFiStatus(
        connected,
        apMode_,
        connected ? WiFi.SSID() : settings_.wifi.ssid,
        connected ? WiFi.localIP() : (apMode_ ? WiFi.softAPIP() : IPAddress()),
        connected ? WiFi.RSSI() : 0,
        apSsid_);
}

bool WiFiManager::isConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

bool WiFiManager::isApMode() const {
    return apMode_;
}

IPAddress WiFiManager::localIp() const {
    return WiFi.localIP();
}

IPAddress WiFiManager::apIp() const {
    return WiFi.softAPIP();
}

String WiFiManager::currentSsid() const {
    return isConnected() ? WiFi.SSID() : settings_.wifi.ssid;
}

String WiFiManager::apSsid() const {
    return apSsid_;
}

int32_t WiFiManager::rssi() const {
    return isConnected() ? WiFi.RSSI() : 0;
}

bool WiFiManager::shouldRedirectCaptivePortal(const String& hostHeader) const {
    if (!apMode_) {
        return false;
    }
    if (hostHeader.length() == 0) {
        return false;
    }
    if (hostHeader == WiFi.softAPIP().toString()) {
        return false;
    }
    if (hostHeader == WiFi.localIP().toString()) {
        return false;
    }
    if (hostHeader.indexOf('.') < 0) {
        return false;
    }
    return true;
}
