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
        if (rip_player_ != nil) {
            [rip_player_ stop];
            [rip_player_ release];
            rip_player_ = nil;
        }
        if (ui_move_player_ != nil) {
            [ui_move_player_ stop];
            [ui_move_player_ release];
            ui_move_player_ = nil;
        }
        if (pickup_player_ != nil) {
            [pickup_player_ stop];
            [pickup_player_ release];
            pickup_player_ = nil;
        }
        if (putdown_player_ != nil) {
            [putdown_player_ stop];
            [putdown_player_ release];
            putdown_player_ = nil;
        }
        if (error_player_ != nil) {
            [error_player_ stop];
            [error_player_ release];
            error_player_ = nil;
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

    bool loadRipSfx(const std::string& path) {
        if (rip_player_ != nil) {
            [rip_player_ stop];
            [rip_player_ release];
            rip_player_ = nil;
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
        rip_player_ = player;
        return true;
    }

    bool loadUiMoveSfx(const std::string& path) {
        if (ui_move_player_ != nil) {
            [ui_move_player_ stop];
            [ui_move_player_ release];
            ui_move_player_ = nil;
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
        ui_move_player_ = player;
        return true;
    }

    bool loadPickupSfx(const std::string& path) {
        return loadSfx(path, pickup_player_);
    }

    bool loadPutdownSfx(const std::string& path) {
        return loadSfx(path, putdown_player_);
    }

    bool loadErrorSfx(const std::string& path) {
        return loadSfx(path, error_player_);
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

    void playRipSfx() {
        if (rip_player_ != nil) {
            rip_player_.currentTime = 0.0;
            [rip_player_ play];
        }
    }

    void playUiMoveSfx() {
        if (ui_move_player_ != nil) {
            ui_move_player_.currentTime = 0.0;
            [ui_move_player_ play];
        }
    }

    void playPickupSfx() {
        playSfx(pickup_player_);
    }

    void playPutdownSfx() {
        playSfx(putdown_player_);
    }

    void playErrorSfx() {
        playSfx(error_player_);
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
        if (rip_player_ != nil) {
            const float clamped = volume_01 < 0.0f ? 0.0f : (volume_01 > 1.0f ? 1.0f : volume_01);
            rip_player_.volume = clamped;
        }
        if (ui_move_player_ != nil) {
            const float clamped = volume_01 < 0.0f ? 0.0f : (volume_01 > 1.0f ? 1.0f : volume_01);
            ui_move_player_.volume = clamped;
        }
        if (pickup_player_ != nil) {
            const float clamped = volume_01 < 0.0f ? 0.0f : (volume_01 > 1.0f ? 1.0f : volume_01);
            pickup_player_.volume = clamped;
        }
        if (putdown_player_ != nil) {
            const float clamped = volume_01 < 0.0f ? 0.0f : (volume_01 > 1.0f ? 1.0f : volume_01);
            putdown_player_.volume = clamped;
        }
        if (error_player_ != nil) {
            const float clamped = volume_01 < 0.0f ? 0.0f : (volume_01 > 1.0f ? 1.0f : volume_01);
            error_player_.volume = clamped;
        }
    }

    bool isMusicLoaded() const {
        return player_ != nil;
    }

    bool isMusicPlaying() const {
        return player_ != nil && player_.playing;
    }

private:
    bool loadSfx(const std::string& path, AVAudioPlayer*& player_slot) {
        if (player_slot != nil) {
            [player_slot stop];
            [player_slot release];
            player_slot = nil;
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
        player_slot = player;
        return true;
    }

    void playSfx(AVAudioPlayer* player) {
        if (player != nil) {
            player.currentTime = 0.0;
            [player play];
        }
    }

    AVAudioPlayer* player_ = nil;
    AVAudioPlayer* button_player_ = nil;
    AVAudioPlayer* rip_player_ = nil;
    AVAudioPlayer* ui_move_player_ = nil;
    AVAudioPlayer* pickup_player_ = nil;
    AVAudioPlayer* putdown_player_ = nil;
    AVAudioPlayer* error_player_ = nil;
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

bool AudioController::loadRipSfx(const std::string& path) {
    return impl_ != nullptr && impl_->loadRipSfx(path);
}

bool AudioController::loadUiMoveSfx(const std::string& path) {
    return impl_ != nullptr && impl_->loadUiMoveSfx(path);
}

bool AudioController::loadPickupSfx(const std::string& path) {
    return impl_ != nullptr && impl_->loadPickupSfx(path);
}

bool AudioController::loadPutdownSfx(const std::string& path) {
    return impl_ != nullptr && impl_->loadPutdownSfx(path);
}

bool AudioController::loadErrorSfx(const std::string& path) {
    return impl_ != nullptr && impl_->loadErrorSfx(path);
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

void AudioController::playRipSfx() {
    if (impl_ != nullptr) {
        impl_->playRipSfx();
    }
}

void AudioController::playUiMoveSfx() {
    if (impl_ != nullptr) {
        impl_->playUiMoveSfx();
    }
}

void AudioController::playPickupSfx() {
    if (impl_ != nullptr) {
        impl_->playPickupSfx();
    }
}

void AudioController::playPutdownSfx() {
    if (impl_ != nullptr) {
        impl_->playPutdownSfx();
    }
}

void AudioController::playErrorSfx() {
    if (impl_ != nullptr) {
        impl_->playErrorSfx();
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
bool AudioController::loadRipSfx(const std::string&) { return false; }
bool AudioController::loadUiMoveSfx(const std::string&) { return false; }
bool AudioController::loadPickupSfx(const std::string&) { return false; }
bool AudioController::loadPutdownSfx(const std::string&) { return false; }
bool AudioController::loadErrorSfx(const std::string&) { return false; }
void AudioController::playMusicLoop() {}
void AudioController::playButtonSfx() {}
void AudioController::playRipSfx() {}
void AudioController::playUiMoveSfx() {}
void AudioController::playPickupSfx() {}
void AudioController::playPutdownSfx() {}
void AudioController::playErrorSfx() {}
void AudioController::stopMusic() {}
void AudioController::setMusicVolume(float) {}
void AudioController::setSfxVolume(float) {}
bool AudioController::isMusicLoaded() const { return false; }
bool AudioController::isMusicPlaying() const { return false; }

} // namespace pr

#endif
