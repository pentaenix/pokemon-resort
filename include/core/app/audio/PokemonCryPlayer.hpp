#pragma once

#include "core/assets/PokemonCryAssets.hpp"

#include <string>

namespace pr {

class AppFrameRequests;

class PokemonCryPlayer {
public:
    explicit PokemonCryPlayer(std::string project_root);

    std::string cryPathForSpeciesId(int species_id) const;
    bool requestCryForSpeciesId(AppFrameRequests& frame_requests, int species_id) const;
    static int speciesIdFromPmModelStem(const std::string& stem);

private:
    PokemonCryAssets assets_;
};

} // namespace pr
