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

bool wifiRestartRequired(const SettingsBundle& current, const SettingsBundle& next) {
    return current.device.deviceName != next.device.deviceName ||
           current.wifi.ssid != next.wifi.ssid ||
           current.wifi.password != next.wifi.password ||
           current.wifi.apFallbackEnabled != next.wifi.apFallbackEnabled ||
           current.wifi.apSsid != next.wifi.apSsid ||
           current.wifi.apPassword != next.wifi.apPassword ||
           current.wifi.useStaticIp != next.wifi.useStaticIp ||
           current.wifi.staticIp != next.wifi.staticIp ||
           current.wifi.gateway != next.wifi.gateway ||
           current.wifi.subnet != next.wifi.subnet ||
           current.wifi.dns1 != next.wifi.dns1 ||
           current.wifi.dns2 != next.wifi.dns2;
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
    const bool needsRestart = !initialized_ || wifiRestartRequired(settings_, settings);
    settings_ = settings;
    if (settings_.device.deviceName.length() > 0) {
        WiFi.setHostname(settings_.device.deviceName.c_str());
    }
    apSsid_ = settings_.wifi.apSsid.isEmpty() ? defaultApName() : settings_.wifi.apSsid;
    if (!needsRestart) {
        updateAppState();
        initialized_ = true;
        return;
    }

    stopAccessPoint();
    startStation();
    initialized_ = true;
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
    WiFi.softAP(apSsid_.c_str(), settings_.wifi.apPassword.c_str());
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

WiFiManager::ScanSnapshot WiFiManager::getScanSnapshot() const {
    const int scanState = WiFi.scanComplete();
    ScanSnapshot snapshot;
    snapshot.active = scanState == WIFI_SCAN_RUNNING;
    snapshot.complete = scanState >= 0 || (lastScanCompleted_ && lastScanStartedAt_ != 0);
    snapshot.failed = lastScanStartedAt_ != 0 && !lastScanCompleted_ && scanState == WIFI_SCAN_FAILED;
    snapshot.ageMs = lastScanStartedAt_ == 0 ? 0 : millis() - lastScanStartedAt_;
    return snapshot;
}

bool WiFiManager::startScan() {
    const int scanState = WiFi.scanComplete();
    if (scanState == WIFI_SCAN_RUNNING) {
        return true;
    }
    if (scanState >= 0 || scanState == WIFI_SCAN_FAILED) {
        WiFi.scanDelete();
    }
    lastScanCompleted_ = false;
    WiFi.enableSTA(true);
    const int scanResult = WiFi.scanNetworks(true, true);
    if (scanResult == WIFI_SCAN_FAILED) {
        delay(50);
        const int retryResult = WiFi.scanNetworks(true, true);
        if (retryResult == WIFI_SCAN_FAILED) {
            lastScanStartedAt_ = 0;
            return false;
        }
    }
    lastScanStartedAt_ = millis();
    return true;
}

void WiFiManager::appendScanResultsJson(JsonArray networks) {
    const int networkCount = WiFi.scanComplete();
    if (networkCount <= 0) {
        if (networkCount == WIFI_SCAN_FAILED) {
            WiFi.scanDelete();
        }
        return;
    }

    lastScanCompleted_ = true;

    for (int index = 0; index < networkCount; ++index) {
        const String ssid = WiFi.SSID(index);
        if (ssid.isEmpty()) {
            continue;
        }

        JsonObject network = networks.add<JsonObject>();
        network["ssid"] = ssid;
        network["rssi"] = WiFi.RSSI(index);
        network["encrypted"] = WiFi.encryptionType(index) != WIFI_AUTH_OPEN;
    }

    WiFi.scanDelete();
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
