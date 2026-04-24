#pragma once

#include "core/Types.hpp"

#include <string>
#include <vector>

namespace pr::title_screen {

enum class OptionsMenuAction {
    None,
    ChangedSettings,
    CloseOptions
};

class OptionsMenuController {
public:
    int selectedIndex() const { return selected_index_; }
    bool navigate(int delta);
    bool selectIndex(int index);
    void resetSelection();

    OptionsMenuAction activate();

    float musicVolumeScale() const;
    float sfxVolumeScale() const;
    UserSettings currentUserSettings() const;
    void applyUserSettings(const UserSettings& settings);
    std::vector<std::string> labels() const;

    static int itemCount();

private:
    static int wrapIndex(int value, int size);
    static int clampVolume(int value);

    int selected_index_ = 0;
    int text_speed_index_ = 2;
    int music_volume_ = 7;
    int sfx_volume_ = 8;
};

} // namespace pr::title_screen
