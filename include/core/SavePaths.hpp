#pragma once

#include "core/Types.hpp"

#include <filesystem>
#include <string>

namespace pr {

/// Resort SQLite file in the same directory as the options save (see `title_screen.json` `persistence`). Empty `file_name` uses `profile.resort.db`.
inline std::filesystem::path resortProfileDatabasePath(
    const std::filesystem::path& app_save_directory,
    const std::string& file_name) {
    const std::string use_name = file_name.empty() ? std::string("profile.resort.db") : file_name;
    return app_save_directory / use_name;
}

inline std::filesystem::path resortProfileDatabasePath(
    const std::filesystem::path& app_save_directory,
    const PersistenceConfig& persistence) {
    return resortProfileDatabasePath(app_save_directory, persistence.resort_profile_file_name);
}

inline std::filesystem::path defaultResortProfilePath(const std::filesystem::path& app_save_directory) {
    return resortProfileDatabasePath(app_save_directory, "profile.resort.db");
}

} // namespace pr
