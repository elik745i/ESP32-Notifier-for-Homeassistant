#pragma once

#include <stdint.h>

namespace DefaultConfig {

constexpr char WIFI_SSID[] = "";
constexpr char WIFI_PASSWORD[] = "";
constexpr bool WIFI_AP_FALLBACK_ENABLED = true;
constexpr char WIFI_AP_SSID_PREFIX[] = "ESP32-Notifier";
constexpr char WIFI_AP_PASSWORD[] = "configureme";

constexpr char DEVICE_NAME[] = "esp32-notifier";
constexpr char FRIENDLY_NAME[] = "ESP32 Notifier";

constexpr char MQTT_HOST[] = "";
constexpr uint16_t MQTT_PORT = 1883;
constexpr char MQTT_USERNAME[] = "";
constexpr char MQTT_PASSWORD[] = "";
constexpr char MQTT_BASE_TOPIC[] = "esp32_notifier";
constexpr bool MQTT_DISCOVERY_ENABLED = true;

constexpr char OTA_OWNER[] = "elik745i";
constexpr char OTA_REPOSITORY[] = "ESP32-Notifier-for-Homeassistant";
constexpr char OTA_CHANNEL[] = "stable";
constexpr char OTA_ASSET_TEMPLATE[] = "esp32-notifier-${version}.bin";
constexpr char OTA_MANIFEST_URL[] = "";
constexpr bool OTA_ALLOW_INSECURE_TLS = true;

constexpr float BATTERY_DIVIDER_RATIO = 2.0f;
constexpr float BATTERY_CALIBRATION = 1.0f;
constexpr float BATTERY_ALPHA = 0.20f;
constexpr float BATTERY_MIN_VOLTAGE = 2.80f;
constexpr float BATTERY_MAX_VOLTAGE = 4.35f;
constexpr uint32_t BATTERY_UPDATE_INTERVAL_MS = 10000;
constexpr uint16_t BATTERY_SAMPLE_COUNT = 8;

constexpr uint8_t DEFAULT_VOLUME_PERCENT = 60;

constexpr bool WEB_AUTH_ENABLED = false;
constexpr char WEB_USERNAME[] = "admin";
constexpr char WEB_PASSWORD[] = "admin";

constexpr bool OLED_ENABLED = true;
constexpr uint8_t OLED_I2C_ADDRESS = 0x3C;
constexpr char OLED_DRIVER[] = "ssd1306";
constexpr uint8_t OLED_WIDTH = 128;
constexpr uint8_t OLED_HEIGHT = 64;
constexpr uint8_t OLED_SDA_PIN = 23;
constexpr uint8_t OLED_SCL_PIN = 19;
constexpr int8_t OLED_RESET_PIN = -1;
constexpr uint16_t OLED_DIM_TIMEOUT_SECONDS = 0;

constexpr uint8_t STATUS_LED_PIN = 22;
constexpr uint8_t BATTERY_ADC_PIN = 36;
constexpr uint8_t I2S_DOUT_PIN = 25;
constexpr uint8_t I2S_WS_PIN = 26;
constexpr uint8_t I2S_BCLK_PIN = 27;

}  // namespace DefaultConfig
