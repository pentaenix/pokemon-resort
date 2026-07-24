#include "core/assets/PokemonCryAssets.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <utility>

namespace pr {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

} // namespace

PokemonCryAssets::PokemonCryAssets(std::string project_root)
    : project_root_(std::move(project_root)) {}

ResolvedPokemonCry PokemonCryAssets::resolveBySpeciesId(int species_id) const {
    if (species_id <= 0) {
        return ResolvedPokemonCry{species_id, {}, false};
    }
    const fs::path relative_path =
        fs::path("assets") / "pokemon" / "cries" / (std::to_string(species_id) + ".ogg");
    const fs::path absolute_path = fs::path(project_root_) / relative_path;
    std::error_code ec;
    const bool found = std::filesystem::exists(absolute_path, ec);
    return ResolvedPokemonCry{species_id, found ? relative_path.string() : std::string{}, found};
}

int PokemonCryAssets::speciesIdFromPmModelStem(const std::string& stem) {
    const std::string lowered = lower(stem);
    if (lowered.size() < 6 || lowered.rfind("pm", 0) != 0) return 0;
    for (std::size_t i = 2; i < 6; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(lowered[i]))) return 0;
    }
    return std::stoi(lowered.substr(2, 4));
}

} // namespace pr
