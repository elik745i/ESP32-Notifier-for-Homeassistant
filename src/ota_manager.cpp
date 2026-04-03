#include "ota_manager.h"

#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <mbedtls/sha256.h>

#include "version.h"

namespace {
constexpr unsigned long RELEASE_CACHE_TTL_MS = 5UL * 60UL * 1000UL;
constexpr uint8_t RELEASE_REFRESH_MAX_ATTEMPTS = 4;
constexpr unsigned long RELEASE_REFRESH_RETRY_DELAY_MS = 1500UL;

String githubApiLatestUrl(const SettingsBundle& settings) {
    return String("https://api.github.com/repos/") + settings.ota.owner + "/" + settings.ota.repository + "/releases/latest";
}

String githubApiReleasesUrl(const SettingsBundle& settings) {
    return String("https://api.github.com/repos/") + settings.ota.owner + "/" + settings.ota.repository + "/releases?per_page=10";
}

String githubReleaseAssetUrl(const SettingsBundle& settings, const String& version, const String& assetName) {
    return String("https://github.com/") + settings.ota.owner + "/" + settings.ota.repository + "/releases/download/" + version + "/" + assetName;
}

String applyVersionTemplate(String templ, const String& version) {
    templ.replace("${version}", version);
    return templ;
}

String normalizedVersionTag(String value) {
    if (value.isEmpty()) {
        return "";
    }
    if (value.startsWith("v") || value.startsWith("V")) {
        return value;
    }
    return "v" + value;
}

String currentReleaseAssetName(const SettingsBundle& settings) {
    const String version = normalizedVersionTag(APP_VERSION);
#ifdef APP_ENABLE_HACS_MQTT
  #ifdef APP_DISABLE_WEB_UI
    return applyVersionTemplate("esp32-notifier-hacs-slim-${version}.bin", version);
  #else
    return applyVersionTemplate("esp32-notifier-hacs-${version}.bin", version);
  #endif
#else
    return applyVersionTemplate(settings.ota.assetTemplate, version);
#endif
}

String variantLabelForAssetName(const String& assetName) {
    String lowered = assetName;
    lowered.toLowerCase();
    if (lowered.indexOf("hacs-slim") >= 0) {
        return "HACS Slim";
    }
    if (lowered.indexOf("hacs") >= 0) {
        return "HACS";
    }
    return "Standard";
}

void configureTlsClient(WiFiClientSecure& client, bool allowInsecureTls) {
    if (allowInsecureTls) {
        client.setInsecure();
    }
}

String httpErrorWithDetail(HTTPClient& http, const char* prefix, int code) {
    String message = prefix;
    message += code;
    if (code < 0) {
        message += " (";
        message += http.errorToString(code);
        message += ")";
    }
    return message;
}

template <typename ConfigureHeaders>
int beginAndGet(HTTPClient& http, WiFiClientSecure& client, const String& url, bool allowInsecureTls, uint16_t timeoutMs, ConfigureHeaders configureHeaders) {
    auto beginRequest = [&](bool insecure) {
        http.end();
        client.stop();
        client = WiFiClientSecure();
        if (insecure) {
            client.setInsecure();
        } else {
            configureTlsClient(client, allowInsecureTls);
        }
        http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
        http.setTimeout(timeoutMs);
        if (!http.begin(client, url)) {
            return false;
        }
        configureHeaders(http);
        return true;
    };

    if (!beginRequest(allowInsecureTls)) {
        yield();
        if (!beginRequest(allowInsecureTls)) {
            if (allowInsecureTls || !beginRequest(true)) {
                return HTTPC_ERROR_CONNECTION_REFUSED;
            }
        }
    }

    int code = http.GET();
    if (code < 0 && !allowInsecureTls) {
        if (!beginRequest(true)) {
            return code;
        }
        code = http.GET();
    }
    return code;
}

}  // namespace

bool OtaManager::hasBinExtension(const String& filename) const {
    String lowercase = filename;
    lowercase.toLowerCase();
    return lowercase.endsWith(".bin");
}

void OtaManager::resetProgress() {
    updatePhase_ = "";
    progressBytes_ = 0;
    progressTotalBytes_ = 0;
    progressPercent_ = 0;
}

void OtaManager::scheduleReboot(unsigned long delayMs) {
    rebootPending_ = true;
    rebootAtMs_ = millis() + delayMs;
}

