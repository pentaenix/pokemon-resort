#pragma once

#include "gameplay/world3d/camera/Gen4CameraPreset.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::aquarium {

struct AquariumBuildingCameraConfig {
    bool enabled = false;
    // Intuitive Cartesian controls relative to the followed player. The
    // runtime derives the orbit distance and pitch from these tile values.
    float distance_behind_player_tiles = 24.0f;
    float height_above_player_tiles = 34.0f;
    // Zero preserves the map camera's near plane. Aquarium interiors can use
    // a smaller local value so swimmers above close tunnel cameras stay visible.
    float near_clip_tiles = 0.0f;
    // Zero preserves the map camera's far plane. A positive value gives large
    // aquarium rooms a local far plane without changing outdoor rendering.
    float far_clip_tiles = 0.0f;
};

struct AquariumBuildingLightingConfig {
    bool enabled = false;
    float brightness = 1.0f;
    std::array<float, 3> tint{1.0f, 1.0f, 1.0f};
};

struct AquariumTankLightingConfig {
    bool enabled = false;
    float brightness = 1.0f;
    std::array<float, 3> tint{1.0f, 1.0f, 1.0f};
    std::array<float, 3> spill_color{0.18f, 0.58f, 0.86f};
    float spill_opacity = 0.0f;
    float spill_reach_tiles = 1.5f;
    // Artistic player-water absorption multiplier. Zero disables attenuation;
    // one is the authored baseline. Deliberately uncapped for murky-water tuning.
    float water_attenuation_intensity = 1.0f;
    // Playback multiplier for the translucent Black 2 upper-water material.
    // Zero freezes the authored UV timeline without affecting bounded fog.
    float water_surface_speed = 1.0f;
    // Player-built substrate darkening. Zero preserves the source texture and
    // one fades it to black; scoped to aquarium tank sand only.
    float sand_darkening = 0.0f;
};

struct AquariumBuildingPresentationConfig {
    AquariumBuildingCameraConfig camera;
    AquariumBuildingLightingConfig lighting;
    AquariumTankLightingConfig tank_lighting;
};

struct AquariumPokemonEmissionConfig {
    float bulb_brightness = 1.0f;
    float halo_brightness = 0.55f;
    // Fraction restored after bounded fog, with ordinary scene depth occlusion.
    float fog_retention = 0.4f;
};

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
    AquariumPokemonEmissionConfig emission;
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
    // School/patrol vertical amplitude as a fraction of usable water height.
    float vertical_movement_scale = 0.34f;
    // Maximum dynamic pitch while following a rising/falling swim path.
    float swim_pitch_degrees = 0.0f;
    // Optional inertial steering. Zero preserves immediate path changes.
    float motion_smoothing_seconds = 0.0f;
    float pitch_turn_degrees_per_second = 20.0f;
    // Couples horizontal translation to the rendered heading. Useful for fish
    // that should steer in arcs instead of sliding sideways toward formation slots.
    bool forward_only = false;
    bool continuous_cruise = false;
    bool habitat_tour = false;
    // Runtime actor ID followed by escort behavior. Offsets describe a loose
    // safety envelope around the leader's measured body, not world-space slots.
    std::string follow_actor_id;
    float follow_distance_meters = 0.65f;
    float follow_vertical_gap_meters = 0.08f;
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
    // Optional timid/local-movement behavior used by catalogue-driven player
    // populations. Threat names are normalized species slugs.
    bool random_start = false;
    float idle_seconds_minimum = 0.0f;
    float idle_seconds_maximum = 0.0f;
    float local_move_distance_meters = 0.0f;
    float flee_radius_meters = 0.0f;
    float flee_distance_meters = 0.0f;
    float flee_speed_multiplier = 1.0f;
    std::vector<std::string> threat_species;
    // Resolved catalogue activity policy. Zero timings preserve continuous
    // legacy motion for authored tanks and ordinary free swimmers.
    std::string idle_animation;
    float idle_pitch_degrees = 0.0f;
    float move_seconds_minimum = 0.0f;
    float move_seconds_maximum = 0.0f;
    float rest_seconds_minimum = 0.0f;
    float rest_seconds_maximum = 0.0f;
    float roaming_height_meters = 0.0f;
    float crowd_body_scale = 0.68f;
    bool rest_at_bottom = false;
    // Player-stocked species carry an offline, animation-sampled envelope in
    // model units. Authored tanks may omit it and retain legacy live measuring.
    bool has_baked_physical_envelope = false;
    std::array<float, 6> baked_physical_envelope{};
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
    // Runtime-only compatibility strip. New/changed footprints may not use it.
    std::vector<Cell> legacy_wall_cells;
    bool has_return_cell = false;
    Cell return_cell;
    std::string return_facing = "south";
    bool has_room_trim_color = false;
    std::array<std::uint8_t, 4> room_trim_color{96, 104, 122, 255};
};

struct AquariumMapConfig {
    std::string map_id;
    AquariumBuildingPresentationConfig building_presentation;
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
    AquariumBuildingPresentationConfig building_presentation;
    std::vector<AquariumMapConfig> maps;
};

AquariumCatalog loadAquariumCatalog(
    const std::string& project_root,
    std::string* error = nullptr);
const AquariumMapConfig* aquariumMapConfig(const AquariumCatalog& catalog, const std::string& map_id);

camera::Gen4CameraPreset aquariumBuildingCameraPreset(
    const camera::Gen4CameraPreset& base,
    const AquariumBuildingCameraConfig& config,
    float tile_size);

} // namespace pr::gameplay::world3d::aquarium
