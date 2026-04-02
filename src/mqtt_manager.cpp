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

String normalizeMediaType(const String& value, bool announce) {
    String mediaType = value;
    mediaType.trim();
    mediaType.toLowerCase();

    if (announce || mediaType.indexOf("tts") >= 0 || mediaType.indexOf("announce") >= 0 || mediaType.indexOf("speech") >= 0) {
        return "tts";
    }

    if (mediaType.isEmpty()) {
        return "stream";
    }

    if (mediaType == "music" || mediaType == "audio" || mediaType == "stream" || mediaType == "media" || mediaType == "radio") {
        return "stream";
    }

    return mediaType;
}

uint8_t percentFromVolumeLevel(float level) {
    const float clamped = level < 0.0f ? 0.0f : (level > 1.0f ? 1.0f : level);
    return static_cast<uint8_t>((clamped * 100.0f) + 0.5f);
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
    if (settings_.mqtt.host.isEmpty()) {
        consecutiveFailureCount_ = 0;
        recoveryRebootRecommended_ = false;
    }
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
    if (settings_.mqtt.host.isEmpty()) {
        consecutiveFailureCount_ = 0;
        recoveryRebootRecommended_ = false;
    }

    if (!connectionEnabled_) {
        return;
    }

    if (!settings_.mqtt.host.isEmpty() && wifiManager_ != nullptr && wifiManager_->isConnected()) {
        Serial.printf("[mqtt] immediate reconnect attempt %u/%u to %s:%u\n",
                      static_cast<unsigned>(consecutiveFailureCount_ + 1),
                      static_cast<unsigned>(MQTT_MAX_CONSECUTIVE_FAILURES),
                      settings_.mqtt.host.c_str(), settings_.mqtt.port);
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
    Serial.printf("[mqtt] connect attempt %u/%u to %s:%u\n",
                  static_cast<unsigned>(consecutiveFailureCount_ + 1),
                  static_cast<unsigned>(MQTT_MAX_CONSECUTIVE_FAILURES),
                  settings_.mqtt.host.c_str(), settings_.mqtt.port);
    lastConnectAttemptAt_ = millis();
    client_.connect();
}

void MqttManager::handleConnected(bool sessionPresent) {
    (void)sessionPresent;
    Serial.printf("[mqtt] connected host=%s port=%u\n", settings_.mqtt.host.c_str(), settings_.mqtt.port);
    consecutiveFailureCount_ = 0;
    recoveryRebootRecommended_ = false;
    clearFrontendError();
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
    if (appState_ != nullptr) {
        appState_->setMqttConnected(false);
    }
    registerFailedAttempt(reason);
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
                if (!doc["volumePercent"].isNull() || !doc["volume"].isNull()) {
                    command.volumePercent = doc["volumePercent"] | doc["volume"] | 0;
                } else if (!doc["volume_level"].isNull()) {
                    command.volumePercent = percentFromVolumeLevel(doc["volume_level"] | 0.0f);
                }
            }
        } else {
            command.volumePercent = payloadValue.indexOf('.') >= 0
                ? percentFromVolumeLevel(payloadValue.toFloat())
                : payloadValue.toInt();
        }
        commandHandler_(command);
        return;
    }

    command.action = topicValue.endsWith("/tts") ? "tts" : "play";
    if (payloadValue.startsWith("{")) {
        JsonDocument doc;
        if (deserializeJson(doc, payloadValue) == DeserializationError::Ok) {
            const bool announce = doc["announce"] | false;
            const String mediaContentType = String(static_cast<const char*>(doc["media_content_type"] | doc["media_type"] | ""));
            const String explicitType = String(static_cast<const char*>(doc["type"] | ""));
            command.url = String(static_cast<const char*>(doc["url"] | doc["media_content_id"] | doc["media_id"] | ""));
            command.label = String(static_cast<const char*>(doc["label"] | doc["title"] | doc["media_title"] | doc["extra"]["title"] | ""));
            command.source = String(static_cast<const char*>(doc["source"] | ""));
            command.mediaType = normalizeMediaType(
                explicitType.isEmpty() ? mediaContentType : explicitType,
                command.action == "tts" || announce);
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
        HaBridge::discoveryPayloadSensor(settings_, "battery_voltage", "Battery Voltage", HaBridge::batteryStateTopic(settings_).c_str(), "{{ value_json.voltage | float(0) | round(2) }}", "V", "voltage", "measurement", "mdi:battery", 2).c_str());
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

bool MqttManager::shouldRebootForRecovery() const {
    return recoveryRebootRecommended_;
}

uint8_t MqttManager::consecutiveFailureCount() const {
    return consecutiveFailureCount_;
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
    consecutiveFailureCount_ = 0;
    recoveryRebootRecommended_ = false;
    lastConnectAttemptAt_ = millis();
    client_.disconnect(true);
    if (appState_ != nullptr) {
        appState_->setMqttConnected(false);
    }
    return true;
}

void MqttManager::registerFailedAttempt(AsyncMqttClientDisconnectReason reason) {
    if (!connectionEnabled_ || settings_.mqtt.host.isEmpty() || client_.connected()) {
        return;
    }

    if (wifiManager_ == nullptr || !wifiManager_->isConnected()) {
        return;
    }

    if (consecutiveFailureCount_ < MQTT_MAX_CONSECUTIVE_FAILURES) {
        ++consecutiveFailureCount_;
    }

    Serial.printf("[mqtt] connect failed reason=%d count=%u/%u\n", static_cast<int>(reason),
                  static_cast<unsigned>(consecutiveFailureCount_),
                  static_cast<unsigned>(MQTT_MAX_CONSECUTIVE_FAILURES));

    if (isCredentialFailureReason(reason)) {
        setFrontendError("MQTT broker rejected the configured client ID or credentials.");
        recoveryRebootRecommended_ = false;
        return;
    }

    if (consecutiveFailureCount_ >= MQTT_MAX_CONSECUTIVE_FAILURES) {
        clearFrontendError();
        recoveryRebootRecommended_ = true;
        Serial.println("[mqtt] max consecutive failures reached, recovery reboot recommended");
    }
}

bool MqttManager::isCredentialFailureReason(AsyncMqttClientDisconnectReason reason) const {
    switch (reason) {
        case AsyncMqttClientDisconnectReason::MQTT_UNACCEPTABLE_PROTOCOL_VERSION:
        case AsyncMqttClientDisconnectReason::MQTT_IDENTIFIER_REJECTED:
        case AsyncMqttClientDisconnectReason::MQTT_MALFORMED_CREDENTIALS:
        case AsyncMqttClientDisconnectReason::MQTT_NOT_AUTHORIZED:
            return true;
        default:
            return false;
    }
}

void MqttManager::clearFrontendError() {
    if (appState_ != nullptr) {
        appState_->setLastError("");
    }
}

void MqttManager::setFrontendError(const String& message) {
    if (appState_ != nullptr) {
        appState_->setLastError(message);
    }
}
