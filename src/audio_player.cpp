#include "audio_player.h"

#include <Audio.h>

namespace {
AudioPlayer::Impl* g_impl = nullptr;

String fallbackTitle(const String& url) {
    const int lastSlash = url.lastIndexOf('/');
    if (lastSlash >= 0 && lastSlash < url.length() - 1) {
        return url.substring(lastSlash + 1);
    }
    return url;
}
}  // namespace

class AudioPlayer::Impl {
  public:
    Audio audio;
    AppState* appState = nullptr;
    uint8_t volume = 60;
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
            title = fallbackTitle(url);
        }
        publish();
    }
};

void audio_info(const char* info) {
    if (g_impl != nullptr && info != nullptr) {
        g_impl->publish();
    }
}

void audio_showstation(const char* info) {
    if (g_impl != nullptr && info != nullptr) {
        g_impl->title = info;
        g_impl->publish();
    }
}

void audio_showstreamtitle(const char* info) {
    if (g_impl != nullptr && info != nullptr) {
        g_impl->title = info;
        g_impl->publish();
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
    impl_->audio.setPinout(bclkPin, wsPin, doutPin);
    impl_->audio.forceMono(false);
    impl_->audio.setConnectionTimeout(3000, 3000);
    setVolumePercent(initialVolumePercent);
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
    impl_->stopRequested = false;
    impl_->retryPending = false;
    impl_->retryCount = 0;
    impl_->url = url;
    impl_->title = title;
    impl_->type = mediaType;
    impl_->source = source;
    impl_->state = "buffering";
    impl_->publish();
    impl_->audio.stopSong();
    const bool connected = impl_->audio.connecttohost(url.c_str());
    if (!connected) {
        impl_->state = "error";
        impl_->publish();
        return false;
    }
    impl_->markPlaying();
    return true;
}

void AudioPlayer::stop() {
    if (impl_ == nullptr) {
        return;
    }
    impl_->stopRequested = true;
    impl_->retryPending = false;
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
    impl_->volume = constrain(volumePercent, static_cast<uint8_t>(0), static_cast<uint8_t>(100));
    const uint8_t audioVolume = map(impl_->volume, 0, 100, 0, 21);
    impl_->audio.setVolume(audioVolume);
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
