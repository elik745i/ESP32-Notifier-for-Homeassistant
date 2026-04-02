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

String discoveryPayloadSensor(const SettingsBundle& settings, const char* objectId, const char* name, const char* stateTopic, const char* valueTemplate, const char* unit, const char* deviceClass, const char* stateClass, const char* icon, int suggestedDisplayPrecision) {
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
    if (suggestedDisplayPrecision >= 0) doc["sug_dsp_prc"] = suggestedDisplayPrecision;
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

}  // namespace HaBridge
