#pragma once

#include <string>

namespace pr::gameplay::world3d::npc {

// Temporary overworld roster source until map spawn points and companion selection exist.
struct ResortPokemonSpawnConfig {
    bool enabled = true;
    std::string profile_id = "default";
    int box_id = 0;
    int max_pokemon = 8;
};

ResortPokemonSpawnConfig loadResortPokemonSpawnConfig(const std::string& project_root);

} // namespace pr::gameplay::world3d::npc
