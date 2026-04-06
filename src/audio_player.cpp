#include "audio_player.h"

#include <Audio.h>

#include "default_config.h"
#include "playback_text.h"

namespace {
AudioPlayer::Impl* g_impl = nullptr;

constexpr unsigned long kSwitchFadeOutMs = 70UL;
constexpr unsigned long kStartFadeInMs = 90UL;
constexpr unsigned long kSwitchQuietTimeMs = 18UL;
}  // namespace

class AudioPlayer::Impl {
  public:
    Audio audio;
    AppState* appState = nullptr;
    uint8_t volume = DefaultConfig::DEFAULT_VOLUME_PERCENT;
    uint8_t hardwareAudioVolume = 0;
    String state = "idle";
    String type = "idle";
    String title = "Idle";
    String url;
    String source = "none";
    bool retryPending = false;
    uint8_t retryCount = 0;
    unsigned long retryAt = 0;
    bool stopRequested = false;

    void publish() {
        if (appState != nullptr) {
            appState->setPlayback(state, type, title, url, source, volume);
        }
    }

    void markPlaying() {
        state = "playing";
        if (title.isEmpty()) {
            title = PlaybackText::fallbackTitleFromUrl(url);
        }
        publish();
    }

    void setHardwareAudioVolume(uint8_t audioVolume) {
        hardwareAudioVolume = constrain(audioVolume, static_cast<uint8_t>(0), static_cast<uint8_t>(21));
        audio.setVolume(hardwareAudioVolume);
    }

    void applyHardwareVolumePercent(uint8_t volumePercent) {
        setHardwareAudioVolume(map(volumePercent, 0, 100, 0, 21));
    }

    void fadeToPercent(uint8_t targetVolumePercent, unsigned long durationMs) {
        const int targetAudioVolume = map(targetVolumePercent, 0, 100, 0, 21);
        if (hardwareAudioVolume == targetAudioVolume) {
            return;
        }

        const int direction = hardwareAudioVolume < targetAudioVolume ? 1 : -1;
        const unsigned long steps = static_cast<unsigned long>(abs(targetAudioVolume - hardwareAudioVolume));
        const unsigned long stepDelayMs = steps == 0 ? 0 : max<unsigned long>(4UL, durationMs / steps);

        while (hardwareAudioVolume != targetAudioVolume) {
            setHardwareAudioVolume(static_cast<uint8_t>(hardwareAudioVolume + direction));
            delay(stepDelayMs);
            yield();
        }
    }
};

void audio_info(const char* info) {
    (void)info;
}

void audio_showstation(const char* info) {
    if (g_impl != nullptr && info != nullptr) {
        const String nextTitle = PlaybackText::normalizeTitle(String(info), g_impl->url);
        if (!nextTitle.isEmpty() && g_impl->title != nextTitle) {
            g_impl->title = nextTitle;
            g_impl->publish();
        }
    }
}

void audio_showstreamtitle(const char* info) {
    if (g_impl != nullptr && info != nullptr) {
        const String nextTitle = PlaybackText::normalizeTitle(String(info), g_impl->url);
        if (!nextTitle.isEmpty() && g_impl->title != nextTitle) {
            g_impl->title = nextTitle;
            g_impl->publish();
        }
    }
}

void audio_eof_stream(const char* info) {
    if (g_impl != nullptr) {
        g_impl->state = "idle";
        g_impl->publish();
        if (!g_impl->stopRequested && !g_impl->url.isEmpty() && g_impl->retryCount < 3) {
            g_impl->retryPending = true;
            g_impl->retryCount++;
            g_impl->retryAt = millis() + 2000UL * g_impl->retryCount;
        }
    }
    (void)info;
}

