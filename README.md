# ESP32 Notifier for Home Assistant

PlatformIO firmware for an ESP32-based Wi-Fi audio notifier / speaker with:

- ESP32 Arduino framework
- local web UI with separate HTML, CSS, and JavaScript assets
- Wi-Fi station mode plus fallback AP configuration mode
- MQTT command and state bridge for Home Assistant
- I2S audio output for MP3 streams, radio streams, and URL-based TTS playback
- OTA checks and installs from a GitHub release or manifest URL
- battery voltage monitoring with smoothing and calibration
- OLED status display support for SSD1306 and SH1106 panels
- compile-time defaults plus saved settings in Preferences / NVS

This project is not ESPHome. It is a custom modular PlatformIO firmware baseline intended to be realistic, buildable, and extendable.

## Project Overview

This notifier project is based on the third hardware iteration of a Home Assistant speaker / notifier build.

The main design goal of that iteration was to simplify the previous versions and make the system more practical for day-to-day use:

- low-voltage operation instead of bulkier higher-voltage amplifier setups
- reduced heat inside a printed enclosure
- less interference and easier wiring
- battery-capable operation
- the ability to drive an existing passive ceiling speaker when needed
- a compact all-in-one Home Assistant audio endpoint without adding a separate Bluetooth speaker

The intended usage is broader than simple beeps. The device is meant to handle:

- Home Assistant speech and notification playback
- soft background music
- internet radio streams
- compact room audio for built-in speakers or powered speakers

The firmware in this repository documents and supports that direction, while keeping the implementation focused on maintainable ESP32 firmware modules.

## Current Status

The repository builds successfully with PlatformIO and emits [firmware.bin](.pio/build/esp32_notifier/firmware.bin) locally.

Current firmware version in this repository:

- `v0.1.6`

Recent firmware and web UI updates included in this version:

- audio I2S output is enabled again in the main build, with larger OTA slots and tuned stream buffering for more reliable playback
- the Audio tab now includes Radio Browser country and station pickers, remembers the browser selection, and uses a single Play or Stop button
- the dashboard now shows a battery icon with estimated percentage and a Wi-Fi signal indicator with RSSI quality
- low-battery deep sleep can now be configured from the Device tab, including threshold and wake interval persistence
- Wi-Fi station connect now prefers the strongest matching BSSID when several mesh nodes share the same SSID
- volume changes now update playback immediately while deferring NVS persistence to avoid interrupting active streams
- the dedicated `esp32_notifier_hacs` build now publishes a discovery and control contract compatible with the `bkbilly/mqtt_media_player` integration while keeping the web UI enabled

The Home Assistant integration supports the direct MQTT URL-command path and includes Home Assistant example configuration for wrapping the notifier as a Home Assistant player entity:

- `play URL`
- `play TTS URL`
- `stop`
- `set volume`

The firmware publishes MQTT state for:

- availability
- playback
- battery voltage
- Wi-Fi / network status

Current limitation:

- Home Assistant core MQTT discovery still does not create a native `media_player` entity for the default firmware on its own
- use the included Home Assistant package example to create a wrapper player entity inside Home Assistant while still using the notifier's built-in MQTT topics
- if the firmware is built with the diagnostic audio-disable flag enabled, playback actions will not start audio until that build flag is removed

## Dev Board

This project is intended around the Wemos / LOLIN32 Lite style ESP32 board.

The original hardware write-up also references the LOLIN32 Mini class board family because those boards integrate useful battery features such as:

- Li-Po connector
- charger circuitry
- battery protection

That makes them a practical fit for a compact battery-backed notifier. One important note from the hardware overview is that battery-voltage measurement still requires an external divider on a free ADC-capable GPIO.

Reference links and local documentation:

- Dev board page: https://www.espboards.dev/esp32/lolin32-lite/
- Board documentation PDF: [Docs/Wemos-ESP32-Lolin32-Board-BOOK-ENGLISH.pdf](Docs/Wemos-ESP32-Lolin32-Board-BOOK-ENGLISH.pdf)
- Case / enclosure files: [3D](3D)
- Demo / reference video: https://www.youtube.com/watch?v=mt_Qr-lGUJ4

Pinout image used for this board:

![LOLIN32 Lite pinout](Docs/lolin32_lite_pinout.png)

## Hardware Wiring

Known required pins:

These values are the current firmware defaults and can be changed later from the web UI if your hardware wiring differs.

