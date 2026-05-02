#pragma once

#include "core/Types.hpp"

#include <filesystem>

namespace pr {

class UserSettingsPersistence {
public:
    UserSettingsPersistence(
        bool enabled,
        std::filesystem::path save_file_path,
        std::filesystem::path backup_file_path);

    bool load(UserSettings& settings) const;
    void save(const UserSettings& settings) const;

private:
    bool enabled_ = false;
    std::filesystem::path save_file_path_;
    std::filesystem::path backup_file_path_;
};

} // namespace pr
