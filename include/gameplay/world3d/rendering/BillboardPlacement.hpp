#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"
#include "gameplay/world3d/terrain/ActorTerrainBinding.hpp"

#include <SDL.h>

#include <cstdint>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::rendering {

struct BillboardPlacement {
    camera::Vec3 anchor{};
    camera::Vec3 feet{};
    camera::Vec3 shadow_ground{};
    float world_w = 0.0f;
    float world_h = 0.0f;
    float depth = 0.0f;
    bool visible = false;
};

struct CharacterBillboardDraw {
    BillboardPlacement placement{};
    const CharacterSpriteDefinition* character = nullptr;
    SDL_Rect source_rect{};
    float sprite_scale_multiplier = 1.0f;
    float alpha_multiplier = 1.0f;
    float white_overlay_alpha = 0.0f;
    float tint_r = 1.0f;
    float tint_g = 1.0f;
    float tint_b = 1.0f;
    float depth_priority_bias = 0.0f;
    std::string activity_id;
    bool draw_shadow = true;
    bool use_run_texture = false;
};

struct TextureBillboardDraw {
    BillboardPlacement placement{};
    std::string texture_cache_key;
    std::vector<std::uint8_t> png_bytes;
    std::string fallback_path;
    SDL_Rect source_rect{};
};

float billboardSortDepth(
    const camera::Gen4FollowCamera& camera,
    const camera::Vec3& world_pos,
    int viewport_w,
    int viewport_h);

BillboardPlacement buildCharacterBillboardPlacement(
    const SceneConfig& scene,
    const camera::Gen4FollowCamera& camera,
    const terrain::ActorTerrainBinding& binding,
    const CharacterSpriteDefinition& character,
    const camera::Vec3& simulation_pos,
    const SDL_Rect& source_rect,
    int viewport_w,
    int viewport_h,
    float sprite_scale_multiplier = 1.0f,
    int screen_offset_y_px = 0,
    const camera::Vec3& world_offset = {});

BillboardPlacement buildTextureBillboardPlacement(
    const SceneConfig& scene,
    const camera::Gen4FollowCamera& camera,
    const terrain::ActorTerrainBinding& binding,
    const camera::Vec3& simulation_pos,
    const SDL_Rect& source_rect,
    int viewport_w,
    int viewport_h,
    float sprite_scale = 1.0f,
    int screen_offset_y_px = 0);

} // namespace pr::gameplay::world3d::rendering