void OtaManager::syncAppState(const String& lastResult, const String& lastError) {
    if (appState_ != nullptr) {
        appState_->setOta(busy_, updateAvailable_, latestVersion_, lastResult, lastError, updatePhase_, progressPercent_);
    }
}

void OtaManager::pumpProgressCallback() {
    if (progressCallback_ != nullptr) {
        progressCallback_();
    }
    yield();
}

void OtaManager::begin(const SettingsBundle& settings, AppState& appState) {
    appState_ = &appState;
    applySettings(settings);
}

void OtaManager::setProgressCallback(void (*callback)()) {
    progressCallback_ = callback;
}

void OtaManager::applySettings(const SettingsBundle& settings) {
    settings_ = settings;
}

void OtaManager::loop() {
    if (rebootPending_ && static_cast<long>(millis() - rebootAtMs_) >= 0) {
        rebootPending_ = false;
        ESP.restart();
    }
    if (pendingReleaseRefresh_ && !busy_ && !releaseRefreshInProgress_ &&
        static_cast<long>(millis() - releaseRefreshNextAttemptAtMs_) >= 0) {
        runReleaseRefreshTask();
        return;
    }
    if (!pendingInstallVersion_.isEmpty() && !busy_) {
        const String version = pendingInstallVersion_;
        const String assetName = pendingInstallAssetName_;
        pendingInstallVersion_ = "";
        pendingInstallAssetName_ = "";
        runVersionTask(version, assetName);
        return;
    }
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
    selectedVersion_ = "";
    selectedAssetName_ = "";
    resetProgress();
    lastMessage_ = applyAfterCheck ? "queued update" : "queued check";
    syncAppState("queued");
    return true;
}

bool OtaManager::beginLocalUpload(const String& filename, size_t totalSize, String& error) {
    if (busy_) {
        error = "Another update is already in progress.";
        return false;
    }
    if (!hasBinExtension(filename)) {
        error = "Select a .bin firmware image.";
        return false;
    }

    selectedVersion_ = "local";
    lastMessage_ = "Flashing local firmware...";
    updatePhase_ = "Flashing";
    progressBytes_ = 0;
    progressTotalBytes_ = totalSize;
    progressPercent_ = 0;
    localUploadStarted_ = true;
    localUploadHadData_ = false;
    localUploadOk_ = false;
    busy_ = true;

    const size_t updateSize = totalSize > 0 ? totalSize : UPDATE_SIZE_UNKNOWN;
    if (!Update.begin(updateSize, U_FLASH)) {
        error = String("Local firmware update failed: ") + Update.errorString();
        abortLocalUpload(error);
        return false;
    }
    syncAppState("updating");
    pumpProgressCallback();
    return true;
}

bool OtaManager::writeLocalUploadChunk(const uint8_t* data, size_t len, String& error) {
    if (!localUploadStarted_ || !busy_) {
        error = "Local upload is not active.";
        return false;
    }
    if (len == 0) {
        return true;
    }

    if (!localUploadHadData_) {
        localUploadHadData_ = true;
        if (data[0] != 0xE9) {
            error = "Uploaded file is not a valid ESP32 firmware image.";
            abortLocalUpload(error);
            return false;
        }
    }

    if (Update.write(const_cast<uint8_t*>(data), len) != len) {
        error = String("Local firmware update failed: ") + Update.errorString();
        abortLocalUpload(error);
        return false;
    }

    progressBytes_ += len;
    if (progressTotalBytes_ > 0) {
        progressPercent_ = static_cast<uint8_t>(min<size_t>(100U, (progressBytes_ * 100U) / progressTotalBytes_));
        lastMessage_ = String("Flashing local firmware... ") + progressPercent_ + "%";
    }
    syncAppState("updating");
    pumpProgressCallback();
    return true;
}

bool OtaManager::finishLocalUpload(String& error) {
    if (!localUploadStarted_) {
        error = "Firmware upload did not start.";
        return false;
    }
    if (!localUploadHadData_) {
        error = "Uploaded firmware did not contain data.";
        abortLocalUpload(error);
        return false;
    }
    if (!Update.end(true)) {
        error = String("Local firmware update failed: ") + Update.errorString();
        abortLocalUpload(error);
        return false;
    }
    if (!Update.isFinished()) {
        error = "Local firmware update failed: incomplete write.";
        abortLocalUpload(error);
        return false;
    }

    localUploadOk_ = true;
    localUploadStarted_ = false;
    busy_ = false;
    progressBytes_ = progressTotalBytes_;
    progressPercent_ = 100;
    updatePhase_ = "";
    lastMessage_ = "Local firmware uploaded. Restarting...";
    syncAppState("installed");
    pumpProgressCallback();
    scheduleReboot(1500);
    return true;
}

