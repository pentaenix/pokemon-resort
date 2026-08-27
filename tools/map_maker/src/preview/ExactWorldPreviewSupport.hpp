#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/camera/Gen4CameraPreset.hpp"
#include "gameplay/world3d/terrain/ActorTerrainBinding.hpp"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <string>
#include <system_error>

namespace pr::mapmaker::preview_detail {

namespace fs = std::filesystem;
using CameraPreset = gameplay::world3d::camera::Gen4CameraPreset;
using Vec3 = gameplay::world3d::camera::Vec3;
using Scene = gameplay::world3d::SceneConfig;
using Binding = gameplay::world3d::terrain::ActorTerrainBinding;

struct StablePlayer {
    Vec3 position{};
    Binding binding{};
    SDL_Rect source_rect{};
    std::string activity_id;
    bool draw_shadow = true;
    bool valid = false;
};

inline fs::path normalizedAbsolute(fs::path path) {
    std::error_code error;
    fs::path absolute = path.is_absolute() ? path : fs::absolute(path, error);
    if (error) absolute = std::move(path);
    const fs::path canonical = fs::weakly_canonical(absolute, error);
    return error ? absolute.lexically_normal() : canonical;
}

inline fs::path resolveMapPath(const fs::path& project_root, const fs::path& requested) {
    if (requested.is_absolute()) return normalizedAbsolute(requested);
    const std::array<fs::path, 3> candidates{
        project_root / requested,
        project_root / "assets" / "overworld" / "maps" / requested,
        requested};
    std::error_code error;
    for (const fs::path& candidate : candidates) {
        if (fs::is_regular_file(candidate, error) && !error) return normalizedAbsolute(candidate);
        error.clear();
    }
    return (project_root / requested).lexically_normal();
}

inline CameraPreset unscaledScenePreset(const Scene& scene) {
    CameraPreset preset = gameplay::world3d::camera::loadGen4PresetById(scene.camera_preset.c_str());
    if (scene.camera_distance > 0.0f) preset.distance = scene.camera_distance;
    preset.pitch_deg = scene.camera_pitch_deg;
    preset.yaw_deg = scene.camera_yaw_deg;
    preset.roll_deg = scene.camera_roll_deg;
    preset.near_clip = scene.camera_near_clip;
    preset.far_clip = scene.camera_far_clip;
    preset.aspect_width = scene.camera_aspect_width;
    preset.aspect_height = scene.camera_aspect_height;
    preset.fov_y_deg = scene.camera_fov_y_deg;
    return preset;
}

inline int facingRow(
    const gameplay::world3d::CharacterSpriteDefinition& character,
    gameplay::world3d::FacingDirection facing) {
    switch (facing) {
        case gameplay::world3d::FacingDirection::South: return character.row_south;
        case gameplay::world3d::FacingDirection::West: return character.row_west;
        case gameplay::world3d::FacingDirection::East: return character.row_east;
        case gameplay::world3d::FacingDirection::North: return character.row_north;
    }
    return character.row_south;
}

inline bool actualWaterAt(const Scene& scene, int tile_x, int tile_y) {
    if (!scene.water_terrain.enabled || tile_y < 0 ||
        tile_y >= static_cast<int>(scene.water_terrain.actual_water_cells.size())) return false;
    const auto& row = scene.water_terrain.actual_water_cells[static_cast<std::size_t>(tile_y)];
    return tile_x >= 0 && tile_x < static_cast<int>(row.size()) &&
        row[static_cast<std::size_t>(tile_x)] != 0;
}

inline StablePlayer stablePlayerFor(
    const Scene& scene,
    const gameplay::world3d::CharacterSpriteDefinition& character) {
    StablePlayer out;
    const int width = std::max(1, scene.grid.width);
    const int height = std::max(1, scene.grid.height);
    const float tile_size = std::max(1.0f, scene.grid.tile_size);
    const int tile_x = std::clamp(scene.player.spawn_tile_x, 0, width - 1);
    const int tile_y = std::clamp(scene.player.spawn_tile_y, 0, height - 1);
    out.position.x = (static_cast<float>(tile_x) + 0.5f) * tile_size;
    out.position.z = (static_cast<float>(tile_y) + 0.5f) * tile_size;
    out.binding = gameplay::world3d::terrain::bindActorStanding(
        scene, tile_x, tile_y, out.position.x, out.position.z);
    out.position.y = out.binding.simulation_y;
    const bool swimming = actualWaterAt(scene, tile_x, tile_y) && character.has_swim;
    const auto& animation = swimming ? character.swim : character.idle;
    const int frame = animation.frames.empty() ? 0 : animation.frames.front();
    const int column = std::clamp(frame, 0, std::max(0, character.columns - 1));
    const int row = std::clamp(
        facingRow(character, scene.player.facing), 0, std::max(0, character.rows - 1));
    out.source_rect = SDL_Rect{column * character.frame_width, row * character.frame_height,
        character.frame_width, character.frame_height};
    out.activity_id = swimming ? "__locomotion_swim" : std::string{};
    out.draw_shadow = !swimming;
    out.valid = true;
    return out;
}

inline void updatePlayerFrame(
    StablePlayer& player,
    const gameplay::world3d::CharacterSpriteDefinition& character,
    gameplay::world3d::FacingDirection facing,
    bool moving,
    bool swimming,
    double elapsed_seconds) {
    const auto& animation = swimming && character.has_swim
        ? character.swim : moving ? character.walk : character.idle;
    int frame = 0;
    if (!animation.frames.empty()) {
        const double frame_seconds = std::max(1, animation.frame_time_ms) / 1000.0;
        const std::size_t index = moving || swimming
            ? static_cast<std::size_t>(elapsed_seconds / frame_seconds) % animation.frames.size()
            : 0U;
        frame = animation.frames[index];
    }
    const int column = std::clamp(frame, 0, std::max(0, character.columns - 1));
    const int row = std::clamp(
        facingRow(character, facing), 0, std::max(0, character.rows - 1));
    player.source_rect = SDL_Rect{column * character.frame_width, row * character.frame_height,
        character.frame_width, character.frame_height};
    player.activity_id = swimming && character.has_swim ? "__locomotion_swim" : std::string{};
    player.draw_shadow = !(swimming && character.has_swim);
}

inline Vec3 tileFocus(const Scene& scene, int tile_x, int tile_y) {
    tile_x = std::clamp(tile_x, 0, std::max(1, scene.grid.width) - 1);
    tile_y = std::clamp(tile_y, 0, std::max(1, scene.grid.height) - 1);
    const float tile_size = std::max(1.0f, scene.grid.tile_size);
    Vec3 focus{(static_cast<float>(tile_x) + 0.5f) * tile_size, 0.0f,
        (static_cast<float>(tile_y) + 0.5f) * tile_size};
    focus.y = gameplay::world3d::terrain::bindActorStanding(
        scene, tile_x, tile_y, focus.x, focus.z).simulation_y;
    return focus;
}

inline Vec3 mapFocus(const Scene& scene) {
    const float tile_size = std::max(1.0f, scene.grid.tile_size);
    const float x = static_cast<float>(std::max(1, scene.grid.width)) * tile_size * 0.5f;
    const float z = static_cast<float>(std::max(1, scene.grid.height)) * tile_size * 0.5f;
    const int tile_x = std::clamp(
        static_cast<int>(std::floor(x / tile_size)), 0, std::max(1, scene.grid.width) - 1);
    const int tile_y = std::clamp(
        static_cast<int>(std::floor(z / tile_size)), 0, std::max(1, scene.grid.height) - 1);
    Vec3 focus{x, 0.0f, z};
    focus.y = gameplay::world3d::terrain::bindActorStanding(
        scene, tile_x, tile_y, x, z).simulation_y;
    return focus;
}

} // namespace pr::mapmaker::preview_detail
