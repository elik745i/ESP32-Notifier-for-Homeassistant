#include "ha_bridge.h"

#include <ArduinoJson.h>

#include "version.h"

namespace {
void fillDevice(const SettingsBundle& settings, JsonObject device) {
    JsonArray ids = device["identifiers"].to<JsonArray>();
    ids.add(settings.device.deviceName);
    device["name"] = settings.device.friendlyName;
    device["manufacturer"] = "DIY";
    device["model"] = "ESP32 Wi-Fi Audio Notifier";
    device["sw_version"] = APP_VERSION;
}

String baseDiscoveryPrefix() {
    return "homeassistant";
}
}  // namespace

namespace HaBridge {

String availabilityTopic(const SettingsBundle& settings) {
    return settings.mqtt.baseTopic + "/availability";
}

String playbackStateTopic(const SettingsBundle& settings) {
    return settings.mqtt.baseTopic + "/state/playback";
}

String networkStateTopic(const SettingsBundle& settings) {
    return settings.mqtt.baseTopic + "/state/network";
}

String batteryStateTopic(const SettingsBundle& settings) {
    return settings.mqtt.baseTopic + "/state/battery";
}

String commandTopic(const SettingsBundle& settings, const char* command) {
    return settings.mqtt.baseTopic + "/cmd/" + command;
}

String playerCommandTopic(const SettingsBundle& settings) {
    return settings.mqtt.baseTopic + "/cmd/player";
}

String playerPlayMediaTopic(const SettingsBundle& settings) {
    return settings.mqtt.baseTopic + "/cmd/play_media";
}

String playerVolumeStateTopic(const SettingsBundle& settings) {
    return settings.mqtt.baseTopic + "/state/volume";
}

String entityUniqueId(const SettingsBundle& settings, const char* suffix) {
    String id = settings.device.deviceName;
    id.replace(" ", "_");
    id.toLowerCase();
    id += "_";
    id += suffix;
    return id;
}

String discoveryTopic(const SettingsBundle& settings, const char* component, const char* objectId) {
    return baseDiscoveryPrefix() + "/" + component + "/" + settings.device.deviceName + "/" + objectId + "/config";
}

String discoveryPayloadSensor(const SettingsBundle& settings, const char* objectId, const char* name, const char* stateTopic, const char* valueTemplate, const char* unit, const char* deviceClass, const char* stateClass, const char* icon) {
    JsonDocument doc;
    doc["name"] = name;
    doc["uniq_id"] = entityUniqueId(settings, objectId);
    doc["stat_t"] = stateTopic;
    doc["avty_t"] = availabilityTopic(settings);
    doc["val_tpl"] = valueTemplate;
    if (unit != nullptr) doc["unit_of_meas"] = unit;
    if (deviceClass != nullptr) doc["dev_cla"] = deviceClass;
    if (stateClass != nullptr) doc["stat_cla"] = stateClass;
    if (icon != nullptr) doc["ic"] = icon;
    fillDevice(settings, doc["dev"].to<JsonObject>());
    String out;
    serializeJson(doc, out);
    return out;
}

String discoveryPayloadNumber(const SettingsBundle& settings, const char* objectId, const char* name, const char* stateTopic, const char* commandTopicValue, int minValue, int maxValue, int step, const char* unit, const char* icon) {
    JsonDocument doc;
    doc["name"] = name;
    doc["uniq_id"] = entityUniqueId(settings, objectId);
    doc["stat_t"] = stateTopic;
    doc["cmd_t"] = commandTopicValue;
    doc["avty_t"] = availabilityTopic(settings);
    doc["min"] = minValue;
    doc["max"] = maxValue;
    doc["step"] = step;
    doc["mode"] = "box";
    if (unit != nullptr) doc["unit_of_meas"] = unit;
    if (icon != nullptr) doc["ic"] = icon;
    fillDevice(settings, doc["dev"].to<JsonObject>());
    String out;
    serializeJson(doc, out);
    return out;
}

String discoveryPayloadButton(const SettingsBundle& settings, const char* objectId, const char* name, const char* commandTopicValue, const char* payloadPress, const char* icon) {
    JsonDocument doc;
    doc["name"] = name;
    doc["uniq_id"] = entityUniqueId(settings, objectId);
    doc["cmd_t"] = commandTopicValue;
    doc["pl_prs"] = payloadPress;
    doc["avty_t"] = availabilityTopic(settings);
    if (icon != nullptr) doc["ic"] = icon;
    fillDevice(settings, doc["dev"].to<JsonObject>());
    String out;
    serializeJson(doc, out);
    return out;
}

String discoveryPayloadText(const SettingsBundle& settings, const char* objectId, const char* name, const char* commandTopicValue, const char* icon) {
    JsonDocument doc;
    doc["name"] = name;
    doc["uniq_id"] = entityUniqueId(settings, objectId);
    doc["cmd_t"] = commandTopicValue;
    doc["mode"] = "text";
    doc["avty_t"] = availabilityTopic(settings);
    if (icon != nullptr) doc["ic"] = icon;
    fillDevice(settings, doc["dev"].to<JsonObject>());
    String out;
    serializeJson(doc, out);
    return out;
}

String discoveryPayloadMediaPlayer(const SettingsBundle& settings, const char* objectId, const char* name, const char* icon) {
    JsonDocument doc;
    doc["name"] = name;
    doc["unique_id"] = entityUniqueId(settings, objectId);
    doc["availability_topic"] = availabilityTopic(settings);
    doc["state_topic"] = playbackStateTopic(settings);
    doc["state_value_template"] = "{% set s = value_json.state | default('idle') %}{% if s == 'playing' %}playing{% elif s == 'buffering' %}playing{% else %}idle{% endif %}";
    doc["command_topic"] = playerCommandTopic(settings);
    doc["payload_play"] = "PLAY";
    doc["payload_stop"] = "STOP";
    doc["play_media_topic"] = playerPlayMediaTopic(settings);
    doc["play_media_payload_template"] = "{{ {'url': media_id, 'label': media_id, 'type': media_type} | tojson }}";
    doc["volume_command_topic"] = commandTopic(settings, "volume");
    doc["volume_state_topic"] = playerVolumeStateTopic(settings);
    doc["volume_value_template"] = "{{ (value | float(0)) / 100 }}";
    doc["json_attributes_topic"] = playbackStateTopic(settings);
    if (icon != nullptr) doc["icon"] = icon;
    fillDevice(settings, doc["device"].to<JsonObject>());
    String out;
    serializeJson(doc, out);
    return out;
}

}  // namespace HaBridge
