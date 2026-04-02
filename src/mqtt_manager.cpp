#include "mqtt_manager.h"

namespace {
String payloadToString(char* payload, size_t len) {
    String value;
    value.reserve(len);
    for (size_t index = 0; index < len; ++index) {
        value += payload[index];
    }
    return value;
}

bool mqttReconnectRequired(const SettingsBundle& current, const SettingsBundle& next) {
    return current.mqtt.host != next.mqtt.host ||
           current.mqtt.port != next.mqtt.port ||
           current.mqtt.username != next.mqtt.username ||
           current.mqtt.password != next.mqtt.password ||
           current.mqtt.clientId != next.mqtt.clientId ||
           current.device.deviceName != next.device.deviceName;
}
}  // namespace

void MqttManager::begin(const SettingsBundle& settings, AppState& appState, WiFiManager& wifiManager, CommandHandler commandHandler) {
    appState_ = &appState;
    wifiManager_ = &wifiManager;
    commandHandler_ = commandHandler;

    client_.onConnect([this](bool sessionPresent) { handleConnected(sessionPresent); });
    client_.onDisconnect([this](AsyncMqttClientDisconnectReason reason) { handleDisconnected(reason); });
    client_.onMessage([this](char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
        handleMessage(topic, payload, properties, len, index, total);
    });
    applySettings(settings);
}

void MqttManager::applySettings(const SettingsBundle& settings) {
    const bool needsReconfigure = !configured_ || mqttReconnectRequired(settings_, settings) || !client_.connected();
    settings_ = settings;
    if (needsReconfigure) {
        configureClient();
        configured_ = true;
        return;
    }

    if (client_.connected()) {
        publishDiscovery();
        publishState();
    }
}

void MqttManager::configureClient() {
    client_.disconnect(true);
    client_.setServer(settings_.mqtt.host.c_str(), settings_.mqtt.port);
    client_.setClientId(settings_.mqtt.clientId.isEmpty() ? settings_.device.deviceName.c_str() : settings_.mqtt.clientId.c_str());
    client_.setCredentials(
        settings_.mqtt.username.isEmpty() ? nullptr : settings_.mqtt.username.c_str(),
        settings_.mqtt.username.isEmpty() ? nullptr : settings_.mqtt.password.c_str());
    client_.setKeepAlive(30);
    client_.setWill(HaBridge::availabilityTopic(settings_).c_str(), 1, true, "offline");
    lastConnectAttemptAt_ = 0;

    if (!connectionEnabled_) {
        return;
    }

    if (!settings_.mqtt.host.isEmpty() && wifiManager_ != nullptr && wifiManager_->isConnected()) {
        Serial.printf("[mqtt] immediate reconnect to %s:%u\n", settings_.mqtt.host.c_str(), settings_.mqtt.port);
        lastConnectAttemptAt_ = millis();
        client_.connect();
    }
}

void MqttManager::loop() {
    connectIfNeeded();
    if (isConnected() && millis() - lastStatePublishAt_ > 30000UL) {
        publishState();
    }
}

void MqttManager::connectIfNeeded() {
    if (!connectionEnabled_ || settings_.mqtt.host.isEmpty() || wifiManager_ == nullptr || !wifiManager_->isConnected() || client_.connected()) {
        return;
    }
    if (millis() - lastConnectAttemptAt_ < 5000UL) {
        return;
    }
    Serial.printf("[mqtt] connect attempt to %s:%u\n", settings_.mqtt.host.c_str(), settings_.mqtt.port);
    lastConnectAttemptAt_ = millis();
    client_.connect();
}

void MqttManager::handleConnected(bool sessionPresent) {
    (void)sessionPresent;
    Serial.printf("[mqtt] connected host=%s port=%u\n", settings_.mqtt.host.c_str(), settings_.mqtt.port);
    if (appState_ != nullptr) {
        appState_->setMqttConnected(true);
    }
    client_.publish(HaBridge::availabilityTopic(settings_).c_str(), 1, true, "online");
    client_.subscribe(HaBridge::commandTopic(settings_, "play").c_str(), 1);
    client_.subscribe(HaBridge::commandTopic(settings_, "tts").c_str(), 1);
    client_.subscribe(HaBridge::commandTopic(settings_, "stop").c_str(), 1);
    client_.subscribe(HaBridge::commandTopic(settings_, "volume").c_str(), 1);
    publishDiscovery();
    publishState();
}

void MqttManager::handleDisconnected(AsyncMqttClientDisconnectReason reason) {
    Serial.printf("[mqtt] disconnected reason=%d\n", static_cast<int>(reason));
    (void)reason;
    if (appState_ != nullptr) {
        appState_->setMqttConnected(false);
    }
}

void MqttManager::handleMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
    (void)properties;
    if (index != 0 || total != len || commandHandler_ == nullptr) {
        return;
    }

    const String topicValue = topic;
    const String payloadValue = payloadToString(payload, len);
    PlaybackCommand command;
    if (topicValue == HaBridge::commandTopic(settings_, "stop")) {
        command.action = "stop";
        commandHandler_(command);
        return;
    }

    if (topicValue == HaBridge::commandTopic(settings_, "volume")) {
        command.action = "volume";
        if (payloadValue.startsWith("{")) {
            JsonDocument doc;
            if (deserializeJson(doc, payloadValue) == DeserializationError::Ok) {
                command.volumePercent = doc["volumePercent"] | doc["volume"] | 0;
            }
        } else {
            command.volumePercent = payloadValue.toInt();
        }
        commandHandler_(command);
        return;
    }

    command.action = topicValue.endsWith("/tts") ? "tts" : "play";
    if (payloadValue.startsWith("{")) {
        JsonDocument doc;
        if (deserializeJson(doc, payloadValue) == DeserializationError::Ok) {
            command.url = String(static_cast<const char*>(doc["url"] | ""));
            command.label = String(static_cast<const char*>(doc["label"] | ""));
            command.mediaType = String(static_cast<const char*>(doc["type"] | (command.action == "tts" ? "tts" : "stream")));
        }
    } else {
        command.url = payloadValue;
        command.mediaType = command.action == "tts" ? "tts" : "stream";
    }
    commandHandler_(command);
}

