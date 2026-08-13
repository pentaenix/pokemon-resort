#include "mapmaker/app/AutosaveRecovery.hpp"

#include <cctype>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace pr::mapmaker {
namespace {

std::string safeName(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (unsigned char character : value) {
        result.push_back(std::isalnum(character) || character == '-' || character == '_'
            ? static_cast<char>(character) : '_');
    }
    return result.empty() ? "map" : result;
}

} // namespace

AutosaveRecovery::AutosaveRecovery(
    std::filesystem::path directory,
    std::chrono::seconds interval)
    : directory_(std::move(directory)), interval_(interval) {}

bool AutosaveRecovery::writeIfDue(
    const std::string& map_id,
    const OwmapDocument& document,
    bool dirty,
    std::string* error) {
    if (!dirty) return false;
    const auto now = std::chrono::steady_clock::now();
    const auto found = next_write_by_map_.try_emplace(map_id, now + interval_).first;
    if (now < found->second) return false;
    return writeNow(map_id, document, error);
}

bool AutosaveRecovery::writeNow(
    const std::string& map_id,
    const OwmapDocument& document,
    std::string* error) {
    try {
        std::filesystem::create_directories(directory_);
        const std::filesystem::path destination = pathFor(map_id);
        const std::filesystem::path temporary = destination.string() + ".tmp";
        const std::vector<std::uint8_t> bytes = document.serialize();
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) throw std::runtime_error("Could not open autosave temporary file");
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        stream.close();
        (void)OwmapDocument::load(temporary);
        std::error_code remove_error;
        std::filesystem::remove(destination, remove_error);
        std::filesystem::rename(temporary, destination);
        next_write_by_map_[map_id] = std::chrono::steady_clock::now() + interval_;
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return false;
    }
}

bool AutosaveRecovery::remove(const std::string& map_id, std::string* error) {
    std::error_code code;
    const bool removed = std::filesystem::remove(pathFor(map_id), code);
    next_write_by_map_.erase(map_id);
    if (code && error) *error = code.message();
    return removed && !code;
}

std::filesystem::path AutosaveRecovery::pathFor(const std::string& map_id) const {
    return directory_ / (safeName(map_id) + ".recovery.owmap");
}

} // namespace pr::mapmaker
