#pragma once

#include <Arduino.h>

#include "settings_schema.h"

struct PlaybackCommand {
    String action;
    String url;
    String label;
    String source;
    String mediaType;
    uint8_t volumePercent = 0;
};

namespace HaBridge {

String availabilityTopic(const SettingsBundle& settings);
String playbackStateTopic(const SettingsBundle& settings);
String networkStateTopic(const SettingsBundle& settings);
String batteryStateTopic(const SettingsBundle& settings);
String commandTopic(const SettingsBundle& settings, const char* command);
String entityUniqueId(const SettingsBundle& settings, const char* suffix);
String discoveryTopic(const SettingsBundle& settings, const char* component, const char* objectId);
String discoveryPayloadSensor(const SettingsBundle& settings, const char* objectId, const char* name, const char* stateTopic, const char* valueTemplate, const char* unit, const char* deviceClass, const char* stateClass, const char* icon = nullptr, int suggestedDisplayPrecision = -1);
String discoveryPayloadNumber(const SettingsBundle& settings, const char* objectId, const char* name, const char* stateTopic, const char* commandTopic, int minValue, int maxValue, int step, const char* unit, const char* icon = nullptr);
String discoveryPayloadButton(const SettingsBundle& settings, const char* objectId, const char* name, const char* commandTopic, const char* payloadPress, const char* icon = nullptr);
String discoveryPayloadText(const SettingsBundle& settings, const char* objectId, const char* name, const char* commandTopic, const char* icon = nullptr);

}  // namespace HaBridge
