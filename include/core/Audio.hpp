#pragma once

#include <string>

namespace pr {

class AudioController {
public:
    AudioController();
    ~AudioController();

    AudioController(const AudioController&) = delete;
    AudioController& operator=(const AudioController&) = delete;
    AudioController(AudioController&&) noexcept;
    AudioController& operator=(AudioController&&) noexcept;

    bool loadMusic(const std::string& path);
    bool loadButtonSfx(const std::string& path);
    bool loadRipSfx(const std::string& path);
    bool loadUiMoveSfx(const std::string& path);
    bool loadPickupSfx(const std::string& path);
    bool loadPutdownSfx(const std::string& path);
    void playMusicLoop();
    void playButtonSfx();
    void playRipSfx();
    void playUiMoveSfx();
    void playPickupSfx();
    void playPutdownSfx();
    void stopMusic();
    void setMusicVolume(float volume_01);
    void setSfxVolume(float volume_01);
    bool isMusicLoaded() const;
    bool isMusicPlaying() const;

private:
    class Impl;
    Impl* impl_ = nullptr;
};

} // namespace pr
