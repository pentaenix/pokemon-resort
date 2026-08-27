#pragma once

#include <string>

namespace pr::gameplay::world3d::aquarium {

// Bounds are in the Attend model's authored units, before aquarium modelScale.
// They are deliberately pose-independent so navigation remains deterministic.
struct AquariumPokemonMetrics {
    float min_x = 0.0f;
    float max_x = 0.0f;
    float min_y = 0.0f;
    float max_y = 0.0f;
    float min_z = 0.0f;
    float max_z = 0.0f;
    bool valid = false;
};

AquariumPokemonMetrics measureAquariumPokemon(
    const std::string& model_path,
    const std::string& form = {},
    std::string* error = nullptr);

} // namespace pr::gameplay::world3d::aquarium
