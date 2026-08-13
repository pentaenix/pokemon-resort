#pragma once

#include "mapmaker/document/OwmapDocument.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace pr::mapmaker {

class AutosaveRecovery {
public:
    explicit AutosaveRecovery(
        std::filesystem::path directory,
        std::chrono::seconds interval = std::chrono::seconds(30));

    bool writeIfDue(
        const std::string& map_id,
        const OwmapDocument& document,
        bool dirty,
        std::string* error = nullptr);
    bool writeNow(
        const std::string& map_id,
        const OwmapDocument& document,
        std::string* error = nullptr);
    bool remove(const std::string& map_id, std::string* error = nullptr);

    std::filesystem::path pathFor(const std::string& map_id) const;
    std::filesystem::path directory() const { return directory_; }

private:
    std::filesystem::path directory_;
    std::chrono::seconds interval_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> next_write_by_map_;
};

} // namespace pr::mapmaker