| Function | GPIO |
|---|---:|
| Status LED | 22 |
| Battery ADC | 36 |
| I2S DOUT | 25 |
| I2S LRCLK / WS | 26 |
| I2S BCLK | 27 |
| OLED SDA | 23 |
| OLED SCL | 19 |

OLED assumptions:

- 0.96 inch I2C OLED
- SSD1306 128x64 or SH1106 128x64
- default I2C address `0x3C`

## Hardware Modules

The project overview describes a compact module stack built around readily available boards:

- ESP32 LOLIN32 Lite / Mini style controller board
- UDA1334 I2S stereo DAC / audio driver
- PAM8403 class-D amplifier module
- Li-Po battery pack
- passive speaker, including in-ceiling speaker use cases

Why this combination was chosen:

- the ESP32 board provides Wi-Fi, battery charging convenience, and low-power control logic
- the UDA1334 cleanly handles I2S audio output without forcing a specific integrated amplifier choice
- the PAM8403 is small, cheap, and good enough for notifier duty even if it is not an audiophile amplifier
- the battery-backed design makes the notifier easier to place without depending on a permanently available external supply

## Parts and Resources

The project overview explicitly calls out these practical resources:

- Main board family: LOLIN32 Lite / Mini style ESP32 board
- Audio DAC: UDA1334 I2S stereo module
- Amplifier: PAM8403 class-D amplifier board
- Power: Li-Po battery pack, approximately `1800 mAh` in the example build
- Extra passive components: bulk capacitor and wiring as needed

Related external references mentioned in the overview:

- Home Assistant community schematic / discussion: https://community.home-assistant.io/t/i2s-stereo-to-play-mp3-tts-from-flash-on-boot/594740/6?u=elik745i
- Older notifier discussion: https://community.home-assistant.io/t/turn-an-esp8266-wemosd1mini-into-an-audio-notifier-for-home-assistant-play-mp3-tts-rttl/211499/224?u=elik745i
- External 3D model reference mentioned in the overview: https://www.thingiverse.com/thing:6910612

## Project Layout

- [platformio.ini](platformio.ini)
- [Docs/lolin32_lite_pinout.png](Docs/lolin32_lite_pinout.png)
- [Docs/Wemos-ESP32-Lolin32-Board-BOOK-ENGLISH.pdf](Docs/Wemos-ESP32-Lolin32-Board-BOOK-ENGLISH.pdf)
- [3D](3D)
- [include/default_config.h](include/default_config.h)
- [include/settings_schema.h](include/settings_schema.h)
- [include/version.h](include/version.h)
- [src/main.cpp](src/main.cpp)
- [src/settings_manager.cpp](src/settings_manager.cpp)
- [src/wifi_manager.cpp](src/wifi_manager.cpp)
- [src/mqtt_manager.cpp](src/mqtt_manager.cpp)
- [src/audio_player.cpp](src/audio_player.cpp)
- [src/ota_manager.cpp](src/ota_manager.cpp)
- [src/web_server.cpp](src/web_server.cpp)
- [src/battery_monitor.cpp](src/battery_monitor.cpp)
- [src/display_manager.cpp](src/display_manager.cpp)
- [src/ha_bridge.cpp](src/ha_bridge.cpp)
- [web/index.html](web/index.html)
- [web/style.css](web/style.css)
- [web/app.js](web/app.js)
- [home_assistant/example_package.yaml](home_assistant/example_package.yaml)
- [home_assistant/example_scripts.yaml](home_assistant/example_scripts.yaml)
- [home_assistant/example_automations.yaml](home_assistant/example_automations.yaml)

## Library Choices

- Async web server: `ESPAsyncWebServer` with `AsyncTCP`
- MQTT: `AsyncMqttClient`
- JSON: `ArduinoJson`
- Audio playback: `schreibfaul1/ESP32-audioI2S` pinned to the last tag compatible with this ESP32 toolchain and framework line
- OLED: `Adafruit SSD1306`, `Adafruit SH110X`, `Adafruit GFX`
- Storage: `Preferences`

The audio library was intentionally pinned to an older compatible tag because newer tags require C++ and ESP-IDF features not available in the default PlatformIO ESP32 Arduino toolchain used here.

## Build Instructions

1. Open this folder in VS Code.
2. Install PlatformIO IDE if needed.
3. Build:

```powershell
pio run
```

  For the MQTT Media Player compatible firmware profile with the web UI still enabled:

```powershell
pio run -e esp32_notifier_hacs
```

4. Upload:

```powershell
pio run -t upload
```

