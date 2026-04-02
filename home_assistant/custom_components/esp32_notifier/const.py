"""Constants for the ESP32 Notifier integration."""

from __future__ import annotations

DOMAIN = "esp32_notifier"

DEFAULT_NAME = "ESP32 Notifier"
DEFAULT_BASE_TOPIC = "esp32_notifier"
DEFAULT_SOURCE_LABEL = "Home Assistant"

CONF_BASE_TOPIC = "base_topic"
CONF_DEVICE_NAME = "device_name"
CONF_MEDIA_SOURCE_BASE_URL = "media_source_base_url"
CONF_SOURCES = "sources"
CONF_URL = "url"

TOPIC_AVAILABILITY = "availability"
TOPIC_PLAYBACK_STATE = "state/playback"
TOPIC_VOLUME_STATE = "state/volume"
TOPIC_COMMAND_PLAY = "cmd/play"
TOPIC_COMMAND_STOP = "cmd/stop"
TOPIC_COMMAND_VOLUME = "cmd/volume"

STATE_ONLINE = "online"
STATE_OFFLINE = "offline"