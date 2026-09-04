#pragma once

#include <string>
#include <cstdint>
#include <unordered_map>
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

struct CharacterActivitySessionDef {
    CharacterAnimationDef enter;
    CharacterAnimationDef stay;
    CharacterAnimationDef exit;
    bool valid = false;
};

struct CharacterSpriteDefinition {
    std::string id;
    std::string texture_path;
    std::vector<std::uint8_t> texture_png_bytes;
    std::vector<std::uint8_t> run_texture_png_bytes;
    std::unordered_map<std::string, std::vector<std::uint8_t>> activity_texture_png_bytes;
    int frame_width = 32;
    int frame_height = 32;
    int columns = 4;
    int rows = 4;
    int row_south = 0;
    int row_west = 1;
    int row_east = 2;
    int row_north = 3;
    float world_height = 32.0f;
    // Legacy SDL/perspective billboard scale. The bgfx pixel compositor uses source
    // frame size * WorldViewportConfig::internal_scale for normal overworld sprites.
    float sprite_scale = 1.0f;
    float world_offset_x = 0.0f;
    float world_offset_y = 0.0f;
    float world_offset_z = 0.0f;
    std::string anchor = "bottom_center";
    int screen_offset_x_px = 0;
    int screen_offset_y_px = 0;
    CharacterAnimationDef idle;
    CharacterAnimationDef walk;
    CharacterAnimationDef run;
    CharacterAnimationDef swim;
    CharacterAnimationDef pause;
    CharacterAnimationDef play;
    std::unordered_map<std::string, CharacterActivitySessionDef> activity_sessions;
    std::string character_type = "npc";
    std::string pokemon_size = "small";
    std::string species_name;
    std::vector<std::string> pokemon_types;
    bool has_run = false;
    bool has_swim = false;
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

struct PixelScaleConfig {
    // Authored map tile texture size in pixels (RTPKS tiles are typically 16×16 per cell).
    int map_pixels_per_tile = 16;
    // World units per source art pixel. <= 0 derives tile_size / map_pixels_per_tile.
    float world_units_per_pixel = 0.0f;
    // Deprecated compatibility input. New configs should use SceneCameraConfig::distance_scale.
    float zoom = 1.0f;
    float zoom_min = 0.5f;
    float zoom_max = 3.0f;
    // Deprecated compatibility input. New configs should use WorldViewportConfig.
    bool pixel_perfect_world = true;
    int world_render_width = 400;
    int world_render_height = 250;
};

struct WorldViewportConfig {
    bool enabled = true;
    int base_width = 400;
    int base_height = 250;
    int internal_scale = 1;
};

struct SceneCameraConfig {
    float distance_scale = 1.0f;
    float distance_scale_min = 0.5f;
    float distance_scale_max = 3.0f;
};

struct PixelCompositorConfig {
    bool snap_anchors = true;
    std::string sprite_sizing = "native";
    float actor_depth_bias_px = 0.5f;
    int actor_screen_offset_y_px = 0;
};

struct PresentationConfig {
    std::string scale_mode = "integerFit";
    float zoom = 1.0f;
};

struct TerrainColor {
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
    std::uint8_t a = 255;
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
    // 14 = actor spawn marker; geometrically flat and not consumed by spawning yet
    std::vector<std::vector<std::uint8_t>> heights;
    std::vector<std::vector<std::uint8_t>> specials;
    std::vector<std::vector<std::uint8_t>> collision;
    // Renderer-facing terrain presentation. height_per_floor controls vertical world
    // units per encoded height level; <= 0 keeps the historical tile_size behavior.
    float height_per_floor = 0.0f;
    // Runtime feet-height sampling inset for cardinal ramps. 0 preserves a full-tile
    // linear ramp; positive values keep the low side flat for this many world pixels
    // before the incline begins.
    float ramp_incline_inset_px = 6.0f;
    TerrainColor floor_color_a{116, 156, 190, 255};
    TerrainColor floor_color_b{125, 166, 200, 255};
    bool floor_height_recolor_enabled = false;
    TerrainColor first_non_base_floor_color_a{196, 84, 86, 255};
    TerrainColor first_non_base_floor_color_b{214, 102, 92, 255};
    bool ramp_recolor_enabled = false;
    TerrainColor ramp_color_a{116, 156, 190, 255};
    TerrainColor ramp_color_b{125, 166, 200, 255};
    bool textured_ramp_readability_enabled = true;
    float textured_ramp_low_shade = 0.88f;
    float textured_ramp_high_shade = 1.10f;
    float textured_ramp_band_count = 5.0f;
    float textured_ramp_band_strength = 0.12f;
    float textured_ramp_band_softness = 0.32f;
    TerrainColor wall_color_ns{88, 117, 145, 255};
    TerrainColor wall_color_ew{80, 108, 136, 255};
    TerrainColor wire_color{102, 138, 170, 120};
};

struct WaterTerrainConfig {
    bool enabled = true;
    // Absolute simulation height, in world units, used once an actor is on an
    // actual water tile. The current Gen 5 ocean surface sits 11 units below the
    // authored sand surface when tileSize is 16.
    float surface_height_world = -11.0f;
    // Shoreline/coast tiles remain land for gameplay, but actor feet blend from
    // the regular terrain height to surface_height_world while crossing them.
    bool shoreline_ramp_enabled = true;
    bool pokemon_swim_animation_enabled = true;

