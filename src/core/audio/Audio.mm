#include "core/audio/Audio.hpp"

#if defined(__APPLE__)

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#if defined(PR_ENABLE_VORBIS_AUDIO)
#include <vorbis/vorbisfile.h>
#endif

namespace pr {

namespace {

std::string lowercaseExtension(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext;
}

void appendU16Le(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

void appendU32Le(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
}

void appendAscii(std::vector<std::uint8_t>& out, const char* text) {
    while (*text) {
        out.push_back(static_cast<std::uint8_t>(*text));
        ++text;
    }
}

std::vector<std::uint8_t> wavBytesFromPcm16(
    const std::vector<std::uint8_t>& pcm,
    int sample_rate,
    int channels) {
    if (pcm.empty() || sample_rate <= 0 || channels <= 0) return {};
    const std::uint32_t data_size = static_cast<std::uint32_t>(pcm.size());
    const std::uint16_t channel_count = static_cast<std::uint16_t>(channels);
    const std::uint16_t bits_per_sample = 16;
    const std::uint32_t byte_rate =
        static_cast<std::uint32_t>(sample_rate) * channel_count * bits_per_sample / 8U;
    const std::uint16_t block_align = channel_count * bits_per_sample / 8U;

    std::vector<std::uint8_t> wav;
    wav.reserve(44U + pcm.size());
    appendAscii(wav, "RIFF");
    appendU32Le(wav, 36U + data_size);
    appendAscii(wav, "WAVE");
    appendAscii(wav, "fmt ");
    appendU32Le(wav, 16U);
    appendU16Le(wav, 1U);
    appendU16Le(wav, channel_count);
    appendU32Le(wav, static_cast<std::uint32_t>(sample_rate));
    appendU32Le(wav, byte_rate);
    appendU16Le(wav, block_align);
    appendU16Le(wav, bits_per_sample);
    appendAscii(wav, "data");
    appendU32Le(wav, data_size);
    wav.insert(wav.end(), pcm.begin(), pcm.end());
    return wav;
}

#if defined(PR_ENABLE_VORBIS_AUDIO)
std::vector<std::uint8_t> decodeOggVorbisToWav(const std::string& path) {
    OggVorbis_File vf{};
    if (ov_fopen(path.c_str(), &vf) != 0) return {};
    vorbis_info* info = ov_info(&vf, -1);
    if (!info || info->rate <= 0 || info->channels <= 0) {
        ov_clear(&vf);
        return {};
    }

    std::vector<std::uint8_t> pcm;
    char buffer[4096];
    int bitstream = 0;
    while (true) {
        const long read = ov_read(&vf, buffer, sizeof(buffer), 0, 2, 1, &bitstream);
        if (read == 0) break;
        if (read < 0) {
            ov_clear(&vf);
            return {};
        }
        const auto begin = reinterpret_cast<const std::uint8_t*>(buffer);
        pcm.insert(pcm.end(), begin, begin + read);
    }
    const int rate = static_cast<int>(info->rate);
    const int channels = info->channels;
    ov_clear(&vf);
    return wavBytesFromPcm16(pcm, rate, channels);
}
#endif

} // namespace

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
        if (save_player_ != nil) {
            [save_player_ stop];
            [save_player_ release];
            save_player_ = nil;
        }
        if (overworld_blocked_player_ != nil) {
            [overworld_blocked_player_ stop];
            [overworld_blocked_player_ release];
            overworld_blocked_player_ = nil;
        }
        for (AVAudioPlayer* player : one_shot_players_) {
            if (player != nil) {
                [player stop];
                [player release];
            }
        }
        one_shot_players_.clear();
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
        return loadSfx(path, button_player_);
    }

    bool loadRipSfx(const std::string& path) {
        return loadSfx(path, rip_player_);
    }