void OtaManager::abortLocalUpload(const String& error) {
    Update.abort();
    localUploadStarted_ = false;
    localUploadHadData_ = false;
    localUploadOk_ = false;
    busy_ = false;
    resetProgress();
    selectedVersion_ = "local";
    lastMessage_ = error;
    syncAppState("error", error);
    pumpProgressCallback();
}

void OtaManager::appendStatusJson(JsonObject root) const {
    root["busy"] = busy_;
    root["updateAvailable"] = updateAvailable_;
    root["latestVersion"] = latestVersion_;
    root["message"] = lastMessage_;
    root["selectedVersion"] = selectedVersion_;
    root["updatePhase"] = updatePhase_;
    root["updateProgress"] = progressPercent_;
    root["updateBytes"] = progressBytes_;
    root["updateTotalBytes"] = progressTotalBytes_;
}

void OtaManager::appendFirmwareInfoJson(JsonObject root, bool refresh, String& error) {
    root["currentVersion"] = normalizeVersion(APP_VERSION);
    root["updateBusy"] = busy_;
    root["updateStatus"] = lastMessage_;
    root["selectedVersion"] = selectedVersion_;
    root["selectedAssetName"] = selectedAssetName_;
    root["updatePhase"] = updatePhase_;
    root["updateProgress"] = progressPercent_;
    root["updateBytes"] = progressBytes_;
    root["updateTotalBytes"] = progressTotalBytes_;
    root["releaseRefreshPending"] = pendingReleaseRefresh_;
    root["releaseRefreshInProgress"] = releaseRefreshInProgress_;
    root["releaseRefreshAttemptsRemaining"] = releaseRefreshAttemptsRemaining_;
    root["releaseRefreshAttemptsStarted"] = releaseRefreshAttemptsStarted_;

    if (refresh) {
        if (busy_) {
            error = "Another update is already in progress.";
        } else if (!pendingReleaseRefresh_ && !releaseRefreshInProgress_) {
            pendingReleaseRefresh_ = true;
            releaseRefreshAttemptsRemaining_ = RELEASE_REFRESH_MAX_ATTEMPTS;
            releaseRefreshAttemptsStarted_ = 0;
            releaseRefreshNextAttemptAtMs_ = millis();
            releaseRefreshError_ = "";
            lastMessage_ = "queued release refresh";
            root["updateStatus"] = lastMessage_;
            root["releaseRefreshPending"] = true;
        } else {
            root["releaseRefreshPending"] = pendingReleaseRefresh_;
            root["releaseRefreshInProgress"] = releaseRefreshInProgress_;
        }
    }

    root["latestVersion"] = latestVersion_;
    if (!releaseCache_.empty()) {
        JsonArray releases = root["releases"].to<JsonArray>();
        for (const ReleaseInfo& release : releaseCache_) {
            JsonObject item = releases.add<JsonObject>();
            item["tag"] = release.tag;
            item["name"] = release.name;
            item["publishedAt"] = release.publishedAt;
            item["assetUrl"] = release.assetUrl;
            item["assetName"] = release.assetName;
            item["variantLabel"] = release.variantLabel;
            item["prerelease"] = release.prerelease;
            item["isInstalled"] = release.isInstalled;
            item["isLatest"] = release.isLatest;
            item["isNew"] = release.isNew;
        }
    }

    if (!error.isEmpty()) {
        root["error"] = error;
    } else if (!releaseRefreshError_.isEmpty()) {
        root["error"] = releaseRefreshError_;
    }
}

bool OtaManager::triggerInstallVersion(const String& version, String& error) {
    if (busy_) {
        error = "Another update is already in progress.";
        return false;
    }

    if (version.isEmpty()) {
        error = "Select a firmware release first.";
        return false;
    }

    const String normalizedVersion = normalizeVersion(version);
    pendingInstallVersion_ = normalizedVersion;
    pendingInstallAssetName_ = "";
    selectedVersion_ = normalizedVersion;
    selectedAssetName_ = "";
    resetProgress();
    lastMessage_ = String("queued install ") + normalizedVersion;
    syncAppState("queued");
    return true;
}

