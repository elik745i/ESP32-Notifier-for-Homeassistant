#include "app_state.h"

AppState::AppState() : mutex_(xSemaphoreCreateMutex()) {}

AppState::~AppState() {
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
    }
}

void AppState::setDevice(const String& deviceName, const String& friendlyName, bool usingSaved) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    state_.device.deviceName = deviceName;
    state_.device.friendlyName = friendlyName;
    state_.settings.usingSaved = usingSaved;
    xSemaphoreGive(mutex_);
}

void AppState::setWiFiStatus(bool connected, bool apMode, const String& ssid, const IPAddress& ip, int32_t rssi, const String& apSsid) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    state_.network.wifiConnected = connected;
    state_.network.apMode = apMode;
    state_.network.ssid = ssid;
    state_.network.ip = ip.toString();
    state_.network.wifiRssi = rssi;
    state_.network.apSsid = apSsid;
    xSemaphoreGive(mutex_);
}

void AppState::setMqttConnected(bool connected) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    state_.network.mqttConnected = connected;
    xSemaphoreGive(mutex_);
}

void AppState::setPlayback(const String& state, const String& type, const String& title, const String& url, const String& source, uint8_t volumePercent) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    state_.playback.state = state;
    state_.playback.type = type;
    state_.playback.title = title;
    state_.playback.url = url;
    state_.playback.source = source;
    state_.playback.volumePercent = volumePercent;
    xSemaphoreGive(mutex_);
}

void AppState::setBattery(float voltage, float rawAdcVoltage, uint16_t rawAdc) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    state_.battery.voltage = voltage;
    state_.battery.rawAdcVoltage = rawAdcVoltage;
    state_.battery.rawAdc = rawAdc;
    xSemaphoreGive(mutex_);
}

void AppState::setOta(bool updateAvailable, const String& latestVersion, const String& lastResult, const String& lastError) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    state_.ota.updateAvailable = updateAvailable;
    state_.ota.latestVersion = latestVersion;
    state_.ota.lastResult = lastResult;
    state_.ota.lastError = lastError;
    xSemaphoreGive(mutex_);
}

void AppState::setLastError(const String& lastError) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    state_.system.lastError = lastError;
    xSemaphoreGive(mutex_);
}

void AppState::setFreeHeap(uint32_t freeHeap) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    state_.system.freeHeap = freeHeap;
    xSemaphoreGive(mutex_);
}

AppStateSnapshot AppState::snapshot() const {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    AppStateSnapshot copy = state_;
    xSemaphoreGive(mutex_);
    return copy;
}

void AppState::toJson(JsonObject root) const {
    const AppStateSnapshot copy = snapshot();
    JsonObject device = root["device"].to<JsonObject>();
    device["deviceName"] = copy.device.deviceName;
    device["friendlyName"] = copy.device.friendlyName;

    JsonObject network = root["network"].to<JsonObject>();
    network["wifiConnected"] = copy.network.wifiConnected;
    network["apMode"] = copy.network.apMode;
    network["mqttConnected"] = copy.network.mqttConnected;
    network["ssid"] = copy.network.ssid;
    network["ip"] = copy.network.ip;
    network["apSsid"] = copy.network.apSsid;
    network["wifiRssi"] = copy.network.wifiRssi;

    JsonObject playback = root["playback"].to<JsonObject>();
    playback["state"] = copy.playback.state;
    playback["type"] = copy.playback.type;
    playback["title"] = copy.playback.title;
    playback["url"] = copy.playback.url;
    playback["source"] = copy.playback.source;
    playback["volumePercent"] = copy.playback.volumePercent;

    JsonObject battery = root["battery"].to<JsonObject>();
    battery["voltage"] = copy.battery.voltage;
    battery["rawAdcVoltage"] = copy.battery.rawAdcVoltage;
    battery["rawAdc"] = copy.battery.rawAdc;

    JsonObject ota = root["ota"].to<JsonObject>();
    ota["updateAvailable"] = copy.ota.updateAvailable;
    ota["latestVersion"] = copy.ota.latestVersion;
    ota["lastResult"] = copy.ota.lastResult;
    ota["lastError"] = copy.ota.lastError;

    JsonObject settings = root["settings"].to<JsonObject>();
    settings["usingSaved"] = copy.settings.usingSaved;

    JsonObject system = root["system"].to<JsonObject>();
    system["freeHeap"] = copy.system.freeHeap;
    system["lastError"] = copy.system.lastError;
}
