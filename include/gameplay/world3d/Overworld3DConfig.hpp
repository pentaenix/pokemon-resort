#pragma once

#include <string>
#include <cstdint>
#include <vector>

namespace pr::gameplay::world3d {

enum class FacingDirection {
    South,
    West,
    East,
    North,
};

struct CharacterAnimationDef {
    std::vector<int> frames;
    int frame_time_ms = 120;
};

struct CharacterSpriteDefinition {
    std::string id;
    std::string texture_path;
    std::vector<std::uint8_t> texture_png_bytes;
    int frame_width = 32;
    int frame_height = 32;
    int columns = 4;
    int rows = 4;
    int row_south = 0;
    int row_west = 1;
    int row_east = 2;
    int row_north = 3;
    float world_height = 28.0f;
    float sprite_scale = 1.0f;
    float world_offset_x = 0.0f;
    float world_offset_y = 0.0f;
    float world_offset_z = 0.0f;
    std::string anchor = "bottom_center";
    int screen_offset_x_px = 0;
    int screen_offset_y_px = 0;
    CharacterAnimationDef idle;
    CharacterAnimationDef walk;
    CharacterAnimationDef pause;
    CharacterAnimationDef play;
};

struct MapVisualConfig {
    std::string mesh_path;
    std::string material_path;
    std::string texture_directory;
    float origin_x = 0.0f;
    float origin_y = 0.0f;
    float origin_z = 0.0f;
    float scale = 1.0f;
};

struct GridConfig {
    bool enabled = false;
    float tile_size = 0.0f;
    int width = 0;
    int height = 0;
};

struct SpriteShadowConfig {
    bool enabled = true;
    float opacity = 0.33f;
    bool pixel_coherent = true;
    int feet_to_shadow_bottom_px = 3;
    int screen_offset_x_px = 0;
    int screen_offset_y_px = 0;
    std::uint8_t color_r = 0x37;
    std::uint8_t color_g = 0x38;
    std::uint8_t color_b = 0x3B;
    std::uint8_t color_a = 0x94;
    float radius_x_tiles = 0.32f;
    float radius_z_tiles = 0.20f;
    float world_y_lift = 0.1f;
    int texture_width_px = 17;
    int texture_height_px = 9;
    std::vector<std::string> mask_rows;
};

enum class NpcMovementProfile {
    StandStill,
    RandomMove,
    LineMove,
    RotateInPlace
};

struct TerrainConfig {
    // Tile special IDs:
    // 0 = flat
    // 1 = editor-only auto ramp; must be resolved by the map editor/exporter before the game loads the map
    // 2..5 = linear ramps (N/E/S/W)
    // 6..9 = convex corners (NE/SE/SW/NW)
    // 10..13 = concave corners (NE/SE/SW/NW)
    std::vector<std::vector<std::uint8_t>> heights;
    std::vector<std::vector<std::uint8_t>> specials;
    std::vector<std::vector<std::uint8_t>> collision;
};

struct ModelPlacementConfig {
    std::string id;
    std::string glb_path; // resolved path to the placed GLB asset
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float yaw_deg = 0.0f;
    float scale = 1.0f;
};

struct NpcConfig {
    std::string id;
    std::string character_id;
    int tile_x = 0;
    int tile_y = 0;
    FacingDirection facing = FacingDirection::South;
    NpcMovementProfile movement_profile = NpcMovementProfile::StandStill;
    int line_distance = 1;
};

struct PlayerSpawnConfig {
    std::string character_path;
    int spawn_tile_x = 0;
    int spawn_tile_y = 0;
    float spawn_height = 0.0f;
    FacingDirection facing = FacingDirection::South;
};

struct SceneConfig {
    std::string id;
    std::string camera_preset;
    std::string lighting_preset;
    float camera_distance = 0.0f;
    float camera_pitch_deg = 0.0f;
    float camera_yaw_deg = 0.0f;
    float camera_roll_deg = 0.0f;
    float camera_near_clip = 0.0f;
    float camera_far_clip = 0.0f;
    float camera_aspect_width = 0.0f;
    float camera_aspect_height = 0.0f;
    float camera_fov_y_deg = 0.0f;
    float freecam_move_speed = 0.0f;
    float freecam_mouse_sensitivity = 0.0f;
    float freecam_initial_offset_x = 0.0f;
    float freecam_initial_offset_y = 0.0f;
    float freecam_initial_offset_z = 0.0f;
    float freecam_initial_yaw_deg = 0.0f;
    float freecam_initial_pitch_deg = 0.0f;
    float freecam_pitch_min_deg = 0.0f;
    float freecam_pitch_max_deg = 0.0f;
    float lighting_brightness = 1.0f;
    float lighting_tint_r = 1.0f;
    float lighting_tint_g = 1.0f;
    float lighting_tint_b = 1.0f;
    // How far (in tiles) toward the camera a placed model's occlusion anchor is biased when
    // depth-sorting it against characters. Larger = the model starts hiding the character
    // sooner (further forward), so a character stepping into a doorway is occluded by the
    // building instead of drawing over its roof. Loaded from config/gameplay/world3d/render.json.
    float model_behind_bias_tiles = 0.0f;
    SpriteShadowConfig sprite_shadow;
    MapVisualConfig visual;
    GridConfig grid;
    TerrainConfig terrain;
    std::vector<ModelPlacementConfig> models;
    std::vector<NpcConfig> characters;
    PlayerSpawnConfig player;
};

} // namespace pr::gameplay::world3d
