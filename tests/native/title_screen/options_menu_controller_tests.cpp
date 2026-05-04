#include "ui/title_screen/OptionsMenuController.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

} // namespace

int main() {
    using pr::UserSettings;
    using pr::title_screen::OptionsMenuAction;
    using pr::title_screen::OptionsMenuController;

    OptionsMenuController options;
    expect(options.selectedIndex() == 0, "default selection starts on text speed");
    expect(options.labels()[0] == "TEXT SPEED: FAST", "default text speed label is FAST");

    expect(options.activate() == OptionsMenuAction::ChangedSettings, "text speed activation changes settings");
    expect(options.currentUserSettings().text_speed_index == 0, "text speed wraps from FAST to SLOW");
    expect(options.labels()[0] == "TEXT SPEED: SLOW", "text speed label updates after cycling");

    expect(options.navigate(1), "navigate to music volume");
    expect(options.selectedIndex() == 1, "music volume row selected");
    expect(options.activate() == OptionsMenuAction::ChangedSettings, "music activation changes settings");
    expect(options.currentUserSettings().music_volume == 8, "music volume increments");
    expect(options.musicVolumeScale() == 0.8f, "music volume scale follows stored volume");

    expect(options.navigate(1), "navigate to sfx volume");
    expect(options.activate() == OptionsMenuAction::ChangedSettings, "sfx activation changes settings");
    expect(options.currentUserSettings().sfx_volume == 9, "sfx volume increments");
    expect(options.sfxVolumeScale() == 0.9f, "sfx volume scale follows stored volume");

    expect(options.navigate(1), "navigate to back");
    expect(options.activate() == OptionsMenuAction::CloseOptions, "back row closes options");

    expect(options.navigate(1), "navigation wraps from back to first row");
    expect(options.selectedIndex() == 0, "wrapped selection lands on text speed");
    expect(options.navigate(-1), "navigation wraps upward from first row");
    expect(options.selectedIndex() == 3, "upward wrap lands on back");

    expect(!options.selectIndex(99), "out-of-range option selection is rejected");
    expect(options.selectedIndex() == 3, "rejected option selection keeps previous index");

    UserSettings settings;
    settings.text_speed_index = -1;
    settings.music_volume = 42;
    settings.sfx_volume = -20;
    options.applyUserSettings(settings);
    const UserSettings clamped = options.currentUserSettings();
    expect(clamped.text_speed_index == 2, "negative text speed wraps to FAST");
    expect(clamped.music_volume == 10, "music volume clamps high");
    expect(clamped.sfx_volume == 0, "sfx volume clamps low");

    options.resetSelection();
    expect(options.selectedIndex() == 0, "reset selection returns to first row");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
