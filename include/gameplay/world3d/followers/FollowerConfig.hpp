#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"

#include <string>

namespace pr::gameplay::world3d::followers {

struct FollowerEntryAnimationConfig {
    int duration_ms = 90;
    float start_scale = 0.55f;
    float tint_r = 1.0f;
    float tint_g = 1.0f;
    float tint_b = 1.0f;
    float alpha = 1.0f;
};

struct FollowerBallAnimationConfig {
    int duration_ms = 120;
    int hold_last_frame_ms = 10;
    bool full_animation = true;
    bool fall_enabled = true;
    float fall_height_world = 10.0f;
};

struct FollowerSummonConfig {
    FollowerBallAnimationConfig ball_animation{};
    FollowerEntryAnimationConfig entry_animation{};
    int follow_step_duration_ms = 250;
};

struct FollowerSessionConfig {
    bool enabled = true;
    std::string pokemon_species;
    std::string pokeball_id = "poke_ball";
    std::string nature;
    std::string forced_behavior;
};

FollowerSummonConfig loadFollowerSummonConfig(const std::string& project_root);
FollowerSessionConfig loadFollowerSessionConfig(const std::string& project_root);
std::string resolveFollowerPokemonCharbinPath(const std::string& project_root, const std::string& pokemon_species);
std::string resolveFollowerPokeballCharbinPath(const std::string& project_root, const std::string& pokeball_id);

} // namespace pr::gameplay::world3d::followers
