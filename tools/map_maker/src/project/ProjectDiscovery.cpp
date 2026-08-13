#include "mapmaker/project/ProjectDiscovery.hpp"

#include <system_error>

namespace pr::mapmaker {

bool ProjectDiscoveryResult::usedFallback() const {
    return path.has_value() && !searched_paths.empty() && *path != searched_paths.front();
}

std::vector<std::filesystem::path> mapProjectDiscoveryCandidates(
    const std::filesystem::path& pokemon_resort_root) {
    const std::filesystem::path root = pokemon_resort_root.lexically_normal();
    return {
        root / "config" / "gameplay" / "world3d" / "map_project.json",
        root / "assets" / "overworld" / "maps" / "map_project.json",
        root.parent_path() / "pokemon-resort-page" / "tools" / "admin" / "data" /
            "map-projects" / "default.json",
    };
}

ProjectDiscoveryResult discoverMapProject(
    const std::filesystem::path& pokemon_resort_root) {
    ProjectDiscoveryResult result;
    result.searched_paths = mapProjectDiscoveryCandidates(pokemon_resort_root);
    for (const std::filesystem::path& candidate : result.searched_paths) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error) && !error) {
            result.path = candidate;
            break;
        }
    }
    return result;
}

} // namespace pr::mapmaker
