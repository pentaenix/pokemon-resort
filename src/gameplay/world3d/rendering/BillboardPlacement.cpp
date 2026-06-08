#include "gameplay/world3d/rendering/BillboardPlacement.hpp"

#include "gameplay/world3d/terrain/TerrainSurface.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pr::gameplay::world3d::rendering {

namespace {

constexpr float kSlopeBillboardLiftTiles = 0.125f;
constexpr float kCanonicalCharacterFramePx = 32.0f;
constexpr float kCanonicalCharacterTilesHigh = 1.75f;

float authoredSpriteWorldHeight(const SceneConfig& scene, float frame_height_px, float scale_multiplier) {
    const float frame_height = std::max(1.0f, frame_height_px);
    return std::max(1.0f, scene.grid.tile_size) *
        kCanonicalCharacterTilesHigh *
        (frame_height / kCanonicalCharacterFramePx) *
        std::max(0.1f, scale_multiplier);
}

int tileSpecial(const SceneConfig& scene, int tx, int ty) {
    if (scene.terrain.specials.empty()) return 0;
    if (ty < 0 || ty >= static_cast<int>(scene.terrain.specials.size())) return 0;
    const auto& row = scene.terrain.specials[static_cast<std::size_t>(ty)];
    if (tx < 0 || tx >= static_cast<int>(row.size())) return 0;
    return static_cast<int>(row[static_cast<std::size_t>(tx)]);
}

bool isSlopeSpecial(int special) {
    return special >= 2 && special <= 13;
}

float slopeBillboardLift(const SceneConfig& scene, const terrain::ActorTerrainBinding& binding) {
    if (!isSlopeSpecial(tileSpecial(scene, binding.height_sample_tx, binding.height_sample_ty))) {
        return 0.0f;
    }
    return std::max(1.0f, scene.grid.tile_size * kSlopeBillboardLiftTiles);
}

camera::Vec3 verticalOnlyScreenOffset(
    const camera::Gen4FollowCamera& camera,
    int offset_y_px,
    float depth,
    int viewport_h) {
    const camera::Vec3 camera_up_offset = camera.screenOffsetToWorldOffset(offset_y_px, depth, viewport_h);
    const auto pose = camera.pose();
    const float camera_up_distance =
        (camera_up_offset.x * pose.up.x) +
        (camera_up_offset.y * pose.up.y) +
        (camera_up_offset.z * pose.up.z);
    const float up_y = (std::abs(pose.up.y) < 0.0001f)
        ? (pose.up.y < 0.0f ? -0.0001f : 0.0001f)
        : pose.up.y;
    const float world_y = camera_up_distance / up_y;
    return camera::Vec3{0.0f, world_y, 0.0f};
}

} // namespace

float billboardSortDepth(
    const camera::Gen4FollowCamera& camera,
    const camera::Vec3& world_pos,
    int viewport_w,
    int viewport_h) {
    float sx = 0.0f;
    float sy = 0.0f;
    float depth = 0.0f;
    if (camera.worldToScreen(world_pos, viewport_w, viewport_h, sx, sy, depth)) {
        return depth;
    }
    return std::numeric_limits<float>::max();
}

