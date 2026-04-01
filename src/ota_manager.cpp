#include "ota_manager.h"

#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <mbedtls/sha256.h>

#include "version.h"

namespace {
String githubApiLatestUrl(const SettingsBundle& settings) {
    return String("https://api.github.com/repos/") + settings.ota.owner + "/" + settings.ota.repository + "/releases/latest";
}

String githubReleaseAssetUrl(const SettingsBundle& settings, const String& version, const String& assetName) {
    return String("https://github.com/") + settings.ota.owner + "/" + settings.ota.repository + "/releases/download/" + version + "/" + assetName;
}

String applyVersionTemplate(String templ, const String& version) {
    templ.replace("${version}", version);
    return templ;
}

}  // namespace

void OtaManager::begin(const SettingsBundle& settings, AppState& appState) {
    appState_ = &appState;
    applySettings(settings);
}

void OtaManager::applySettings(const SettingsBundle& settings) {
    settings_ = settings;
}

void OtaManager::loop() {
    if (pendingCheck_ && !busy_) {
        const bool applyAfterCheck = pendingApply_;
        pendingCheck_ = false;
        pendingApply_ = false;
        runTask(applyAfterCheck);
    }
}

bool OtaManager::triggerCheck(bool applyAfterCheck) {
    if (busy_) {
        return false;
    }
    pendingCheck_ = true;
    pendingApply_ = applyAfterCheck;
    lastMessage_ = applyAfterCheck ? "queued update" : "queued check";
    if (appState_ != nullptr) {
        appState_->setOta(updateAvailable_, latestVersion_, "queued", "");
    }
    return true;
}

void OtaManager::appendStatusJson(JsonObject root) const {
    root["busy"] = busy_;
    root["updateAvailable"] = updateAvailable_;
    root["latestVersion"] = latestVersion_;
    root["message"] = lastMessage_;
}

void OtaManager::runTask(bool applyAfterCheck) {
    busy_ = true;
    lastMessage_ = applyAfterCheck ? "checking and applying" : "checking";
    if (appState_ != nullptr) {
        appState_->setOta(updateAvailable_, latestVersion_, applyAfterCheck ? "checking/apply" : "checking", "");
    }

    const CheckResult result = checkNow();
    latestVersion_ = result.latestVersion;
    updateAvailable_ = result.updateAvailable;
    lastMessage_ = result.message;
    if (!result.success) {
        if (appState_ != nullptr) {
            appState_->setOta(updateAvailable_, latestVersion_, "error", result.message);
        }
        busy_ = false;
        return;
    }

    if (!applyAfterCheck || !result.updateAvailable) {
        if (appState_ != nullptr) {
            appState_->setOta(updateAvailable_, latestVersion_, result.updateAvailable ? "available" : "current", "");
        }
        busy_ = false;
        return;
    }

    String installMessage;
    if (!installNow(result, installMessage)) {
        lastMessage_ = installMessage;
        if (appState_ != nullptr) {
            appState_->setOta(updateAvailable_, latestVersion_, "error", installMessage);
        }
        busy_ = false;
        return;
    }

    lastMessage_ = installMessage;
    if (appState_ != nullptr) {
        appState_->setOta(false, latestVersion_, "installed", "");
    }
    delay(500);
    ESP.restart();
}

int OtaManager::compareVersions(const String& left, const String& right) const {
    auto tokenize = [](String value, int* parts, size_t count) {
        value.replace("v", "");
        value.replace("V", "");
        for (size_t i = 0; i < count; ++i) parts[i] = 0;
        size_t index = 0;
        int start = 0;
        while (index < count && start < value.length()) {
            int dot = value.indexOf('.', start);
            String token = dot >= 0 ? value.substring(start, dot) : value.substring(start);
            parts[index++] = token.toInt();
            if (dot < 0) break;
            start = dot + 1;
        }
    };
    int leftParts[4];
    int rightParts[4];
    tokenize(left, leftParts, 4);
    tokenize(right, rightParts, 4);
    for (size_t i = 0; i < 4; ++i) {
        if (leftParts[i] < rightParts[i]) return -1;
        if (leftParts[i] > rightParts[i]) return 1;
    }
    return 0;
}

String OtaManager::normalizeVersion(const String& value) const {
    if (value.startsWith("v") || value.startsWith("V")) {
        return value;
    }
    return "v" + value;
}

