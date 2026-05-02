#include "core/save/SaveDataStore.hpp"
#include "core/config/Json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace fs = std::filesystem;

namespace pr {

namespace {

const JsonValue* child(const JsonValue& parent, const std::string& key) { return parent.get(key); }
int asInt(const JsonValue& value) { return static_cast<int>(value.asNumber()); }

int clampValue(int value, int min_value, int max_value) {
    return std::max(min_value, std::min(max_value, value));
}

void applyFieldIfPresent(
    UserSettings& settings,
    const JsonValue& object,
    const char* field_name,
    int min_value,
    int max_value,
    int UserSettings::*field) {
    if (const JsonValue* value = child(object, field_name)) {
        settings.*field = clampValue(asInt(*value), min_value, max_value);
    }
}

void applyOptionsFromRoot(UserSettings& options, const JsonValue& root) {
    if (const JsonValue* options_node = child(root, "options")) {
        if (!options_node->isObject()) {
            throw std::runtime_error("Save data options must be an object");
        }

        applyFieldIfPresent(options, *options_node, "text_speed_index", 0, 2, &UserSettings::text_speed_index);
        applyFieldIfPresent(options, *options_node, "music_volume", 0, 10, &UserSettings::music_volume);
        applyFieldIfPresent(options, *options_node, "sfx_volume", 0, 10, &UserSettings::sfx_volume);
        return;
    }

    applyFieldIfPresent(options, root, "text_speed_index", 0, 2, &UserSettings::text_speed_index);
    applyFieldIfPresent(options, root, "music_volume", 0, 10, &UserSettings::music_volume);
    applyFieldIfPresent(options, root, "sfx_volume", 0, 10, &UserSettings::sfx_volume);
}

SaveData parseSaveDataFile(const std::string& path) {
    JsonValue root = parseJsonFile(path);
    if (!root.isObject()) {
        throw std::runtime_error("Save data root must be an object");
    }

    SaveData save_data;
    if (const JsonValue* version = child(root, "version")) {
        save_data.version = std::max(1, asInt(*version));
    }

    applyOptionsFromRoot(save_data.options, root);
    return save_data;
}

std::string serializeSaveData(const SaveData& save_data) {
    std::ostringstream out;
    out << "{\n"
        << "  \"version\": " << std::max(1, save_data.version) << ",\n"
        << "  \"options\": {\n"
        << "    \"text_speed_index\": " << save_data.options.text_speed_index << ",\n"
        << "    \"music_volume\": " << save_data.options.music_volume << ",\n"
        << "    \"sfx_volume\": " << save_data.options.sfx_volume << "\n"
        << "  },\n"
        << "  \"game\": {}\n"
        << "}\n";
    return out.str();
}

bool fileExists(const fs::path& path) {
    std::error_code error;
    return fs::exists(path, error);
}

bool copyFileReplacing(const fs::path& from, const fs::path& to) {
    std::error_code remove_error;
    fs::remove(to, remove_error);

    std::error_code copy_error;
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, copy_error);
    return !copy_error;
}

} // namespace

std::optional<SaveData> loadSaveData(
    const std::string& primary_path,
    const std::string& backup_path,
    std::string* loaded_from_path,
    std::string* error_message) {
    if (loaded_from_path) {
        loaded_from_path->clear();
    }
    if (error_message) {
        error_message->clear();
    }

    const std::pair<std::string, const char*> candidates[] = {
        {primary_path, "primary"},
        {backup_path, "backup"}
    };

    std::string combined_error;
    for (const auto& candidate : candidates) {
        if (candidate.first.empty()) {
            continue;
        }
        if (!fileExists(candidate.first)) {
            continue;
        }

        try {
            SaveData save_data = parseSaveDataFile(candidate.first);
            if (loaded_from_path) {
                *loaded_from_path = candidate.first;
            }
            return save_data;
        } catch (const std::exception& e) {
            if (!combined_error.empty()) {
                combined_error += " | ";
            }
            combined_error += std::string(candidate.second) + ": " + e.what();
        }
    }

    if (error_message) {
        *error_message = combined_error;
    }
    return std::nullopt;
}

bool saveSaveDataAtomic(
    const std::string& primary_path,
    const std::string& backup_path,
    const SaveData& save_data,
    std::string* error_message) {
    if (error_message) {
        error_message->clear();
    }

    try {
        const fs::path primary(primary_path);
        const fs::path backup(backup_path);
        const fs::path directory = primary.parent_path();
        if (!directory.empty()) {
            fs::create_directories(directory);
        }

        const fs::path temp = primary.string() + ".tmp";
        {
            std::ofstream output(temp, std::ios::trunc);
            if (!output) {
                throw std::runtime_error("Could not open temporary save file for writing");
            }

            output << serializeSaveData(save_data);
            output.flush();
            if (!output) {
                throw std::runtime_error("Failed while writing save data");
            }
        }

        if (fileExists(primary) && !backup.empty() && !copyFileReplacing(primary, backup)) {
            throw std::runtime_error("Could not refresh save backup");
        }

        std::error_code remove_error;
        fs::remove(primary, remove_error);

        std::error_code rename_error;
        fs::rename(temp, primary, rename_error);
        if (rename_error) {
            std::error_code cleanup_error;
            fs::remove(temp, cleanup_error);
            throw std::runtime_error("Could not replace primary save file: " + rename_error.message());
        }

        if (!backup.empty() && !copyFileReplacing(primary, backup)) {
            throw std::runtime_error("Primary save updated, but backup refresh failed");
        }

        return true;
    } catch (const std::exception& e) {
        if (error_message) {
            *error_message = e.what();
        }
        return false;
    }
}

} // namespace pr
