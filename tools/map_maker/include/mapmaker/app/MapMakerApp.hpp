#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace pr::mapmaker {

struct MapMakerOptions {
    std::filesystem::path resort_root;
    std::optional<std::filesystem::path> project_path;
    std::optional<std::string> initial_map_id;
    std::string renderer;
    bool validate_project = false;
    bool smoke_test = false;
};

int runMapMaker(const MapMakerOptions& options);

} // namespace pr::mapmaker