5. Open serial monitor:

```powershell
pio device monitor -b 115200
```

## VS Code Workflow

The repository now includes VS Code workspace files under [.vscode](.vscode) so you can work from the VS Code UI without manually typing the PlatformIO commands each time.

Recommended extension:

- `PlatformIO IDE`

Available task labels in VS Code:

- `PlatformIO: Verify`
- `PlatformIO: Upload (Auto Port)`
- `PlatformIO: Monitor (Auto Port)`
- `PlatformIO: List Serial Devices`

How to use them in VS Code:

1. Open `Terminal -> Run Task`.
2. Choose `PlatformIO: Verify` to build.
3. Choose `PlatformIO: Upload (Auto Port)` to flash the board.
4. Choose `PlatformIO: Monitor (Auto Port)` to open the serial monitor.

Serial-port behavior:

- the project intentionally does not hardcode `upload_port`
- PlatformIO is allowed to auto-discover the serial adapter
- at the time of setup, PlatformIO detected a `USB-SERIAL CH340` device on `COM4`

If Windows later renumbers the port, the tasks still keep working as long as PlatformIO can see a single matching ESP32 serial adapter.

## First Flash and Provisioning

On boot the firmware does this:

1. Loads saved settings from Preferences if present.
2. Otherwise applies hardwired defaults from [include/default_config.h](include/default_config.h).
3. Attempts Wi-Fi STA mode if an SSID is configured.
4. If STA credentials are missing or the connection does not come up in time, starts fallback AP mode.

Fallback AP behavior:

- AP SSID: `ESP32-Notifier-XXXXXX`
- AP password: `configureme`
- Config URL: `http://192.168.4.1`

The web UI is served in both AP mode and normal LAN mode.

## Web UI

The frontend is stored in separate files under [web](web) and embedded into firmware at build time by [scripts/asset_embed.py](scripts/asset_embed.py).

The page allows you to:

- inspect Wi-Fi, IP, MQTT, firmware, battery, playback, and heap status
- test playback with a URL
- stop playback
- adjust volume
- edit Wi-Fi, MQTT, OTA, battery, device, OLED, and web auth settings
- reboot
- factory reset saved settings
- trigger an OTA check or install

The visual structure and Wi-Fi provisioning flow intentionally follow the same practical template style used in your pressure transducer project.

## Enclosure and Assembly

The project overview describes a compact printed enclosure workflow:

- reuse existing 3D models where practical
- create custom CAD when no suitable enclosure exists
- confirm dimensions from PCB photos, caliper measurements, and known reference spacing
- secure the finished modules inside the enclosure with adhesive mounting rather than complicated brackets

Repository-local enclosure resources are available in [3D](3D).

The hardware story in the overview is useful context here: the case design was driven by the desire to keep the notifier compact, battery-friendly, and resistant to the heat problems caused by earlier amplifier choices.

## Hardwired Defaults and Saved Settings

Compile-time defaults live in [include/default_config.h](include/default_config.h).

Saved settings live in ESP32 Preferences / NVS and override compile-time defaults.

Precedence rules:

1. Saved settings from Preferences are loaded first if the settings marker exists.
2. If no saved settings are present, defaults from [include/default_config.h](include/default_config.h) are used.
3. Saving through the web UI writes persistent values that override defaults on future boots.

Persisted values include:

- Wi-Fi SSID and password
- MQTT host, port, username, password, client ID, base topic
- device and friendly name
- OTA repository, channel, asset template, manifest URL
- battery calibration multiplier, ADC update interval, moving average window size
- saved volume
- OLED settings
- optional web auth settings

## MQTT Topics

Default base topic:

- `esp32_notifier`

Command topics:

- `esp32_notifier/cmd/play`
- `esp32_notifier/cmd/tts`
- `esp32_notifier/cmd/stop`
- `esp32_notifier/cmd/volume`

State topics:

- `esp32_notifier/availability`
- `esp32_notifier/state/playback`
- `esp32_notifier/state/network`
- `esp32_notifier/state/battery`
- `esp32_notifier/state/volume`

Example payloads:

Play URL:

```json
{"url":"https://example.com/stream.mp3","label":"Test Stream","type":"stream"}
```

Play TTS URL:

```json
{"url":"https://example.local/tts/doorbell.mp3","label":"Doorbell","type":"tts"}
```

Volume:

```json
{"volumePercent":55}
```

## Home Assistant Setup

Example HA files are included in [home_assistant](home_assistant).