void AudioPlayer::begin(uint8_t bclkPin, uint8_t wsPin, uint8_t doutPin, uint8_t initialVolumePercent, AppState& appState) {
    if (impl_ == nullptr) {
        impl_ = new Impl();
    }
    impl_->appState = &appState;
    g_impl = impl_;
    impl_->audio.setBufsize(DefaultConfig::AUDIO_BUFFER_SIZE_RAM, DefaultConfig::AUDIO_BUFFER_SIZE_PSRAM);
    impl_->audio.setPinout(bclkPin, wsPin, doutPin);
    impl_->audio.forceMono(false);
    impl_->audio.setConnectionTimeout(8000, 8000);
    impl_->volume = constrain(initialVolumePercent, static_cast<uint8_t>(0), static_cast<uint8_t>(100));
    impl_->applyHardwareVolumePercent(impl_->volume);
    impl_->publish();
}

void AudioPlayer::loop() {
    if (impl_ == nullptr) {
        return;
    }
    impl_->audio.loop();
    if (impl_->retryPending && millis() >= impl_->retryAt) {
        impl_->retryPending = false;
        impl_->audio.stopSong();
        impl_->audio.connecttohost(impl_->url.c_str());
        impl_->state = "buffering";
        impl_->publish();
    }
}

bool AudioPlayer::play(const String& url, const String& title, const String& mediaType, const String& source) {
    if (impl_ == nullptr || url.isEmpty()) {
        return false;
    }

    const String normalizedUrl = PlaybackText::normalizeUrl(url);
    const String normalizedTitle = PlaybackText::normalizeTitle(title, normalizedUrl);

    if (impl_->audio.isRunning() || impl_->state == "playing" || impl_->state == "buffering") {
        impl_->fadeToPercent(0, kSwitchFadeOutMs);
        impl_->audio.stopSong();
        delay(kSwitchQuietTimeMs);
    }

    impl_->stopRequested = false;
    impl_->retryPending = false;
    impl_->retryCount = 0;
    impl_->url = normalizedUrl;
    impl_->title = normalizedTitle;
    impl_->type = mediaType;
    impl_->source = source;
    impl_->state = "buffering";
    impl_->publish();
    impl_->applyHardwareVolumePercent(0);

    bool connected = impl_->audio.connecttohost(normalizedUrl.c_str());
    if (!connected) {
        delay(120);
        connected = impl_->audio.connecttohost(normalizedUrl.c_str());
    }
    if (!connected) {
        impl_->applyHardwareVolumePercent(impl_->volume);
        impl_->state = "error";
        impl_->publish();
        return false;
    }

    impl_->markPlaying();
    impl_->fadeToPercent(impl_->volume, kStartFadeInMs);
    return true;
}

void AudioPlayer::stop() {
    if (impl_ == nullptr) {
        return;
    }

    impl_->stopRequested = true;
    impl_->retryPending = false;
    if (impl_->audio.isRunning() || impl_->state == "playing" || impl_->state == "buffering") {
        impl_->fadeToPercent(0, kSwitchFadeOutMs);
        delay(kSwitchQuietTimeMs);
    }
    impl_->audio.stopSong();
    impl_->state = "idle";
    impl_->type = "idle";
    impl_->title = "Idle";
    impl_->url = "";
    impl_->source = "manual";
    impl_->publish();
}

void AudioPlayer::setVolumePercent(uint8_t volumePercent) {
    if (impl_ == nullptr) {
        return;
    }
    const uint8_t nextVolume = constrain(volumePercent, static_cast<uint8_t>(0), static_cast<uint8_t>(100));
    if (impl_->volume == nextVolume) {
        return;
    }
    impl_->volume = nextVolume;
    impl_->applyHardwareVolumePercent(impl_->volume);
    impl_->publish();
}

uint8_t AudioPlayer::volumePercent() const {
    return impl_ == nullptr ? 0 : impl_->volume;
}

String AudioPlayer::currentTitle() const {
    return impl_ == nullptr ? String() : impl_->title;
}

String AudioPlayer::currentUrl() const {
    return impl_ == nullptr ? String() : impl_->url;
}

String AudioPlayer::currentState() const {
    return impl_ == nullptr ? String("idle") : impl_->state;
}

void AudioPlayer::onStationName(const char* text) { (void)text; }
void AudioPlayer::onStreamTitle(const char* text) { (void)text; }
void AudioPlayer::onInfo(const char* text) { (void)text; }
void AudioPlayer::onEof(const char* text) { (void)text; }
