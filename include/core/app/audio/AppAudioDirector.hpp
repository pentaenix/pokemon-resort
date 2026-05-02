#pragma once

#include "core/audio/Audio.hpp"
#include "core/Types.hpp"

#include <filesystem>
#include <string>

namespace pr {

struct AppMusicRequest {
    bool menu_requested = false;
    bool transfer_requested = false;
    std::string transfer_music_path;
    double transfer_silence_seconds = 0.0;
    double transfer_fade_in_seconds = 0.0;
    float volume = 1.0f;
};

struct AppSfxRequests {
    bool button = false;
    bool rip = false;
    bool ui_move = false;
    bool pickup = false;
    bool putdown = false;
    bool error = false;
    bool save = false;
};

class AppAudioDirector {
public:
    AppAudioDirector(std::string project_root, const AudioConfig& config);

    void updateMusic(double dt, const AppMusicRequest& request);
    void playSfx(const AppSfxRequests& requests, float sfx_volume);

private:
    enum class ActiveMusicTrack {
        None,
        Menu,
        Transfer
    };

    bool loadMenuMusic();
    bool loadTransferMusic(const std::string& relative_path);

    AudioController audio_;
    std::filesystem::path project_root_;
    AudioConfig config_;
    ActiveMusicTrack loaded_music_track_ = ActiveMusicTrack::None;
    bool music_playing_ = false;
    double transfer_music_elapsed_seconds_ = 0.0;
};

} // namespace pr