    // Runtime-derived from RTPKS tags and expanded tile footprints.
    std::vector<std::vector<std::uint8_t>> actual_water_cells;
    std::vector<std::vector<std::uint8_t>> shoreline_cells;
    std::vector<std::vector<float>> shoreline_corner_progress;
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

struct TilePackageConfig {
    std::string file;
    std::string pack_id;
    std::string name;
    std::string path;
};

struct TileLayerConfig {
    std::string id;
    bool visible = true;
    // -1 means empty. Values >= 0 are stable RTPKS resortTileId values.
    std::vector<std::vector<int>> cells;
};

struct TileLayersConfig {
    int active_layer = 0;
    std::vector<TileLayerConfig> layers;
};

struct MapAnchorConfig {
    std::string id;
    int tile_x = 0;
    int tile_y = 0;
    FacingDirection facing = FacingDirection::South;
};

struct MapLinkConfig {
    std::string id;
    std::string destination_map_id;
    std::string destination_anchor_id;
};

struct DoorVisualTileConfig {
    bool enabled = false;
    std::string map_id;
    std::string layer_id;
    int tile_x = 0;
    int tile_y = 0;
};

struct DoorTriggerConfig {
    std::string id;
    int tile_x = 0;
    int tile_y = 0;
    std::vector<FacingDirection> allowed_directions;
    DoorVisualTileConfig visual;
    std::string link_id;
    std::string script_id = "door_enter_default";
};

// Gameplay-facing identity of the visible tile surface at one logical map cell.
// `surface` comes from the tile's single `surface.*` tag; the remaining tags are
// retained so scripts can opt into more specific behavior without changing OWMAP.
struct TileSurfaceInfo {
    std::string surface = "ground";
    int resort_tile_id = -1;
    std::string tile_name;
    std::vector<std::string> tags;
};

struct TileSurfaceGrid {
    std::vector<std::vector<TileSurfaceInfo>> cells;
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

enum class SpawnTileUse {
    PokemonRandomFromBoxes,
    NpcWithPartnerPokemon,
    NpcWithoutPokemon,
};

struct SpawnTileConfig {
    std::string id;
    int tile_x = 0;
    int tile_y = 0;
    SpawnTileUse allows = SpawnTileUse::PokemonRandomFromBoxes;
};

struct MapEnvironmentConfig {
    std::string space;
    TerrainColor clear_color{150, 191, 224, 255};
    bool render_other_spaces = true;
};

struct InteriorOpeningConfig {
    std::string edge;
    int from = 0;
    int to = 0;
};

struct InteriorFloorCutoutConfig {
    int x = 0;
    int y = 0;
    int width = 1;
    int height = 1;
    // Optional convex polygon in the referenced model's local X/Z units. This
    // cuts rounded/angled installations exactly while preserving floor UVs.
    std::string placement_id;
    std::vector<std::array<float, 2>> local_polygon;
    // Runtime installations can provide an exact world-space outline without
    // creating a synthetic model placement. This field is never serialized by
    // the player aquarium document; it is rebuilt from its tank design.
    std::vector<std::array<float, 2>> world_polygon;
};

struct InteriorDefaultRoomConfig {
    // Shell-less interiors receive a procedural floor and cutaway wall envelope.
    // Authored RTPKS tiles render over this foundation and can replace it gradually.
    bool enabled = true;
    float wall_height_tiles = 4.0f;
    float front_wall_height_tiles = 0.35f;
    // Boundary openings keep their lower portion clear while the wall/lintel
    // continues above them. A large default preserves legacy full-height
    // openings unless a room explicitly opts into a lower doorway.
    float opening_height_tiles = 16.0f;
    float trim_height_tiles = 0.125f;
    int walkable_inset_tiles = 1;
    float wall_face_offset_tiles = 0.5f;
    // Additional rows beyond the map boundary. The normal three-wide entry is
    // already the final in-bounds row, so the default adds no second row.
    float entry_extension_depth_tiles = 0.0f;
    bool black_top_cap = true;
    float top_cap_depth_tiles = 0.125f;
    // Optional opaque curtain below the south/front floor edge. Deep interior
    // geometry remains hidden below the room datum without changing collision.
    float lower_facade_depth_tiles = 0.0f;
    TerrainColor floor_color_a{72, 80, 94, 255};
    TerrainColor floor_color_b{80, 89, 104, 255};
    TerrainColor wall_color_ns{58, 64, 78, 255};
    TerrainColor wall_color_ew{52, 58, 72, 255};
    TerrainColor trim_color{96, 104, 122, 255};
    TerrainColor baseboard_color{42, 47, 58, 255};
    TerrainColor top_cap_color{0, 0, 0, 255};
    TerrainColor lower_facade_color{0, 0, 0, 255};
};

struct InteriorMapConfig {
    std::string shell_model_id;
    float floor_datum = 0.0f;
    int grid_origin_x = 0;
    int grid_origin_y = 0;
    std::vector<InteriorOpeningConfig> openings;
    std::vector<InteriorFloorCutoutConfig> floor_cutouts;
    InteriorDefaultRoomConfig default_room;
};

struct SceneConfig {
    std::string id;
    std::string map_type = "exterior";
    MapEnvironmentConfig environment;
    InteriorMapConfig interior;
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
    // Renderer-only speed for ambient tile material motion and animated map
    // props. This deliberately does not alter simulation or actor animation.
    float environment_animation_speed = 1.0f;
    float water_scroll_speed = 0.75f;
    float water_wave_speed = 1.25f;
    float water_wave_wait_seconds = 0.0f;
    bool water_smooth_uv_motion = true;
    float water_shoreline_seam_overlap_pixels = 0.5f;
    // How far (in tiles) toward the camera a placed model's occlusion anchor is biased when
    // depth-sorting it against characters. Larger = the model starts hiding the character
    // sooner (further forward), so a character stepping into a doorway is occluded by the
    // building instead of drawing over its roof. Loaded from config/gameplay/world3d/render.json.
    float model_behind_bias_tiles = 0.0f;
    // Gen4 presentation: shift billboards along camera forward/right on the ground plane (tiles).
    float billboard_tile_anchor_forward = 0.0f;
    float billboard_tile_anchor_right = 0.0f;
    PixelScaleConfig pixel_scale;
    WorldViewportConfig world_viewport;
    SceneCameraConfig scene_camera;
    PixelCompositorConfig pixel_compositor;
    PresentationConfig presentation;
    SpriteShadowConfig sprite_shadow;
    MapVisualConfig visual;
    GridConfig grid;
    TerrainConfig terrain;
    WaterTerrainConfig water_terrain;
    std::vector<ModelPlacementConfig> models;
    TilePackageConfig tile_package;
    TileLayersConfig tile_layers;
    std::vector<MapAnchorConfig> anchors;
    std::vector<MapLinkConfig> links;
    std::vector<DoorTriggerConfig> door_triggers;
    TileSurfaceGrid tile_surfaces;
    std::vector<NpcConfig> characters;
    // Parsed authoring markers only. The current random spawner intentionally
    // does not consume these until its later migration.
    std::vector<SpawnTileConfig> spawn_tiles;
    PlayerSpawnConfig player;
};

inline const TileSurfaceInfo* tileSurfaceAt(const SceneConfig& scene, int tile_x, int tile_y) {
    if (tile_y < 0 || tile_y >= static_cast<int>(scene.tile_surfaces.cells.size())) return nullptr;
    const auto& row = scene.tile_surfaces.cells[static_cast<std::size_t>(tile_y)];
    if (tile_x < 0 || tile_x >= static_cast<int>(row.size())) return nullptr;
    return &row[static_cast<std::size_t>(tile_x)];
}

} // namespace pr::gameplay::world3d
