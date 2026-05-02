#include "core/app/persistence/UserSettingsPersistence.hpp"

#include "core/save/SaveDataStore.hpp"

#include <iostream>
#include <string>
#include <utility>

namespace pr {

UserSettingsPersistence::UserSettingsPersistence(
    bool enabled,
    std::filesystem::path save_file_path,
    std::filesystem::path backup_file_path)
    : enabled_(enabled),
      save_file_path_(std::move(save_file_path)),
      backup_file_path_(std::move(backup_file_path)) {}

bool UserSettingsPersistence::load(UserSettings& settings) const {
    if (!enabled_) {
        return false;
    }

    std::string load_error;
    std::string loaded_from_path;
    if (auto save_data = loadSaveData(
            save_file_path_.string(),
            backup_file_path_.string(),
            &loaded_from_path,
            &load_error)) {
        settings = save_data->options;
        if (!loaded_from_path.empty() && loaded_from_path != save_file_path_.string()) {
            std::cerr << "Warning: primary save unavailable; loaded fallback save from "
                      << loaded_from_path << '\n';
        }
        return true;
    }

    if (!load_error.empty()) {
        std::cerr << "Warning: could not load save data from "
                  << save_file_path_ << " or backup " << backup_file_path_
                  << ": " << load_error << '\n';
    }
    return false;
}

void UserSettingsPersistence::save(const UserSettings& settings) const {
    if (!enabled_) {
        return;
    }

    SaveData save_data;
    save_data.options = settings;
    std::string save_error;
    if (!saveSaveDataAtomic(
            save_file_path_.string(),
            backup_file_path_.string(),
            save_data,
            &save_error)) {
        std::cerr << "Warning: could not save data to " << save_file_path_
                  << " with backup " << backup_file_path_
                  << ": " << save_error << '\n';
    }
}

} // namespace pr