bool OtaManager::triggerInstallVersion(const String& version, const String& assetName, String& error) {
    if (assetName.isEmpty()) {
        return triggerInstallVersion(version, error);
    }
    if (busy_) {
        error = "Another update is already in progress.";
        return false;
    }
    if (version.isEmpty()) {
        error = "Select a firmware release first.";
        return false;
    }

    const String normalizedVersion = normalizeVersion(version);
    pendingInstallVersion_ = normalizedVersion;
    pendingInstallAssetName_ = assetName;
    selectedVersion_ = normalizedVersion;
    selectedAssetName_ = assetName;
    resetProgress();
    lastMessage_ = String("queued install ") + assetName;
    syncAppState("queued");
    return true;
}

void OtaManager::runReleaseRefreshTask() {
    releaseRefreshInProgress_ = true;
    releaseRefreshError_ = "";
    if (releaseRefreshAttemptsRemaining_ > 0) {
        releaseRefreshAttemptsRemaining_--;
    }
    releaseRefreshAttemptsStarted_++;
    lastMessage_ = String("checking releases (attempt ") + releaseRefreshAttemptsStarted_ + "/" + RELEASE_REFRESH_MAX_ATTEMPTS + ")";
    syncAppState("checking releases");

    String error;
    const bool success = fetchAvailableReleases(true, error);

    releaseRefreshInProgress_ = false;
    if (success) {
        pendingReleaseRefresh_ = false;
        releaseRefreshAttemptsRemaining_ = 0;
        lastMessage_ = releaseCache_.empty() ? "No firmware releases found." : "Firmware releases refreshed";
        syncAppState("releases refreshed");
        return;
    }

    if (releaseRefreshAttemptsRemaining_ > 0) {
        pendingReleaseRefresh_ = true;
        releaseRefreshError_ = error;
        releaseRefreshNextAttemptAtMs_ = millis() + RELEASE_REFRESH_RETRY_DELAY_MS;
        lastMessage_ = String("Retrying release refresh...");
        syncAppState("release refresh retry", error);
        return;
    }

    pendingReleaseRefresh_ = false;
    releaseRefreshError_ = error;
    lastMessage_ = error;
    syncAppState("release refresh failed", error);
}

void OtaManager::runTask(bool applyAfterCheck) {
    busy_ = true;
    selectedVersion_ = "";
    resetProgress();
    updatePhase_ = "Checking";
    lastMessage_ = applyAfterCheck ? "checking and applying" : "checking";
    syncAppState(applyAfterCheck ? "checking/apply" : "checking");

    const CheckResult result = checkNow();
    latestVersion_ = result.latestVersion;
    updateAvailable_ = result.updateAvailable;
    lastMessage_ = result.message;
    if (!result.success) {
        syncAppState("error", result.message);
        busy_ = false;
        return;
    }

    if (!applyAfterCheck || !result.updateAvailable) {
        updatePhase_ = "";
        syncAppState(result.updateAvailable ? "available" : "current");
        busy_ = false;
        return;
    }

    String installMessage;
    if (!installNow(result, installMessage)) {
        lastMessage_ = installMessage;
        syncAppState("error", installMessage);
        busy_ = false;
        return;
    }

    lastMessage_ = installMessage;
    syncAppState("installed");
    busy_ = false;
    scheduleReboot(1500);
}

void OtaManager::runVersionTask(const String& version, const String& assetName) {
    busy_ = true;
    selectedVersion_ = version;
    selectedAssetName_ = assetName;
    resetProgress();
    updatePhase_ = "Checking";
    lastMessage_ = assetName.isEmpty() ? String("checking ") + version : String("checking ") + assetName;
    syncAppState("checking");

    CheckResult result;
    String error;
    if (!resolveVersionResult(version, assetName, result, error)) {
        lastMessage_ = error;
        syncAppState("error", error);
        busy_ = false;
        return;
    }

    latestVersion_ = releaseCache_.empty() ? version : latestVersion_;
    updateAvailable_ = compareVersions(normalizeVersion(APP_VERSION), version) < 0;

    String installMessage;
    if (!installNow(result, installMessage)) {
        lastMessage_ = installMessage;
        syncAppState("error", installMessage);
        busy_ = false;
        return;
    }

    lastMessage_ = installMessage;
    syncAppState("installed");
    busy_ = false;
    scheduleReboot(1500);
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
    if (value.isEmpty()) {
        return "";
    }
    if (value.startsWith("v") || value.startsWith("V")) {
        return value;
    }
    return "v" + value;
}