void MqttManager::publishJson(const String& topic, const JsonDocument& doc, bool retained) {
    if (!client_.connected()) {
        return;
    }
    String payload;
    serializeJson(doc, payload);
    client_.publish(topic.c_str(), 1, retained, payload.c_str());
}

void MqttManager::publishState() {
    if (!client_.connected() || appState_ == nullptr) {
        return;
    }
    lastStatePublishAt_ = millis();
    const AppStateSnapshot snapshot = appState_->snapshot();

    JsonDocument playback;
    playback["state"] = snapshot.playback.state;
    playback["type"] = snapshot.playback.type;
    playback["title"] = snapshot.playback.title;
    playback["url"] = snapshot.playback.url;
    playback["source"] = snapshot.playback.source;
    playback["volumePercent"] = snapshot.playback.volumePercent;
    publishJson(HaBridge::playbackStateTopic(settings_), playback, true);

    JsonDocument network;
    network["wifiConnected"] = snapshot.network.wifiConnected;
    network["apMode"] = snapshot.network.apMode;
    network["ip"] = snapshot.network.ip;
    network["ssid"] = snapshot.network.ssid;
    network["wifiRssi"] = snapshot.network.wifiRssi;
    network["mqttConnected"] = snapshot.network.mqttConnected;
    publishJson(HaBridge::networkStateTopic(settings_), network, true);

    JsonDocument battery;
    battery["voltage"] = snapshot.battery.voltage;
    battery["rawAdcVoltage"] = snapshot.battery.rawAdcVoltage;
    battery["rawAdc"] = snapshot.battery.rawAdc;
    publishJson(HaBridge::batteryStateTopic(settings_), battery, true);

    client_.publish((settings_.mqtt.baseTopic + "/state/volume").c_str(), 1, true, String(snapshot.playback.volumePercent).c_str());
}

void MqttManager::publishBattery(float voltage, float rawAdcVoltage, uint16_t rawAdc) {
    if (!client_.connected()) {
        return;
    }
    JsonDocument battery;
    battery["voltage"] = voltage;
    battery["rawAdcVoltage"] = rawAdcVoltage;
    battery["rawAdc"] = rawAdc;
    publishJson(HaBridge::batteryStateTopic(settings_), battery, true);
}

void MqttManager::publishDiscovery() {
    if (!client_.connected() || !settings_.mqtt.discoveryEnabled) {
        return;
    }
    client_.publish(
        HaBridge::discoveryTopic(settings_, "sensor", "battery_voltage").c_str(), 1, true,
        HaBridge::discoveryPayloadSensor(settings_, "battery_voltage", "Battery Voltage", HaBridge::batteryStateTopic(settings_).c_str(), "{{ value_json.voltage }}", "V", "voltage", "measurement", "mdi:battery", 2).c_str());
    client_.publish(
        HaBridge::discoveryTopic(settings_, "sensor", "wifi_rssi").c_str(), 1, true,
        HaBridge::discoveryPayloadSensor(settings_, "wifi_rssi", "Wi-Fi RSSI", HaBridge::networkStateTopic(settings_).c_str(), "{{ value_json.wifiRssi }}", "dBm", "signal_strength", "measurement", "mdi:wifi").c_str());
    client_.publish(
        HaBridge::discoveryTopic(settings_, "sensor", "playback_state").c_str(), 1, true,
        HaBridge::discoveryPayloadSensor(settings_, "playback_state", "Playback State", HaBridge::playbackStateTopic(settings_).c_str(), "{{ value_json.state }}", nullptr, nullptr, nullptr, "mdi:speaker-wireless").c_str());
    client_.publish(
        HaBridge::discoveryTopic(settings_, "number", "volume").c_str(), 1, true,
        HaBridge::discoveryPayloadNumber(settings_, "volume", "Notifier Volume", (settings_.mqtt.baseTopic + "/state/volume").c_str(), HaBridge::commandTopic(settings_, "volume").c_str(), 0, 100, 1, "%", "mdi:volume-high").c_str());
    client_.publish(
        HaBridge::discoveryTopic(settings_, "button", "stop").c_str(), 1, true,
        HaBridge::discoveryPayloadButton(settings_, "stop", "Stop Playback", HaBridge::commandTopic(settings_, "stop").c_str(), "stop", "mdi:stop").c_str());
    client_.publish(
        HaBridge::discoveryTopic(settings_, "text", "play_url").c_str(), 1, true,
        HaBridge::discoveryPayloadText(settings_, "play_url", "Play URL", HaBridge::commandTopic(settings_, "play").c_str(), "mdi:link").c_str());
}

bool MqttManager::isConnected() const {
    return client_.connected();
}

bool MqttManager::requestConnect(String& error) {
    if (settings_.mqtt.host.isEmpty()) {
        error = "Enter an MQTT host first.";
        return false;
    }

    connectionEnabled_ = true;
    if (client_.connected()) {
        publishDiscovery();
        publishState();
        return true;
    }

    configureClient();
    return true;
}

bool MqttManager::requestDisconnect(String& error) {
    error = "";
    connectionEnabled_ = false;
    lastConnectAttemptAt_ = millis();
    client_.disconnect(true);
    if (appState_ != nullptr) {
        appState_->setMqttConnected(false);
    }
    return true;
}
