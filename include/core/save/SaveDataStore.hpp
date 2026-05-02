#pragma once

#include "core/Types.hpp"

#include <optional>
#include <string>

namespace pr {

std::optional<SaveData> loadSaveData(
    const std::string& primary_path,
    const std::string& backup_path,
    std::string* loaded_from_path = nullptr,
    std::string* error_message = nullptr);

bool saveSaveDataAtomic(
    const std::string& primary_path,
    const std::string& backup_path,
    const SaveData& save_data,
    std::string* error_message = nullptr);

} // namespace pr