bool OtaManager::fetchAvailableReleases(bool refresh, String& error) {
    if (!refresh && !releaseCache_.empty() && (millis() - releasesFetchedAtMs_) < RELEASE_CACHE_TTL_MS) {
        return true;
    }

    if (WiFi.status() != WL_CONNECTED) {
        error = "Connect to Wi-Fi to check GitHub releases.";
        return false;
    }

    WiFiClientSecure client;
    HTTPClient http;
    const int code = beginAndGet(
        http,
        client,
        githubApiReleasesUrl(settings_),
        settings_.ota.allowInsecureTls,
        10000,
        [](HTTPClient& request) {
            request.addHeader("Accept", "application/vnd.github+json");
            request.addHeader("User-Agent", String(APP_NAME "/" APP_VERSION));
            request.addHeader("X-GitHub-Api-Version", "2022-11-28");
        });
    if (code == HTTPC_ERROR_CONNECTION_REFUSED) {
        error = "Could not open GitHub releases API.";
        return false;
    }
    if (code != HTTP_CODE_OK) {
        error = httpErrorWithDetail(http, "GitHub API error: HTTP ", code);
        http.end();
        return false;
    }

    JsonDocument filter;
    JsonObject releaseFilter = filter[0].to<JsonObject>();
    releaseFilter["tag_name"] = true;
    releaseFilter["name"] = true;
    releaseFilter["draft"] = true;
    releaseFilter["prerelease"] = true;
    releaseFilter["published_at"] = true;
    JsonObject assetFilter = releaseFilter["assets"][0].to<JsonObject>();
    assetFilter["name"] = true;
    assetFilter["browser_download_url"] = true;

    JsonDocument doc;
    const DeserializationError parseError = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    if (parseError != DeserializationError::Ok) {
        error = String("GitHub response parse failed: ") + parseError.c_str();
        return false;
    }
    if (!doc.is<JsonArray>()) {
        error = "GitHub response format invalid.";
        return false;
    }

    releaseCache_.clear();
    latestVersion_ = "";
    const String currentVersion = normalizeVersion(APP_VERSION);
    const String installedAssetName = currentReleaseAssetName(settings_);
    for (JsonObjectConst release : doc.as<JsonArrayConst>()) {
        if (release["draft"] | false) {
            continue;
        }

        const String releaseTag = normalizeVersion(String(static_cast<const char*>(release["tag_name"] | "")));
        if (releaseTag.isEmpty()) {
            continue;
        }
        const String releaseName = String(static_cast<const char*>(release["name"] | ""));
        const String publishedAt = String(static_cast<const char*>(release["published_at"] | ""));
        const bool prerelease = release["prerelease"] | false;
        if (latestVersion_.isEmpty() && !prerelease) {
            latestVersion_ = releaseTag;
        }

        bool matchedAsset = false;
        for (JsonObjectConst asset : release["assets"].as<JsonArrayConst>()) {
            const String assetName = String(static_cast<const char*>(asset["name"] | ""));
            if (!hasBinExtension(assetName)) {
                continue;
            }

            ReleaseInfo item;
            item.tag = releaseTag;
            item.name = releaseName;
            item.publishedAt = publishedAt;
            item.assetName = assetName;
            item.assetUrl = String(static_cast<const char*>(asset["browser_download_url"] | ""));
            if (item.assetUrl.isEmpty()) {
                item.assetUrl = githubReleaseAssetUrl(settings_, releaseTag, assetName);
            }
            item.variantLabel = variantLabelForAssetName(assetName);
            item.prerelease = prerelease;
            item.isInstalled = compareVersions(currentVersion, releaseTag) == 0 && assetName == installedAssetName;
            item.isLatest = !latestVersion_.isEmpty() && releaseTag == latestVersion_;
            item.isNew = compareVersions(currentVersion, releaseTag) < 0;
            releaseCache_.push_back(item);
            matchedAsset = true;
        }

        if (!matchedAsset) {
            ReleaseInfo item;
            item.tag = releaseTag;
            item.name = releaseName;
            item.publishedAt = publishedAt;
            item.prerelease = prerelease;
            item.assetName = applyVersionTemplate(settings_.ota.assetTemplate, releaseTag);
            item.assetUrl = githubReleaseAssetUrl(settings_, releaseTag, item.assetName);
            item.variantLabel = variantLabelForAssetName(item.assetName);
            item.isInstalled = compareVersions(currentVersion, releaseTag) == 0 && item.assetName == installedAssetName;
            item.isLatest = !latestVersion_.isEmpty() && releaseTag == latestVersion_;
            item.isNew = compareVersions(currentVersion, releaseTag) < 0;
            releaseCache_.push_back(item);
        }
    }

    if (latestVersion_.isEmpty() && !releaseCache_.empty()) {
        latestVersion_ = releaseCache_.front().tag;
        releaseCache_.front().isLatest = true;
    }
    releasesFetchedAtMs_ = millis();
    return true;
}

