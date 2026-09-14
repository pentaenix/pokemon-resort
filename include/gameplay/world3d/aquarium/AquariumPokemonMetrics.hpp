#pragma once

#include <string>
#include <vector>

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

// Samples complete configured animation loops and returns their combined
// visible, skinned bounds. This is an offline catalogue-baking path; gameplay
// consumes the baked result and never performs this work when opening the UI.
AquariumPokemonMetrics measureAquariumPokemonAnimationEnvelope(
    const std::string& model_path,
    const std::string& form,
    const std::vector<std::string>& animation_names,
    int samples_per_animation,
    int* sampled_poses = nullptr,
    std::string* error = nullptr);

AquariumPokemonMetrics rotateAquariumPokemonMetrics(
    const AquariumPokemonMetrics& metrics,
    float pitch_degrees);

AquariumPokemonMetrics orientAquariumPokemonMetrics(
    const AquariumPokemonMetrics& metrics,
    float pitch_degrees,
    float yaw_degrees);

} // namespace pr::gameplay::world3d::aquarium