Current integration paths:

1. Use MQTT discovery to add the notifier's sensors, number, button, and text entities.
2. Use the included package to create a Home Assistant `universal` wrapper `media_player` that targets the notifier's MQTT bridge.
3. The wrapper supports `play_media`, `media_stop`, `volume_set`, and `select_source`, and forwards Home Assistant-style media payloads into the notifier's existing MQTT command topics.
4. If your TTS engine can expose a direct MP3 or stream URL, publish that URL to `cmd/tts` or route it through the wrapper player's `play_media` call.
5. If you want the notifier to appear in Home Assistant's Media dashboard target picker with native browse/play handling, use the custom integration in [home_assistant/custom_components/esp32_notifier](home_assistant/custom_components/esp32_notifier).
6. If you want to test a third-party HACS MQTT media-player integration, build the dedicated `esp32_notifier_hacs` environment, which keeps the local web UI and publishes an experimental compatibility layer for those integrations.

Which integration to use:

- Use the included package wrapper when you want the fastest setup and only need URL playback, TTS, volume, and preset sources.
- Use the native custom integration when you want the notifier in Home Assistant's Media dashboard with browse/play support handled by Home Assistant.
- Use `bkbilly/mqtt_media_player` from HACS with the `esp32_notifier_hacs` firmware build when you want a broker-discovered MQTT media player entity and still need the notifier web UI for device setup.

Step-by-step Home Assistant setup:

1. Install and configure the Home Assistant MQTT integration so it connects to the same broker as the notifier.
2. In `configuration.yaml`, enable packages if you do not already use them:

```yaml
homeassistant:
  packages: !include_dir_named packages
```

3. Create a package file such as `packages/esp32_notifier.yaml` in your Home Assistant config directory.
4. Copy the contents of [home_assistant/example_package.yaml](home_assistant/example_package.yaml) into that package file.
5. If your notifier uses a custom MQTT base topic, replace the default `esp32_notifier/...` topics in the package with your configured base topic.
6. Edit the preset helper values in the package or in Home Assistant after the first restart:
  `input_text.esp32_notifier_source_1_name` / `_url` through `source_4_name` / `_url` define the source picker entries.
  `input_text.esp32_notifier_play_url` is used by the `Custom URL` source.
7. Restart Home Assistant so the package is loaded.
8. Verify the wrapper entities appear, especially the online binary sensor, playback state sensor, current source sensor, volume number, and the `ESP32 Notifier` media player.
9. Open the `ESP32 Notifier` media player card and verify the source picker appears with your preset names plus `Custom URL`.
10. Test playback first with a preset source or a direct MP3 URL through the `media_player.play_media` action before testing TTS.
11. Test volume with the `media_player.volume_set` action using Home Assistant's `0..1` scale.
12. For TTS, prefer a flow that produces a directly reachable audio URL and send it through the wrapper player or the included speak helper script.

Native Media Dashboard option:

1. Copy [home_assistant/custom_components/esp32_notifier](home_assistant/custom_components/esp32_notifier) into your Home Assistant config directory under `custom_components/esp32_notifier`.
2. Add a `media_player:` platform entry using [home_assistant/example_custom_integration.yaml](home_assistant/example_custom_integration.yaml) as the starting point.
3. Set `base_topic` to the notifier MQTT base topic and `device_name` to a stable identifier for the entity registry.
4. Set `media_source_base_url` to a Home Assistant URL that the ESP32 can reach on your network. This is important when Home Assistant resolves `media-source://...` items into relative URLs.
5. Restart Home Assistant.
6. If you were previously using the wrapper `media_player` from [home_assistant/example_package.yaml](home_assistant/example_package.yaml), remove or comment out its `media_player:` block to avoid duplicate `ESP32 Notifier` players.
7. After restart, the custom integration entity should be usable from the Media dashboard target picker and should resolve Home Assistant media-browser selections into direct URLs before sending them to the notifier over MQTT.

Pure MQTT Media Player option:

1. Install the `bkbilly/mqtt_media_player` integration from HACS.
2. Build and flash the `esp32_notifier_hacs` environment instead of the default firmware profile.
3. Keep Home Assistant's core MQTT integration enabled so the notifier can publish its discovery payload and retained state topics.
4. Remove any stale retained discovery topic left from older test builds, especially `homeassistant/media_player/<device_name>/hacs_player/config`, if it still exists on the broker.
5. Restart Home Assistant or reload both the MQTT integration and the `MQTT Media Player` custom integration.
6. The integration should discover the notifier from `homeassistant/media_player/<device_name>/config` and subscribe to `<base_topic>/hacs/...` plus `<base_topic>/hacs/cmd/...`.
7. Use `esp32_notifier_hacs_slim` only if you explicitly want a no-web fallback build.