bool OtaManager::resolveVersionResult(const String& version, const String& assetName, CheckResult& result, String& error) {
    if (!fetchAvailableReleases(true, error)) {
        return false;
    }

    for (const ReleaseInfo& release : releaseCache_) {
        if (release.tag != version) {
            continue;
        }
        if (!assetName.isEmpty() && release.assetName != assetName) {
            continue;
        }
        result.success = true;
        result.latestVersion = release.tag;
        result.assetName = release.assetName;
        result.assetUrl = release.assetUrl;
        result.updateAvailable = compareVersions(normalizeVersion(APP_VERSION), release.tag) < 0;
        result.message = result.updateAvailable ? "update available" : "selected release ready";
        return true;
    }

    error = assetName.isEmpty() ? String("Release not found: ") + version : String("Release asset not found: ") + assetName;
    return false;
}

OtaManager::CheckResult OtaManager::checkNow() {
    CheckResult result;
    if (WiFi.status() != WL_CONNECTED) {
        result.message = "Wi-Fi not connected";
        return result;
    }

    WiFiClientSecure client;
    HTTPClient http;

    if (!settings_.ota.manifestUrl.isEmpty()) {
        const int code = beginAndGet(
            http,
            client,
            settings_.ota.manifestUrl,
            settings_.ota.allowInsecureTls,
            10000,
            [](HTTPClient&) {});
        if (code == HTTPC_ERROR_CONNECTION_REFUSED) {
            result.message = "Failed to open manifest URL";
            return result;
        }
        if (code != HTTP_CODE_OK) {
            result.message = httpErrorWithDetail(http, "Manifest HTTP ", code);
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
        const int code = beginAndGet(
            http,
            client,
            githubApiLatestUrl(settings_),
            settings_.ota.allowInsecureTls,
            10000,
            [](HTTPClient& request) {
                request.addHeader("Accept", "application/vnd.github+json");
                request.addHeader("User-Agent", String(APP_NAME "/" APP_VERSION));
            });
        if (code == HTTPC_ERROR_CONNECTION_REFUSED) {
            result.message = "Failed to open GitHub releases API";
            return result;
        }
        if (code != HTTP_CODE_OK) {
            result.message = httpErrorWithDetail(http, "GitHub API HTTP ", code);
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
    selectedVersion_ = result.latestVersion;
    selectedAssetName_ = result.assetName;
    updatePhase_ = "Flashing";
    progressBytes_ = 0;
    progressTotalBytes_ = 0;
    progressPercent_ = 0;
    syncAppState("updating");
    pumpProgressCallback();
    WiFiClientSecure client;
    HTTPClient http;
    const int code = beginAndGet(
        http,
        client,
        result.assetUrl,
        settings_.ota.allowInsecureTls,
        15000,
        [](HTTPClient&) {});
    if (code == HTTPC_ERROR_CONNECTION_REFUSED) {
        message = "Failed to open firmware URL";
        return false;
    }
    if (code != HTTP_CODE_OK) {
        message = httpErrorWithDetail(http, "Firmware HTTP ", code);
        http.end();
        return false;
    }
    const int contentLength = http.getSize();
    if (contentLength <= 0) {
        message = "Invalid content length";
        http.end();
        return false;
    }
    progressTotalBytes_ = static_cast<size_t>(contentLength);
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
            pumpProgressCallback();
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
        progressBytes_ = static_cast<size_t>(written);
        progressPercent_ = static_cast<uint8_t>(min(100, (written * 100) / contentLength));
        lastMessage_ = String("Flashing firmware... ") + progressPercent_ + "%";
        syncAppState("updating");
        pumpProgressCallback();
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
    updatePhase_ = "";
    progressBytes_ = progressTotalBytes_;
    progressPercent_ = 100;
    message = "update installed";
    syncAppState("installed");
    pumpProgressCallback();
    return true;
}