    bool loadUiMoveSfx(const std::string& path) {
        return loadSfx(path, ui_move_player_);
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

    bool loadSaveSfx(const std::string& path) {
        return loadSfx(path, save_player_);
    }

    bool loadOverworldBlockedSfx(const std::string& path) {
        return loadSfx(path, overworld_blocked_player_);
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

    void playSaveSfx() {
        playSfx(save_player_);
    }

    void playOverworldBlockedSfx() {
        playSfx(overworld_blocked_player_);
    }

    void playOneShotSfx(const std::string& path) {
        pruneOneShotPlayers();
        NSError* error = nil;
        AVAudioPlayer* player = nil;
#if defined(PR_ENABLE_VORBIS_AUDIO)
        if (lowercaseExtension(path) == ".ogg") {
            const std::vector<std::uint8_t> wav = decodeOggVorbisToWav(path);
            if (!wav.empty()) {
                NSData* data = [NSData dataWithBytes:wav.data() length:wav.size()];
                player = [[AVAudioPlayer alloc] initWithData:data error:&error];
            }
        }
#endif
        if (player == nil) {
            NSString* ns_path = [NSString stringWithUTF8String:path.c_str()];
            if (ns_path == nil) return;
            NSURL* url = [NSURL fileURLWithPath:ns_path];
            player = [[AVAudioPlayer alloc] initWithContentsOfURL:url error:&error];
        }
        if (player == nil) return;
        player.volume = sfx_volume_;
        [player prepareToPlay];
        [player play];
        one_shot_players_.push_back(player);
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
        sfx_volume_ = volume_01 < 0.0f ? 0.0f : (volume_01 > 1.0f ? 1.0f : volume_01);
        if (button_player_ != nil) {
            button_player_.volume = sfx_volume_;
        }
        if (rip_player_ != nil) {
            rip_player_.volume = sfx_volume_;
        }
        if (ui_move_player_ != nil) {
            ui_move_player_.volume = sfx_volume_;
        }
        if (pickup_player_ != nil) {
            pickup_player_.volume = sfx_volume_;
        }
        if (putdown_player_ != nil) {
            putdown_player_.volume = sfx_volume_;
        }
        if (error_player_ != nil) {
            error_player_.volume = sfx_volume_;
        }
        if (save_player_ != nil) {
            save_player_.volume = sfx_volume_;
        }
        if (overworld_blocked_player_ != nil) {
            overworld_blocked_player_.volume = sfx_volume_;
        }
        for (AVAudioPlayer* player : one_shot_players_) {
            if (player != nil) {
                player.volume = sfx_volume_;
            }
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

    void pruneOneShotPlayers() {
        std::vector<AVAudioPlayer*> active;
        active.reserve(one_shot_players_.size());
        for (AVAudioPlayer* player : one_shot_players_) {
            if (player != nil && player.playing) {
                active.push_back(player);
            } else if (player != nil) {
                [player release];
            }
        }
        one_shot_players_ = std::move(active);
    }

    AVAudioPlayer* player_ = nil;
    AVAudioPlayer* button_player_ = nil;
    AVAudioPlayer* rip_player_ = nil;
    AVAudioPlayer* ui_move_player_ = nil;
    AVAudioPlayer* pickup_player_ = nil;
    AVAudioPlayer* putdown_player_ = nil;
    AVAudioPlayer* error_player_ = nil;
    AVAudioPlayer* save_player_ = nil;
    AVAudioPlayer* overworld_blocked_player_ = nil;
    std::vector<AVAudioPlayer*> one_shot_players_;
    float sfx_volume_ = 1.0f;
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

bool AudioController::loadSaveSfx(const std::string& path) {
    return impl_ != nullptr && impl_->loadSaveSfx(path);
}

bool AudioController::loadOverworldBlockedSfx(const std::string& path) {
    return impl_ != nullptr && impl_->loadOverworldBlockedSfx(path);
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

void AudioController::playSaveSfx() {
    if (impl_ != nullptr) {
        impl_->playSaveSfx();
    }
}

void AudioController::playOverworldBlockedSfx() {
    if (impl_ != nullptr) {
        impl_->playOverworldBlockedSfx();
    }
}

void AudioController::playOneShotSfx(const std::string& path) {
    if (impl_ != nullptr) {
        impl_->playOneShotSfx(path);
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
bool AudioController::loadSaveSfx(const std::string&) { return false; }
bool AudioController::loadOverworldBlockedSfx(const std::string&) { return false; }
void AudioController::playMusicLoop() {}
void AudioController::playButtonSfx() {}
void AudioController::playRipSfx() {}
void AudioController::playUiMoveSfx() {}
void AudioController::playPickupSfx() {}
void AudioController::playPutdownSfx() {}
void AudioController::playErrorSfx() {}
void AudioController::playSaveSfx() {}
void AudioController::playOverworldBlockedSfx() {}
void AudioController::playOneShotSfx(const std::string&) {}
void AudioController::stopMusic() {}
void AudioController::setMusicVolume(float) {}
void AudioController::setSfxVolume(float) {}
bool AudioController::isMusicLoaded() const { return false; }
bool AudioController::isMusicPlaying() const { return false; }

} // namespace pr

#endif
