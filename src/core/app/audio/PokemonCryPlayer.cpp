#include "core/app/audio/PokemonCryPlayer.hpp"

#include "core/app/frame/AppFrameRequests.hpp"

#include <utility>

namespace pr {

PokemonCryPlayer::PokemonCryPlayer(std::string project_root)
    : assets_(std::move(project_root)) {}

std::string PokemonCryPlayer::cryPathForSpeciesId(int species_id) const {
    return assets_.resolveBySpeciesId(species_id).relative_path;
}

bool PokemonCryPlayer::requestCryForSpeciesId(AppFrameRequests& frame_requests, int species_id) const {
    const std::string path = cryPathForSpeciesId(species_id);
    if (path.empty()) {
        return false;
    }
    frame_requests.requestOneShotSfx(path);
    return true;
}

int PokemonCryPlayer::speciesIdFromPmModelStem(const std::string& stem) {
    return PokemonCryAssets::speciesIdFromPmModelStem(stem);
}

} // namespace pr
