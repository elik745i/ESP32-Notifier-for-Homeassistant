"""Media player platform for the ESP32 Notifier custom integration."""

from __future__ import annotations

from dataclasses import dataclass
import json
import logging
from typing import Any
from urllib.parse import urljoin

import voluptuous as vol

from homeassistant.components import media_source, mqtt
from homeassistant.components.media_player import BrowseMedia, MediaPlayerEntity, PLATFORM_SCHEMA
from homeassistant.components.media_player.browse_media import async_process_play_media_url
from homeassistant.components.media_player.const import (
    MediaPlayerDeviceClass,
    MediaPlayerEntityFeature,
    MediaPlayerState,
)
from homeassistant.const import CONF_NAME
from homeassistant.core import HomeAssistant, callback
from homeassistant.exceptions import HomeAssistantError, PlatformNotReady
from homeassistant.helpers import config_validation as cv
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.helpers.typing import ConfigType, DiscoveryInfoType

from .const import (
    CONF_BASE_TOPIC,
    CONF_DEVICE_NAME,
    CONF_MEDIA_SOURCE_BASE_URL,
    CONF_SOURCES,
    CONF_URL,
    DEFAULT_BASE_TOPIC,
    DEFAULT_NAME,
    DEFAULT_SOURCE_LABEL,
    DOMAIN,
    STATE_OFFLINE,
    STATE_ONLINE,
    TOPIC_AVAILABILITY,
    TOPIC_COMMAND_PLAY,
    TOPIC_COMMAND_STOP,
    TOPIC_COMMAND_VOLUME,
    TOPIC_PLAYBACK_STATE,
    TOPIC_VOLUME_STATE,
)

_LOGGER = logging.getLogger(__name__)


@dataclass(slots=True, frozen=True)
class SourcePreset:
    """A named source preset for the notifier."""

    name: str
    url: str


SOURCE_SCHEMA = vol.Schema(
    {
        vol.Required(CONF_NAME): cv.string,
        vol.Required(CONF_URL): cv.string,
    }
)

PLATFORM_SCHEMA = PLATFORM_SCHEMA.extend(
    {
        vol.Optional(CONF_NAME, default=DEFAULT_NAME): cv.string,
        vol.Optional(CONF_BASE_TOPIC, default=DEFAULT_BASE_TOPIC): cv.string,
        vol.Optional(CONF_DEVICE_NAME): cv.string,
        vol.Optional(CONF_MEDIA_SOURCE_BASE_URL): cv.string,
        vol.Optional(CONF_SOURCES, default=[]): vol.All(cv.ensure_list, [SOURCE_SCHEMA]),
    }
)


async def async_setup_platform(
    hass: HomeAssistant,
    config: ConfigType,
    async_add_entities: AddEntitiesCallback,
    discovery_info: DiscoveryInfoType | None = None,
) -> None:
    """Set up the ESP32 Notifier media player platform."""
    del discovery_info

    if not await mqtt.async_wait_for_mqtt_client(hass):
        raise PlatformNotReady("MQTT integration is not available")

    name = config[CONF_NAME]
    base_topic = str(config[CONF_BASE_TOPIC]).rstrip("/")
    device_name = config.get(CONF_DEVICE_NAME) or _slugify_identifier(name, base_topic)
    media_source_base_url = config.get(CONF_MEDIA_SOURCE_BASE_URL)
    sources = [
        SourcePreset(name=item[CONF_NAME], url=item[CONF_URL])
        for item in config[CONF_SOURCES]
    ]

    async_add_entities(
        [
            Esp32NotifierMediaPlayer(
                hass=hass,
                name=name,
                device_name=device_name,
                base_topic=base_topic,
                media_source_base_url=media_source_base_url,
                sources=sources,
            )
        ]
    )


def _slugify_identifier(name: str, base_topic: str) -> str:
    """Build a stable identifier for the notifier entity."""
    source = name or base_topic
    identifier = "".join(ch if ch.isalnum() else "_" for ch in source).strip("_").lower()
    return identifier or DEFAULT_BASE_TOPIC


def _normalize_media_type(media_type: str | None, announce: bool) -> str:
    """Map Home Assistant media types to the notifier's simpler play types."""
    value = (media_type or "").strip().lower()
    if announce or any(token in value for token in ("tts", "announce", "speech")):
        return "tts"
    return "stream"


