#include "core/app/audio/AppAudioDirector.hpp"

#include <algorithm>
#include <iostream>
#include <utility>

namespace pr {
namespace {

double clamp01(double value) {
    return std::max(0.0, std::min(1.0, value));
}

} // namespace

AppAudioDirector::AppAudioDirector(std::string project_root, const AudioConfig& config)
    : project_root_(std::move(project_root)),
      config_(config) {
    loadMenuMusic();

    const auto load_sfx = [this](const std::string& relative_path, const char* label, auto load) {
        const std::filesystem::path path = project_root_ / relative_path;
        if (!(audio_.*load)(path.string())) {
            std::cerr << "Warning: could not load " << label << " at " << path << '\n';
        }
    };

    load_sfx(config_.button_sfx, "button sfx", &AudioController::loadButtonSfx);
    load_sfx(config_.rip_sfx, "rip sfx", &AudioController::loadRipSfx);
    load_sfx(config_.ui_move_sfx, "ui move sfx", &AudioController::loadUiMoveSfx);
    load_sfx(config_.pickup_sfx, "pickup sfx", &AudioController::loadPickupSfx);
    load_sfx(config_.putdown_sfx, "putdown sfx", &AudioController::loadPutdownSfx);
    load_sfx(config_.error_sfx, "error sfx", &AudioController::loadErrorSfx);
    load_sfx(config_.save_sfx, "save sfx", &AudioController::loadSaveSfx);
}

void AppAudioDirector::updateMusic(double dt, const AppMusicRequest& request) {
    const ActiveMusicTrack desired_music_track =
        request.menu_requested
            ? ActiveMusicTrack::Menu
            : (request.transfer_requested ? ActiveMusicTrack::Transfer : ActiveMusicTrack::None);

    if (desired_music_track == ActiveMusicTrack::Menu) {
        if (loaded_music_track_ != ActiveMusicTrack::Menu) {
            audio_.stopMusic();
            music_playing_ = false;
            loadMenuMusic();
        }
        transfer_music_elapsed_seconds_ = 0.0;
        if (loaded_music_track_ == ActiveMusicTrack::Menu && audio_.isMusicLoaded()) {
            audio_.setMusicVolume(request.volume);
            if (!music_playing_) {
                audio_.playMusicLoop();
                music_playing_ = true;
            }
        }
        return;
    }

    if (desired_music_track == ActiveMusicTrack::Transfer) {
        if (loaded_music_track_ != ActiveMusicTrack::Transfer) {
            audio_.stopMusic();
            music_playing_ = false;
            transfer_music_elapsed_seconds_ = 0.0;
            loadTransferMusic(request.transfer_music_path);
        }
        if (loaded_music_track_ == ActiveMusicTrack::Transfer && audio_.isMusicLoaded()) {
            transfer_music_elapsed_seconds_ += dt;
            const double silence_seconds = std::max(0.0, request.transfer_silence_seconds);
            const double fade_in_seconds = std::max(0.0, request.transfer_fade_in_seconds);
            double volume_scale = 0.0;
            if (transfer_music_elapsed_seconds_ >= silence_seconds) {
                volume_scale = fade_in_seconds <= 0.0
                    ? 1.0
                    : clamp01((transfer_music_elapsed_seconds_ - silence_seconds) / fade_in_seconds);
            }
            audio_.setMusicVolume(static_cast<float>(
                static_cast<double>(request.volume) * volume_scale));
            if (!music_playing_) {
                audio_.playMusicLoop();
                music_playing_ = true;
            }
        }
        return;
    }

    if (music_playing_) {
        audio_.stopMusic();
        music_playing_ = false;
        transfer_music_elapsed_seconds_ = 0.0;
    }
}

void AppAudioDirector::playSfx(const AppSfxRequests& requests, float sfx_volume) {
    audio_.setSfxVolume(sfx_volume);
    if (requests.button) {
        audio_.playButtonSfx();
    }
    if (requests.ui_move) {
        audio_.playUiMoveSfx();
    }
    if (requests.rip) {
        audio_.playRipSfx();
    }
    if (requests.pickup) {
        audio_.playPickupSfx();
    }
    if (requests.putdown) {
        audio_.playPutdownSfx();
    }
    if (requests.error) {
        audio_.playErrorSfx();
    }
    if (requests.save) {
        audio_.playSaveSfx();
    }
}

bool AppAudioDirector::loadMenuMusic() {
    const std::filesystem::path menu_music_path = project_root_ / config_.menu_music;
    if (audio_.loadMusic(menu_music_path.string())) {
        loaded_music_track_ = ActiveMusicTrack::Menu;
        return true;
    }

    loaded_music_track_ = ActiveMusicTrack::None;
    std::cerr << "Warning: could not load menu music at " << menu_music_path << '\n';
    return false;
}

bool AppAudioDirector::loadTransferMusic(const std::string& relative_path) {
    const std::filesystem::path transfer_music_path = project_root_ / relative_path;
    if (audio_.loadMusic(transfer_music_path.string())) {
        loaded_music_track_ = ActiveMusicTrack::Transfer;
        return true;
    }

    loaded_music_track_ = ActiveMusicTrack::None;
    std::cerr << "Warning: could not load transfer music at " << transfer_music_path << '\n';
    return false;
}

} // namespace pr
