#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::aquarium {

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
    float interaction_reach_tiles = 1.35f;
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
    // Optional Aquarium Maker-space origin. The runtime validates it against the
    // water volume and the rendered model bounds before using it.
    std::array<float, 3> starting_position_meters{};
    bool has_starting_position = false;
    // authored: use Y above; bottom: rest the rendered model on the water floor.
    std::string vertical_anchor = "authored";
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

struct AquariumMapConfig {
    std::string map_id;
    float pokemon_scale = 0.14f;
    std::vector<AquariumTankConfig> tanks;
};

struct AquariumCatalog {
    // Attend exports retain species-relative proportions, so one authored scale
    // controls every aquarium Pokemon without per-species correction tables.
    float pokemon_scale = 0.14f;
    std::vector<AquariumMapConfig> maps;
};

AquariumCatalog loadAquariumCatalog(const std::string& project_root);
const AquariumMapConfig* aquariumMapConfig(const AquariumCatalog& catalog, const std::string& map_id);

} // namespace pr::gameplay::world3d::aquarium
