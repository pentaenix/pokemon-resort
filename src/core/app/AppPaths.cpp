#include "core/app/AppPaths.hpp"

#include <SDL.h>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace pr {

std::string findProjectRoot() {
    // Support running the binary from the repo root where cwd is title_screen_demo/
    // but config lives in title_screen_demo/pokemon-resort/config/.
    std::vector<fs::path> candidates{
        fs::current_path(),
        fs::current_path() / "pokemon-resort",
        fs::current_path().parent_path(),
        fs::current_path().parent_path() / "pokemon-resort",
        fs::current_path().parent_path().parent_path(),
    };
    for (const auto& candidate : candidates) {
        if (fs::exists(candidate / "config" / "app.json") &&
            fs::exists(candidate / "config" / "title_screen.json")) {
            std::error_code ec;
            const fs::path canon = fs::weakly_canonical(candidate, ec);
            return ec ? candidate.string() : canon.string();
        }
    }
    throw std::runtime_error("Could not locate project root with config/app.json and config/title_screen.json");
}

fs::path resolveSaveDirectory(
    const PersistenceConfig& persistence,
    const std::string& project_root) {
    char* pref_path = SDL_GetPrefPath(
        persistence.organization.c_str(),
        persistence.application.c_str());
    if (pref_path) {
        fs::path directory(pref_path);
        SDL_free(pref_path);
        return directory;
    }

    return fs::path(project_root) / "save";
}

} // namespace pr
