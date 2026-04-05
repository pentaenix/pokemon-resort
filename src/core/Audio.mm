#include "core/Audio.hpp"

#if defined(__APPLE__)

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

namespace pr {

class AudioController::Impl {
public:
    ~Impl() {
        if (player_ != nil) {
            [player_ stop];
            [player_ release];
            player_ = nil;
        }
        if (button_player_ != nil) {
            [button_player_ stop];
            [button_player_ release];
            button_player_ = nil;
        }
    }

    bool loadMusic(const std::string& path) {
        stopMusic();

        NSString* ns_path = [NSString stringWithUTF8String:path.c_str()];
        if (ns_path == nil) {
            return false;
        }

        NSURL* url = [NSURL fileURLWithPath:ns_path];
        NSError* error = nil;
        AVAudioPlayer* player = [[AVAudioPlayer alloc] initWithContentsOfURL:url error:&error];
        if (player == nil) {
            return false;
        }

        player.numberOfLoops = -1;
        [player prepareToPlay];
        player_ = player;
        return true;
    }

    bool loadButtonSfx(const std::string& path) {
        if (button_player_ != nil) {
            [button_player_ stop];
            [button_player_ release];
            button_player_ = nil;
        }

        NSString* ns_path = [NSString stringWithUTF8String:path.c_str()];
        if (ns_path == nil) {
            return false;
        }

        NSURL* url = [NSURL fileURLWithPath:ns_path];
        NSError* error = nil;
        AVAudioPlayer* player = [[AVAudioPlayer alloc] initWithContentsOfURL:url error:&error];
        if (player == nil) {
            return false;
        }

        [player prepareToPlay];
        button_player_ = player;
        return true;
    }

    void playMusicLoop() {
        if (player_ != nil && !player_.playing) {
            [player_ play];
        }
    }

    void playButtonSfx() {
        if (button_player_ != nil) {
            button_player_.currentTime = 0.0;
            [button_player_ play];
        }
    }

    void stopMusic() {
        if (player_ != nil) {
            [player_ stop];
            player_.currentTime = 0.0;
        }
    }

    void setMusicVolume(float volume_01) {
        if (player_ != nil) {
            const float clamped = volume_01 < 0.0f ? 0.0f : (volume_01 > 1.0f ? 1.0f : volume_01);
            player_.volume = clamped;
        }
    }

    void setSfxVolume(float volume_01) {
        if (button_player_ != nil) {
            const float clamped = volume_01 < 0.0f ? 0.0f : (volume_01 > 1.0f ? 1.0f : volume_01);
            button_player_.volume = clamped;
        }
    }

    bool isMusicLoaded() const {
        return player_ != nil;
    }

    bool isMusicPlaying() const {
        return player_ != nil && player_.playing;
    }

private:
    AVAudioPlayer* player_ = nil;
    AVAudioPlayer* button_player_ = nil;
};

AudioController::AudioController()
    : impl_(new Impl()) {}

AudioController::~AudioController() {
    delete impl_;
}

AudioController::AudioController(AudioController&& other) noexcept
    : impl_(other.impl_) {
    other.impl_ = nullptr;
}

AudioController& AudioController::operator=(AudioController&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    delete impl_;
    impl_ = other.impl_;
    other.impl_ = nullptr;
    return *this;
}

bool AudioController::loadMusic(const std::string& path) {
    return impl_ != nullptr && impl_->loadMusic(path);
}

bool AudioController::loadButtonSfx(const std::string& path) {
    return impl_ != nullptr && impl_->loadButtonSfx(path);
}

void AudioController::playMusicLoop() {
    if (impl_ != nullptr) {
        impl_->playMusicLoop();
    }
}

void AudioController::playButtonSfx() {
    if (impl_ != nullptr) {
        impl_->playButtonSfx();
    }
}

void AudioController::stopMusic() {
    if (impl_ != nullptr) {
        impl_->stopMusic();
    }
}

void AudioController::setMusicVolume(float volume_01) {
    if (impl_ != nullptr) {
        impl_->setMusicVolume(volume_01);
    }
}

void AudioController::setSfxVolume(float volume_01) {
    if (impl_ != nullptr) {
        impl_->setSfxVolume(volume_01);
    }
}

bool AudioController::isMusicLoaded() const {
    return impl_ != nullptr && impl_->isMusicLoaded();
}

bool AudioController::isMusicPlaying() const {
    return impl_ != nullptr && impl_->isMusicPlaying();
}

} // namespace pr

#else

namespace pr {

class AudioController::Impl {};

AudioController::AudioController()
    : impl_(new Impl()) {}

AudioController::~AudioController() {
    delete impl_;
}

AudioController::AudioController(AudioController&& other) noexcept
    : impl_(other.impl_) {
    other.impl_ = nullptr;
}

AudioController& AudioController::operator=(AudioController&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    delete impl_;
    impl_ = other.impl_;
    other.impl_ = nullptr;
    return *this;
}

bool AudioController::loadMusic(const std::string&) { return false; }
bool AudioController::loadButtonSfx(const std::string&) { return false; }
void AudioController::playMusicLoop() {}
void AudioController::playButtonSfx() {}
void AudioController::stopMusic() {}
void AudioController::setMusicVolume(float) {}
void AudioController::setSfxVolume(float) {}
bool AudioController::isMusicLoaded() const { return false; }
bool AudioController::isMusicPlaying() const { return false; }

} // namespace pr

#endif
