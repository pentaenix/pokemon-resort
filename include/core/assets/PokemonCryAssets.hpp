#pragma once

#include <string>

namespace pr {

struct ResolvedPokemonCry {
    int species_id = -1;
    std::string relative_path;
    bool found = false;
};

class PokemonCryAssets {
public:
    explicit PokemonCryAssets(std::string project_root);

    ResolvedPokemonCry resolveBySpeciesId(int species_id) const;

    static int speciesIdFromPmModelStem(const std::string& stem);

private:
    std::string project_root_;
};

} // namespace pr
