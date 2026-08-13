#pragma once

#include <filesystem>
#include <optional>
#include <vector>

namespace pr::mapmaker {

struct ProjectDiscoveryResult {
    std::optional<std::filesystem::path> path;
    std::vector<std::filesystem::path> searched_paths;

    bool found() const { return path.has_value(); }
    bool usedFallback() const;
};

// The order mirrors the runtime: canonical game config, legacy asset-local
// project, then the old web-admin project during migration.
std::vector<std::filesystem::path> mapProjectDiscoveryCandidates(
    const std::filesystem::path& pokemon_resort_root);
ProjectDiscoveryResult discoverMapProject(
    const std::filesystem::path& pokemon_resort_root);

} // namespace pr::mapmaker
