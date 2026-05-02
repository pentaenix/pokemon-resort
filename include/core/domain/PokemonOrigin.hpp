#pragma once

#include "core/domain/PcSlotSpecies.hpp"

#include <string>

namespace pr {

struct PokemonOriginInfo {
    std::string game_id;
    std::string region_key = "unknown";
    std::string game_code;
};

std::string normalizePokemonGameId(const std::string& raw_game, const std::string& filename_hint = {});
std::string pokemonGameTitle(const std::string& game_id, const std::string& filename_hint = {});
std::string pokemonGameCode(const std::string& game_id);
std::string pokemonGameRegionKey(const std::string& game_id);
PokemonOriginInfo resolvePokemonOrigin(const PcSlotSpecies& slot, const std::string& source_game_key);

} // namespace pr
