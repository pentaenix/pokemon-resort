#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace pr::gameplay::world3d::followers {

struct NatureIdleRangeSeconds {
    double min = 0.0;
    double max = 0.0;
};

struct NatureIdleClamp {
    int min = 0;
    int max = 10;
};

struct NatureIdleJumpConfig {
    int height_pixels = 10;
    double duration_seconds = 0.35;
};

struct NatureIdlePokeConfig {
    double distance_tiles = 0.18;
    double forward_seconds = 0.15;
    double return_seconds = 0.15;
};

struct NatureIdleLandingDustConfig {
    bool enabled = true;
    std::string texture_path = "assets/effects/dust.png";
    int frame_width = 32;
    int frame_height = 32;
    int frame_count = 3;
    float sprite_scale = 4.8f;
    int screen_offset_y_px = 0;
};

struct NatureIdleBehaviorConfig {
    bool enabled = true;
    std::string nature_source = "pokemon.temporal.nature";
    bool disable_if_nature_missing_or_invalid = true;
    double start_after_idle_seconds = 5.0;
    NatureIdleRangeSeconds behavior_duration_seconds{4.0, 9.0};
    NatureIdleRangeSeconds cooldown_between_behaviors_seconds{2.5, 6.0};
    double movement_speed_multiplier = 1.0;
    double return_to_origin_speed_multiplier = 1.35;
    double cancel_return_speed_multiplier = 2.25;
    int max_player_radius = 5;
    int max_npc_follow_radius = 10;
    bool restore_original_position_on_natural_end = true;
    bool restore_original_direction_on_natural_end = true;
    bool allow_soft_snap_on_cancel_return = true;
    double soft_snap_delay_seconds = 0.35;
    NatureIdleClamp behavior_weight_clamp{0, 10};
    double quirky_random_behavior_chance = 0.2;
    NatureIdleJumpConfig jump{};
    NatureIdlePokeConfig poke{};
    NatureIdleLandingDustConfig landing_dust{};
    std::vector<std::string> fallback_order{
        "watch_player",
        "face_away",
        "random_walk",
        "do_nothing"};
    std::unordered_map<std::string, std::unordered_map<std::string, int>> group_weights;
    std::unordered_map<std::string, std::unordered_map<std::string, int>> nature_modifiers;
};

NatureIdleBehaviorConfig loadNatureIdleBehaviorConfig(const std::string& project_root);

} // namespace pr::gameplay::world3d::followers
