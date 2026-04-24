#include "ui/title_screen/OptionsMenuController.hpp"

#include <algorithm>
#include <array>

namespace pr::title_screen {

namespace {

constexpr int kTextSpeedOptionIndex = 0;
constexpr int kMusicVolumeOptionIndex = 1;
constexpr int kSfxVolumeOptionIndex = 2;
constexpr int kBackOptionIndex = 3;
constexpr int kOptionCount = 4;

} // namespace

bool OptionsMenuController::navigate(int delta) {
    const int next = wrapIndex(selected_index_ + delta, kOptionCount);
    if (next == selected_index_) {
        return false;
    }
    selected_index_ = next;
    return true;
}

bool OptionsMenuController::selectIndex(int index) {
    if (index < 0 || index >= kOptionCount) {
        return false;
    }
    if (selected_index_ == index) {
        return false;
    }
    selected_index_ = index;
    return true;
}

void OptionsMenuController::resetSelection() {
    selected_index_ = 0;
}

OptionsMenuAction OptionsMenuController::activate() {
    switch (selected_index_) {
        case kTextSpeedOptionIndex:
            text_speed_index_ = wrapIndex(text_speed_index_ + 1, 3);
            return OptionsMenuAction::ChangedSettings;
        case kMusicVolumeOptionIndex:
            music_volume_ = wrapIndex(music_volume_ + 1, 11);
            return OptionsMenuAction::ChangedSettings;
        case kSfxVolumeOptionIndex:
            sfx_volume_ = wrapIndex(sfx_volume_ + 1, 11);
            return OptionsMenuAction::ChangedSettings;
        case kBackOptionIndex:
            return OptionsMenuAction::CloseOptions;
        default:
            return OptionsMenuAction::None;
    }
}

float OptionsMenuController::musicVolumeScale() const {
    return static_cast<float>(music_volume_) / 10.0f;
}

float OptionsMenuController::sfxVolumeScale() const {
    return static_cast<float>(sfx_volume_) / 10.0f;
}

UserSettings OptionsMenuController::currentUserSettings() const {
    UserSettings settings;
    settings.text_speed_index = wrapIndex(text_speed_index_, 3);
    settings.music_volume = clampVolume(music_volume_);
    settings.sfx_volume = clampVolume(sfx_volume_);
    return settings;
}

void OptionsMenuController::applyUserSettings(const UserSettings& settings) {
    text_speed_index_ = wrapIndex(settings.text_speed_index, 3);
    music_volume_ = clampVolume(settings.music_volume);
    sfx_volume_ = clampVolume(settings.sfx_volume);
}

std::vector<std::string> OptionsMenuController::labels() const {
    static const std::array<const char*, 3> kTextSpeeds{"SLOW", "MID", "FAST"};

    return {
        "TEXT SPEED: " + std::string(kTextSpeeds[wrapIndex(text_speed_index_, static_cast<int>(kTextSpeeds.size()))]),
        "MUSIC VOLUME: " + std::to_string(music_volume_),
        "SFX VOLUME: " + std::to_string(sfx_volume_),
        "BACK"
    };
}

int OptionsMenuController::itemCount() {
    return kOptionCount;
}

int OptionsMenuController::wrapIndex(int value, int size) {
    if (size <= 0) {
        return 0;
    }
    value %= size;
    return value < 0 ? value + size : value;
}

int OptionsMenuController::clampVolume(int value) {
    return std::max(0, std::min(10, value));
}

} // namespace pr::title_screen