class Esp32NotifierMediaPlayer(MediaPlayerEntity):
    """Representation of an ESP32 Notifier as a native Home Assistant media player."""

    _attr_should_poll = False
    _attr_device_class = MediaPlayerDeviceClass.SPEAKER

    def __init__(
        self,
        hass: HomeAssistant,
        name: str,
        device_name: str,
        base_topic: str,
        media_source_base_url: str | None,
        sources: list[SourcePreset],
    ) -> None:
        """Initialize the media player."""
        self.hass = hass
        self._attr_name = name
        self._attr_unique_id = f"{device_name}_media_player"
        self._device_name = device_name
        self._base_topic = base_topic
        self._media_source_base_url = media_source_base_url.rstrip("/") if media_source_base_url else None
        self._sources = sources
        self._source_map = {source.name: source.url for source in sources}
        self._attr_supported_features = (
            MediaPlayerEntityFeature.BROWSE_MEDIA
            | MediaPlayerEntityFeature.MEDIA_ANNOUNCE
            | MediaPlayerEntityFeature.PLAY_MEDIA
            | MediaPlayerEntityFeature.STOP
            | MediaPlayerEntityFeature.VOLUME_SET
        )
        if sources:
            self._attr_supported_features |= MediaPlayerEntityFeature.SELECT_SOURCE

        self._available = False
        self._playback_state = "idle"
        self._volume_level = 0.0
        self._current_title: str | None = None
        self._current_url: str | None = None
        self._current_source: str | None = None
        self._current_type: str | None = None

    @property
    def available(self) -> bool:
        """Return whether the player is available."""
        return self._available

    @property
    def state(self) -> MediaPlayerState:
        """Return the current playback state."""
        if self._playback_state == "playing":
            return MediaPlayerState.PLAYING
        if self._playback_state == "buffering":
            return MediaPlayerState.BUFFERING
        if self._playback_state == "paused":
            return MediaPlayerState.PAUSED
        return MediaPlayerState.IDLE

    @property
    def source(self) -> str | None:
        """Return the current source name."""
        return self._current_source

    @property
    def source_list(self) -> list[str] | None:
        """Return the list of configured source presets."""
        if not self._sources:
            return None
        return [source.name for source in self._sources]

    @property
    def volume_level(self) -> float | None:
        """Return the current volume level in the 0..1 range."""
        return self._volume_level

    @property
    def media_title(self) -> str | None:
        """Return the title of the currently playing media."""
        return self._current_title

    @property
    def media_content_id(self) -> str | None:
        """Return the currently playing media identifier."""
        return self._current_url

    @property
    def media_content_type(self) -> str | None:
        """Return the content type of the currently playing media."""
        return self._current_type

    @property
    def extra_state_attributes(self) -> dict[str, Any]:
        """Return additional diagnostic state attributes."""
        return {
            "base_topic": self._base_topic,
            "device_name": self._device_name,
            "playback_url": self._current_url,
            "playback_type": self._current_type,
        }

    @property
    def device_info(self) -> DeviceInfo:
        """Return device information for the entity registry."""
        return DeviceInfo(
            identifiers={(DOMAIN, self._device_name)},
            manufacturer="DIY",
            model="ESP32 Wi-Fi Audio Notifier",
            name=self.name,
        )

    async def async_added_to_hass(self) -> None:
        """Subscribe to MQTT topics when the entity is added."""
        await super().async_added_to_hass()

        self.async_on_remove(
            await mqtt.async_subscribe(
                self.hass,
                self._topic(TOPIC_AVAILABILITY),
                self._handle_availability,
                1,
            )
        )
        self.async_on_remove(
            await mqtt.async_subscribe(
                self.hass,
                self._topic(TOPIC_PLAYBACK_STATE),
                self._handle_playback,
                1,
            )
        )
        self.async_on_remove(
            await mqtt.async_subscribe(
                self.hass,
                self._topic(TOPIC_VOLUME_STATE),
                self._handle_volume,
                1,
            )
        )

    async def async_browse_media(
        self,
        media_content_type: str | None = None,
        media_content_id: str | None = None,
    ) -> BrowseMedia:
        """Route media browsing through Home Assistant's media source browser."""
        del media_content_type
        return await media_source.async_browse_media(
            self.hass,
            media_content_id,
            content_filter=lambda item: item.media_content_type.startswith("audio/"),
        )

    async def async_play_media(
        self,
        media_type: str,
        media_id: str,
        enqueue: str | None = None,
        announce: bool | None = None,
        **kwargs: Any,
    ) -> None:
        """Play media selected through Home Assistant."""
        del enqueue
        announce_flag = bool(announce)
        extra = kwargs.get("extra") if isinstance(kwargs.get("extra"), dict) else {}
        title = extra.get("title") or kwargs.get("title") or media_id
        source = extra.get("source") or DEFAULT_SOURCE_LABEL
        resolved_url = media_id
        play_type = _normalize_media_type(media_type, announce_flag)

        if media_source.is_media_source_id(media_id):
            play_item = await media_source.async_resolve_media(self.hass, media_id, self.entity_id)
            resolved_url = self._absolute_media_url(play_item.url)
            play_type = _normalize_media_type(play_item.mime_type, announce_flag)
            if not extra.get("title"):
                title = title_from_path(resolved_url)
            source = DEFAULT_SOURCE_LABEL
        elif not resolved_url.startswith(("http://", "https://")):
            raise HomeAssistantError(f"Unsupported media ID for ESP32 Notifier: {media_id}")

        await self._async_publish_play(
            url=resolved_url,
            title=title,
            source=source,
            play_type=play_type,
        )

    async def async_select_source(self, source: str) -> None:
        """Select a named preset source and start playback."""
        url = self._source_map.get(source)
        if not url:
            raise HomeAssistantError(f"Unknown ESP32 Notifier source preset: {source}")

        await self._async_publish_play(
            url=url,
            title=source,
            source=source,
            play_type="stream",
        )

    async def async_media_stop(self) -> None:
        """Stop playback."""
        await mqtt.async_publish(self.hass, self._topic(TOPIC_COMMAND_STOP), "stop", 1, False)
        self._playback_state = "idle"
        self.async_write_ha_state()

    async def async_set_volume_level(self, volume: float) -> None:
        """Set the notifier volume."""
        percent = max(0, min(100, round(volume * 100)))
        await mqtt.async_publish(
            self.hass,
            self._topic(TOPIC_COMMAND_VOLUME),
            json.dumps({"volumePercent": percent}),
            1,
            False,
        )
        self._volume_level = percent / 100.0
        self.async_write_ha_state()

    async def _async_publish_play(self, url: str, title: str, source: str, play_type: str) -> None:
        """Publish a play command to the notifier over MQTT."""
        payload = {
            "url": url,
            "label": title,
            "source": source,
            "type": play_type,
        }
        await mqtt.async_publish(
            self.hass,
            self._topic(TOPIC_COMMAND_PLAY),
            json.dumps(payload),
            1,
            False,
        )
        self._current_url = url
        self._current_title = title
        self._current_source = source
        self._current_type = play_type
        self._playback_state = "buffering"
        self.async_write_ha_state()

    def _absolute_media_url(self, url: str) -> str:
        """Convert a relative Home Assistant media URL into a notifier-reachable URL."""
        if url.startswith(("http://", "https://")):
            return url
        if self._media_source_base_url:
            return urljoin(f"{self._media_source_base_url}/", url.lstrip("/"))
        return async_process_play_media_url(self.hass, url)

    def _topic(self, suffix: str) -> str:
        """Build a topic under the notifier base topic."""
        return f"{self._base_topic}/{suffix}"

    @callback
    def _handle_availability(self, msg: mqtt.ReceiveMessage) -> None:
        """Handle availability updates from MQTT."""
        self._available = msg.payload.strip().lower() == STATE_ONLINE
        if msg.payload.strip().lower() == STATE_OFFLINE:
            self._playback_state = "idle"
        self.async_write_ha_state()

    @callback
    def _handle_playback(self, msg: mqtt.ReceiveMessage) -> None:
        """Handle playback state updates from MQTT."""
        try:
            payload = json.loads(msg.payload)
        except json.JSONDecodeError:
            _LOGGER.warning("Ignoring invalid playback payload from %s: %s", msg.topic, msg.payload)
            return

        self._playback_state = str(payload.get("state", self._playback_state))
        self._current_title = payload.get("title") or self._current_title
        self._current_url = payload.get("url") or self._current_url
        self._current_source = payload.get("source") or self._current_source
        self._current_type = payload.get("type") or self._current_type
        if "volumePercent" in payload:
            try:
                self._volume_level = max(0.0, min(1.0, float(payload["volumePercent"]) / 100.0))
            except (TypeError, ValueError):
                _LOGGER.debug("Invalid volumePercent in playback payload: %s", payload.get("volumePercent"))
        self.async_write_ha_state()

    @callback
    def _handle_volume(self, msg: mqtt.ReceiveMessage) -> None:
        """Handle the retained numeric volume topic."""
        try:
            self._volume_level = max(0.0, min(1.0, float(msg.payload) / 100.0))
        except (TypeError, ValueError):
            _LOGGER.warning("Ignoring invalid volume payload from %s: %s", msg.topic, msg.payload)
            return
        self.async_write_ha_state()


def title_from_path(path: str) -> str:
    """Build a readable title from a resolved media URL or path."""
    trimmed = path.rstrip("/")
    if not trimmed:
        return DEFAULT_SOURCE_LABEL
    return trimmed.rsplit("/", 1)[-1]