OtaManager::CheckResult OtaManager::checkNow() {
    CheckResult result;
    if (WiFi.status() != WL_CONNECTED) {
        result.message = "Wi-Fi not connected";
        return result;
    }

    WiFiClientSecure client;
    if (settings_.ota.allowInsecureTls) {
        client.setInsecure();
    }
    HTTPClient http;
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.setTimeout(10000);

    if (!settings_.ota.manifestUrl.isEmpty()) {
        if (!http.begin(client, settings_.ota.manifestUrl)) {
            result.message = "Failed to open manifest URL";
            return result;
        }
        const int code = http.GET();
        if (code != HTTP_CODE_OK) {
            result.message = String("Manifest HTTP ") + code;
            http.end();
            return result;
        }
        JsonDocument doc;
        if (deserializeJson(doc, http.getString()) != DeserializationError::Ok) {
            result.message = "Manifest parse failed";
            http.end();
            return result;
        }
        http.end();
        JsonVariantConst release;
        if (doc["release"].isNull()) {
            release = doc.as<JsonVariantConst>();
        } else {
            release = doc["release"].as<JsonVariantConst>();
        }
        result.latestVersion = normalizeVersion(String(static_cast<const char*>(release["version"] | "")));
        result.assetUrl = String(static_cast<const char*>(release["url"] | ""));
        result.assetName = String(static_cast<const char*>(release["asset"] | ""));
        result.checksumSha256 = String(static_cast<const char*>(release["sha256"] | ""));
        if (release["channel"].is<const char*>()) {
            const String channel = String(static_cast<const char*>(release["channel"]));
            if (!settings_.ota.channel.isEmpty() && channel != settings_.ota.channel) {
                result.message = "Manifest channel mismatch";
                return result;
            }
        }
    } else {
        if (!http.begin(client, githubApiLatestUrl(settings_))) {
            result.message = "Failed to open GitHub releases API";
            return result;
        }
        http.addHeader("Accept", "application/vnd.github+json");
        http.addHeader("User-Agent", String(APP_NAME "/" APP_VERSION));
        const int code = http.GET();
        if (code != HTTP_CODE_OK) {
            result.message = String("GitHub API HTTP ") + code;
            http.end();
            return result;
        }
        JsonDocument doc;
        if (deserializeJson(doc, http.getString()) != DeserializationError::Ok) {
            result.message = "GitHub release parse failed";
            http.end();
            return result;
        }
        http.end();
        result.latestVersion = normalizeVersion(String(static_cast<const char*>(doc["tag_name"] | "")));
        result.assetName = applyVersionTemplate(settings_.ota.assetTemplate, result.latestVersion);
        for (JsonObject asset : doc["assets"].as<JsonArray>()) {
            const String name = String(static_cast<const char*>(asset["name"] | ""));
            if (name == result.assetName) {
                result.assetUrl = String(static_cast<const char*>(asset["browser_download_url"] | ""));
                break;
            }
        }
        if (result.assetUrl.isEmpty()) {
            result.assetUrl = githubReleaseAssetUrl(settings_, result.latestVersion, result.assetName);
        }
    }

    if (result.latestVersion.isEmpty() || result.assetUrl.isEmpty()) {
        result.message = "Incomplete release metadata";
        return result;
    }
    result.updateAvailable = compareVersions(normalizeVersion(APP_VERSION), result.latestVersion) < 0;
    result.success = true;
    result.message = result.updateAvailable ? "update available" : "already current";
    return result;
}

bool OtaManager::installNow(const CheckResult& result, String& message) {
    WiFiClientSecure client;
    if (settings_.ota.allowInsecureTls) {
        client.setInsecure();
    }
    HTTPClient http;
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.setTimeout(15000);
    if (!http.begin(client, result.assetUrl)) {
        message = "Failed to open firmware URL";
        return false;
    }
    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        message = String("Firmware HTTP ") + code;
        http.end();
        return false;
    }
    const int contentLength = http.getSize();
    if (contentLength <= 0) {
        message = "Invalid content length";
        http.end();
        return false;
    }
    if (!Update.begin(contentLength)) {
        message = "Not enough space for OTA";
        http.end();
        return false;
    }

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts_ret(&sha, 0);

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buffer[1024];
    int written = 0;
    while (http.connected() && written < contentLength) {
        const size_t available = stream->available();
        if (available == 0) {
            delay(1);
            continue;
        }
        const int read = stream->readBytes(buffer, min(sizeof(buffer), available));
        if (read <= 0) {
            continue;
        }
        mbedtls_sha256_update_ret(&sha, buffer, read);
        if (Update.write(buffer, read) != static_cast<size_t>(read)) {
            message = "OTA write failed";
            Update.abort();
            http.end();
            mbedtls_sha256_free(&sha);
            return false;
        }
        written += read;
    }

    uint8_t digest[32];
    mbedtls_sha256_finish_ret(&sha, digest);
    mbedtls_sha256_free(&sha);
    String actualHash;
    for (uint8_t byte : digest) {
        char chunk[3];
        snprintf(chunk, sizeof(chunk), "%02x", byte);
        actualHash += chunk;
    }
    if (!result.checksumSha256.isEmpty() && !actualHash.equalsIgnoreCase(result.checksumSha256)) {
        message = "SHA256 mismatch";
        Update.abort();
        http.end();
        return false;
    }

    if (!Update.end()) {
        message = String("Update finalize failed: ") + Update.errorString();
        http.end();
        return false;
    }
    if (!Update.isFinished()) {
        message = "Update incomplete";
        http.end();
        return false;
    }

    http.end();
    message = "update installed";
    return true;
}
