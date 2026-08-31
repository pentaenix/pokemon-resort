#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::aquarium {

struct AquariumPokemonPresentationConfig {
    // Defaults mirror TEST ATTEND's active clear-sky Pokemon presentation.
    float brightness = 1.08f;
    float pokemon_brightness = 1.02f;
    float saturation = 1.0f;
    float contrast = 1.0f;
    float ambient = 0.74f;
    float directional = 0.34f;
    float form_shadow = 0.24f;
    std::array<float, 3> light_direction{-0.35f, 0.82f, 0.45f};
    std::array<float, 3> tint{1.0f, 0.99f, 0.95f};
};

struct AquariumInspectionCameraConfig {
    bool enabled = true;
    // Offset from the valid follow-camera pose: side, lower, closer (world tiles).
    // Keeping the existing orbit distance also keeps the scene beyond the Gen 4
    // camera's large near clip plane.
    float side_tiles = 0.0f;
    float lower_tiles = 1.35f;
    float closer_tiles = 1.15f;
    // Point inside the aquarium to look at, in Aquarium Maker metres.
    float look_at_x_meters = 0.0f;
    float look_at_y_meters = 1.15f;
    float look_at_z_meters = 0.0f;
    // <= 0 snaps immediately. Positive values are world units per second.
    float smooth = 0.0f;
    // Return-to-gameplay speed. This is intentionally independent from the
    // slower presentation move into a tank view so movement can resume at once.
    float return_smooth = 2400.0f;
    float interaction_reach_tiles = 1.35f;

    // Optional explicit first-stage framing. Legacy configs without this block
    // retain the original follow-camera-relative offsets above.
    bool has_framed_inspection_view = false;
    float inspection_behind_player_tiles = 11.0f;
    float inspection_front_height_tiles = 5.15f;
    float inspection_side_height_tiles = 4.43f;

    // The second, actor-free inspection stage is framed from the face the
    // player approached. Distances remain tile-relative so a captured view is
    // stable when the world-unit scale changes.
    float focused_standoff_tiles = 9.8125f;
    float focused_front_height_tiles = 4.15f;
    float focused_side_height_tiles = 3.43f;
    float focused_front_pitch_degrees = -10.48f;
    float focused_side_pitch_degrees = -10.72f;
    float focused_near_clip = 12.0f;
    // Radius around the close-focus camera in which default room walls are
    // clipped. Tanks, floors, models, and Pokemon are unaffected.
    float focused_wall_clip_radius_tiles = 0.0f;
};

struct AquariumPokemonConfig {
    std::string id;
    std::string species;
    std::string form;
    std::string animation = "walk";
    int count = 1;
    float size_multiplier = 1.0f;
    float speed_meters_per_second = 0.55f;
    float turn_degrees_per_second = 120.0f;
    float body_radius_meters = 0.18f;
    // Model-space pitch applied before the tank's world yaw. -90 rotates a
    // forward-facing model onto its back so its front points upward.
    float pitch_degrees = 0.0f;
    // Optional Aquarium Maker-space origin. The runtime validates it against the
    // water volume and the rendered model bounds before using it.
    std::array<float, 3> starting_position_meters{};
    bool has_starting_position = false;
    // authored: use Y above; bottom: rest on the swim volume; floor: rest on
    // the Aquarium Maker coordinateSystem.floorLevelY (for shallow pools).
    std::string vertical_anchor = "authored";
    // volume: full 3D swimming; floor: horizontal navigation on a shallow
    // authored surface while the visible model may extend above the water slab.
    std::string movement_plane = "volume";
    // stationary, wander, or school. Empty/legacy values infer from speed.
    std::string behavior;
};

struct AquariumTankConfig {
    std::string placement_id;
    std::string navigation_path;
    std::uint32_t seed = 1;
    AquariumInspectionCameraConfig inspection_camera;
    std::vector<AquariumPokemonConfig> pokemon;
};

struct AquariumConstructionConfig {
    struct Cell {
        int column = 0;
        int row = 0;
    };
    bool enabled = false;
    std::vector<Cell> allowed_cells;
    bool has_return_cell = false;
    Cell return_cell;
    std::string return_facing = "south";
    bool has_room_trim_color = false;
    std::array<std::uint8_t, 4> room_trim_color{96, 104, 122, 255};
};

struct AquariumMapConfig {
    std::string map_id;
    float pokemon_scale = 0.14f;
    AquariumPokemonPresentationConfig pokemon_presentation;
    std::vector<AquariumTankConfig> tanks;
    AquariumConstructionConfig construction;
};

struct AquariumCatalog {
    // Attend exports retain species-relative proportions, so one authored scale
    // controls every aquarium Pokemon without per-species correction tables.
    float pokemon_scale = 0.14f;
    AquariumPokemonPresentationConfig pokemon_presentation;
    std::vector<AquariumMapConfig> maps;
};

AquariumCatalog loadAquariumCatalog(
    const std::string& project_root,
    std::string* error = nullptr);
const AquariumMapConfig* aquariumMapConfig(const AquariumCatalog& catalog, const std::string& map_id);

} // namespace pr::gameplay::world3d::aquarium