BillboardPlacement buildCharacterBillboardPlacement(
    const SceneConfig& scene,
    const camera::Gen4FollowCamera& camera,
    const terrain::ActorTerrainBinding& binding,
    const CharacterSpriteDefinition& character,
    const camera::Vec3& simulation_pos,
    const SDL_Rect& source_rect,
    int viewport_w,
    int viewport_h,
    float sprite_scale_multiplier,
    int screen_offset_y_px,
    const camera::Vec3& world_offset) {
    BillboardPlacement out{};
    camera::Vec3 world_pos = simulation_pos;
    world_pos.x += world_offset.x;
    world_pos.y += world_offset.y;
    world_pos.z += world_offset.z;

    camera::Vec3 pos = world_pos;
    pos.x += character.world_offset_x;
    pos.z += character.world_offset_z;

    const float base_terrain_y = terrain::heightAtActorFeet(
        scene,
        world_pos.x,
        world_pos.z,
        binding.height_sample_tx,
        binding.height_sample_ty);
    const float foot_terrain_y = terrain::heightAtActorFeet(
        scene,
        pos.x,
        pos.z,
        binding.height_sample_tx,
        binding.height_sample_ty);
    const float render_lift_y = slopeBillboardLift(scene, binding);
    const float grounded_foot_y = foot_terrain_y + character.world_offset_y + (world_pos.y - base_terrain_y);
    pos.y = grounded_foot_y + render_lift_y;

    float sx = 0.0f;
    float sy = 0.0f;
    if (!camera.worldToScreen(pos, viewport_w, viewport_h, sx, sy, out.depth)) {
        return out;
    }

    const camera::Vec3 screen_offset =
        verticalOnlyScreenOffset(camera, character.screen_offset_y_px + screen_offset_y_px, out.depth, viewport_h);
    pos.x += screen_offset.x;
    pos.y += screen_offset.y;
    pos.z += screen_offset.z;

    const float frame_height = std::max(1.0f, static_cast<float>(source_rect.h));
    out.world_h = authoredSpriteWorldHeight(scene, frame_height, sprite_scale_multiplier);
    const float tex_aspect =
        static_cast<float>(source_rect.w) / std::max(1.0f, static_cast<float>(source_rect.h));
    out.world_w = out.world_h * tex_aspect;

    out.anchor = pos;
    const float anchor_y = (character.anchor == "center") ? (out.world_h * 0.5f) : 0.0f;
    out.feet = camera::Vec3{pos.x, pos.y - anchor_y, pos.z};
    out.shadow_ground = camera::Vec3{
        out.feet.x,
        grounded_foot_y + scene.sprite_shadow.world_y_lift,
        out.feet.z};
    out.visible = true;
    return out;
}

BillboardPlacement buildTextureBillboardPlacement(
    const SceneConfig& scene,
    const camera::Gen4FollowCamera& camera,
    const terrain::ActorTerrainBinding& binding,
    const camera::Vec3& simulation_pos,
    const SDL_Rect& source_rect,
    int viewport_w,
    int viewport_h,
    float sprite_scale,
    int screen_offset_y_px) {
    BillboardPlacement out{};
    camera::Vec3 pos = simulation_pos;

    const float base_terrain_y = terrain::heightAtActorFeet(
        scene,
        simulation_pos.x,
        simulation_pos.z,
        binding.height_sample_tx,
        binding.height_sample_ty);
    const float foot_terrain_y = terrain::heightAtActorFeet(
        scene,
        pos.x,
        pos.z,
        binding.height_sample_tx,
        binding.height_sample_ty);
    pos.y = foot_terrain_y + (simulation_pos.y - base_terrain_y);

    float sx = 0.0f;
    float sy = 0.0f;
    if (!camera.worldToScreen(pos, viewport_w, viewport_h, sx, sy, out.depth)) {
        return out;
    }

    const camera::Vec3 screen_offset =
        verticalOnlyScreenOffset(camera, screen_offset_y_px, out.depth, viewport_h);
    pos.x += screen_offset.x;
    pos.y += screen_offset.y;
    pos.z += screen_offset.z;

    out.world_h = authoredSpriteWorldHeight(scene, static_cast<float>(source_rect.h), sprite_scale);
    const float tex_aspect =
        static_cast<float>(source_rect.w) / std::max(1.0f, static_cast<float>(source_rect.h));
    out.world_w = out.world_h * tex_aspect;

    out.anchor = pos;
    out.feet = pos;
    out.shadow_ground = out.feet;
    out.visible = true;
    return out;
}

} // namespace pr::gameplay::world3d::rendering