Important package syntax note:

- Current Home Assistant versions expect MQTT YAML entities under a top-level `mqtt:` key in packages.
- Older `platform: mqtt` under `sensor:`, `binary_sensor:`, or `number:` will trigger Home Assistant repair warnings and should not be used.

Current Home Assistant wrapper scope:

- Home Assistant can expose the notifier as a `media_player` by wrapping the MQTT entities and scripts with a `universal` media player.
- The included wrapper now accepts Home Assistant-style `play_media` fields such as `media_content_id`, `media_content_type`, optional `extra.title`, and `announce`.
- The wrapper also exposes a real `source_list` and `select_source` flow backed by editable preset name/URL pairs in the package.
- Playback state, title, URL, and volume are still published back from the notifier over MQTT.

Important current limitation:

- Because Home Assistant MQTT discovery does not natively create a `media_player` for this device, the wrapper package is still required for the notifier to appear in the media-player list.
- The notifier is most reliable today with direct URL-based playback, whether that URL comes from a script, HA automation, or the HA wrapper `play_media` action.
- Source selection in Home Assistant is implemented as named URL presets plus a `Custom URL` fallback, not as a browsable streaming catalog.
- The Home Assistant Media dashboard target picker and full built-in media-browser flow are not fully provided by this YAML wrapper alone; matching that exact behavior requires a native/custom Home Assistant media player integration that resolves `media-source://...` items into playable URLs before forwarding them to the notifier.
- For TTS, the best path is still a directly reachable audio URL. If Home Assistant passes `announce: true`, the wrapper/firmware path now marks the playback as `tts`.
- If the firmware is built with `APP_DISABLE_AUDIO=1`, the HA-side wrapper player will exist but playback actions will not produce sound.

Custom integration scope:

- The custom integration provides a native `media_player` entity that subscribes directly to the notifier's MQTT topics.
- It implements Home Assistant `browse_media` and `play_media` natively, including resolving `media-source://...` selections into direct URLs before publishing them to the notifier.
- This is the path intended for the Home Assistant Media dashboard target picker.
- Named preset streams can also be configured directly in YAML under `sources:`.

Experimental HACS MQTT media-player compatibility:

- Build `esp32_notifier_hacs` when you want the MQTT Media Player compatible firmware profile with the web UI still enabled.
- That profile additionally publishes flat retained topics under `<base_topic>/hacs/` for `state`, `title`, `mediatype`, and `volume`, with volume normalized to `0..1` for MQTT Media Player compatibility.
- It publishes its discovery payload at `homeassistant/media_player/<device_name>/config`, which matches the `bkbilly/mqtt_media_player` integration's expected discovery path.
- The profile exposes dedicated compatibility command topics under `<base_topic>/hacs/cmd/` for `play`, `pause`, `playpause`, `stop`, `volume`, and `playmedia`.
- Existing notifier JSON topics remain unchanged, so the current package and custom integration continue to work.
- `next` and `previous` are accepted as compatibility no-ops because the notifier does not implement queue-based transport controls.
- Use `esp32_notifier_hacs_slim` only if you explicitly want a no-web fallback build.

Practical TTS options right now:

1. Use a TTS engine or workflow that can produce a directly reachable MP3/HTTP URL.
2. Publish that URL to `esp32_notifier/cmd/tts` or pass it via the Home Assistant wrapper player's `play_media` path.
3. Optionally use the included `script.esp32_notifier_speak_url` helper as the HA wrapper.

Media and radio playback are straightforward today because they are already URL-based.

## Sound Quality Notes

The original project overview includes two useful real-world expectations:

- with a basic passive ceiling speaker, the goal is practical speech and light background audio rather than hi-fi playback
- when connected to better powered speakers, the UDA1334-based I2S path can sound noticeably better than expected for such a small and inexpensive module stack

That matches the intended role of this firmware: reliable notifier and room-audio endpoint first, rather than a full-featured audiophile streamer.

## OTA From GitHub Releases

The firmware supports two OTA metadata strategies:

1. Preferred: a lightweight JSON manifest URL
2. Fallback: GitHub Releases API lookup

Recommended manifest shape:

```json
{
  "version": "v0.1.6",
  "url": "https://github.com/elik745i/ESP32-Notifier-for-Homeassistant/releases/download/v0.1.6/esp32-notifier-v0.1.6.bin",
  "asset": "esp32-notifier-v0.1.6.bin",
  "sha256": "<optional sha256>",
  "channel": "stable"
}
```

Release asset naming strategy used by default:

- `esp32-notifier-${version}.bin`

OTA notes:

- If a manifest provides `sha256`, the firmware verifies it while streaming the update.
- If no manifest is provided, the firmware falls back to GitHub release metadata and asset naming.
- OTA install is currently triggered manually from the web UI or API.

## Battery Monitoring

Battery monitoring is handled by [src/battery_monitor.cpp](src/battery_monitor.cpp).

Battery input and defaults:

- GPIO pin: `GPIO36`
- ADC source: ESP32 internal ADC
- default calibration multiplier: `3.866`
- default smoothing: moving average
- default moving average window: `10` samples

Calculation path:

1. read raw ADC from `GPIO36`
2. convert the raw ADC reading to ADC pin voltage using `3.3 V * raw / 4095`
3. multiply that voltage by the configured battery correction multiplier
4. apply a moving average over the configured window size
5. publish and display the filtered battery voltage

Configurable battery settings:

- calibration multiplier
- ADC update interval
- moving average window size

The default behavior intentionally matches the previous ESPHome setup as closely as possible:

`raw ADC -> multiply by 3.866 -> moving average over 10 samples -> publish final voltage`

### Recalibration

If the reported battery voltage is off, recalibrate it with a multimeter:

1. Measure the actual battery voltage directly with a multimeter.
2. Compare that value to the voltage reported in the web UI or MQTT state.
3. Compute a corrected multiplier:

  `new_multiplier = old_multiplier * (actual_voltage / reported_voltage)`

4. Save the new `calibration multiplier` in the web UI.
5. Let the moving average settle for about 10 samples before judging the result.

## OLED Behavior

OLED support is handled by [src/display_manager.cpp](src/display_manager.cpp).

Displayed layout:

- top row: IP address or AP SSID
- center: current media title, TTS preview, OTA state, or idle / setup text
- bottom row: Wi-Fi / MQTT / playback summary

The display refreshes on an interval and supports simple scrolling for longer center text.

## Factory Reset

Factory reset from the web UI clears Preferences and reboots.

After reset:

- saved settings are removed
- hardwired defaults become active again
- AP fallback will start if Wi-Fi is not configured by defaults

## Troubleshooting

- If Wi-Fi never connects, join the fallback AP and open `http://192.168.4.1`.
- If MQTT state never appears, check base topic, credentials, and broker reachability.
- If audio playback fails, first confirm the build is not using `APP_DISABLE_AUDIO=1`, then test with a known good MP3 URL before debugging Home Assistant.
- If HTTPS stream playback fails, check certificate compatibility and the remote server response.
- If OTA checks fail, prefer a manifest URL first and verify asset naming.
- If battery voltage is wrong, confirm your divider ratio and calibration multiplier.

## Known Limitations

- Home Assistant MQTT discovery still does not auto-create a native `media_player` entity for this firmware, so a Home Assistant-side wrapper entity is required if you want this notifier to appear in the media-player list.
- The notifier is still most robust when driven by direct URL playback over MQTT.
- Audio playback support is centered on the selected audio library's stream capabilities; some edge-case codecs and playlists may still need tuning.
- Firmware size is now very close to the OTA slot limit, so future feature additions will likely require code trimming or a different partition strategy.
- OTA installs are functional but still conservative: the preferred secure path is a manifest with SHA256.
- Web auth is basic HTTP auth, not a complete role-based access model.
- The current firmware focuses on output-only audio. No microphone or duplex audio path is implemented.
- The original Russian overview also mentions two-way voice communication as a broader idea, but that is not implemented in this firmware baseline.

## GitHub Push Readiness

The repository already includes:

- [platformio.ini](platformio.ini)
- [.gitignore](.gitignore)
- version constants in [include/version.h](include/version.h)
- CI workflow in [.github/workflows/platformio.yml](.github/workflows/platformio.yml)
- Home Assistant examples under [home_assistant](home_assistant)

Suggested push flow:

```powershell
git init
git checkout -b main
git add .
git commit -m "Initial ESP32 notifier firmware"
git remote add origin https://github.com/elik745i/ESP32-Notifier-for-Homeassistant.git
git push -u origin main
```

