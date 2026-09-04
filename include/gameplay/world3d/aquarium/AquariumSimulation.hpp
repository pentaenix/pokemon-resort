#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/aquarium/AquariumConfig.hpp"
#include "gameplay/world3d/aquarium/AquariumNavigation.hpp"

#include <array>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::aquarium {

struct AquariumPokemonActor {
    std::string id;
    std::string species;
    std::string form;
    std::string model_path;
    std::string animation;
    Point3 world_position{};
    float world_yaw_degrees = 0.0f;
    float world_pitch_degrees = 0.0f;
    float model_scale = 0.01f;
    double animation_time_seconds = 0.0;
    AquariumPokemonPresentationConfig presentation;
};

struct AquariumTankRuntime {
    std::string placement_id;
    Point3 world_center{};
    float half_width_world = 0.0f;
    float half_depth_world = 0.0f;
    float yaw_degrees = 0.0f;
    float units_per_meter_world = 1.0f;
    // The authored installation floor and complete water range in world units.
    // Camera placement uses these to remain above the room floor even for tanks
    // whose navigation extends underground.
    float floor_y_world = 0.0f;
    float water_bottom_world = 0.0f;
    float water_top_world = 0.0f;
    AquariumInspectionCameraConfig inspection_camera;
    // Optional player-design radius used only to shape local exhibit-light spill.
    float light_corner_radius_world = 0.0f;
};

// Runtime-only population request used by generated player tanks. The
// population policy owns species and movement choices; the simulation owns
// navigation, animation time, and actor transforms.
struct AquariumSwimmerDefinition {
    AquariumPokemonActor actor;
    AquariumPokemonConfig movement;
    std::uint32_t seed = 1;
    int formation_index = 0;
    int formation_count = 1;
};

struct AquariumPlayerTankSimulationInput {
    std::string tank_id;
    AquariumNavigation navigation;
    Point3 world_origin{};
    float yaw_degrees = 0.0f;
    AquariumInspectionCameraConfig inspection_camera;
    std::vector<AquariumSwimmerDefinition> swimmers;
    float light_corner_radius_world = 0.0f;
};

class AquariumSimulation {
public:
    AquariumSimulation(
        std::filesystem::path project_root,
        const SceneConfig& scene,
        const AquariumMapConfig* config);

    bool active() const { return !actors_.empty(); }
    const std::vector<AquariumPokemonActor>& actors() const { return actors_; }
    const std::vector<AquariumTankRuntime>& tanks() const { return tanks_; }
    const std::vector<std::string>& warnings() const { return warnings_; }
    void replacePlayerTanks(
        const std::vector<AquariumPlayerTankSimulationInput>& tanks);
    void update(double dt_seconds);

private:
    struct TankTransform {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float yaw_degrees = 0.0f;
        float scale = 1.0f;
    };

    struct Swimmer {
        enum class Behavior { Stationary, Wander, School };

        std::size_t actor_index = 0;
        AquariumPokemonActor actor;
        AquariumNavigation navigation;
        TankTransform tank;
        Point3 local_position{};
        Point3 local_target{};
        float speed = 0.5f;
        float turn_speed = 120.0f;
        float radius = 0.1f;
        float lower_extent = 0.0f;
        float upper_extent = 0.0f;
        bool floor_navigation = false;
        float floor_navigation_y = 0.0f;
        Behavior behavior = Behavior::Wander;
        std::string school_id;
        float school_phase = 0.0f;
        float school_elapsed = 0.0f;
        bool school_detouring = false;
        Point3 volume_center{};
        Point3 volume_half_extent{};
        std::mt19937 rng;
        bool player_built = false;
    };

    struct TankSimulationContext {
        std::string id;
        AquariumNavigation navigation;
        TankTransform transform;
        std::array<float, 4> horizontal_bounds{};
        std::array<float, 2> vertical_bounds{};
        float world_units_per_meter = 1.0f;
    };

    static Point3 toWorld(const TankTransform& tank, float units_per_meter, Point3 local);
    bool appendSwimmer(
        AquariumPokemonActor actor,
        const AquariumPokemonConfig& movement,
        const TankSimulationContext& tank,
        std::uint32_t seed,
        int copy,
        int count,
        bool player_built);
    bool chooseTarget(Swimmer& swimmer);
    bool containsBody(const Swimmer& swimmer, Point3 origin) const;
    bool segmentNavigable(const Swimmer& swimmer, Point3 from, Point3 to) const;
    Point3 resolveStartingPosition(
        Swimmer& swimmer,
        const AquariumPokemonConfig& config,
        int copy,
        int count);
    void updateSchool(Swimmer& swimmer, float dt);
    bool moveTowardTarget(Swimmer& swimmer, Point3 target, float dt);
    void syncActor(Swimmer& swimmer);

    std::filesystem::path project_root_;
    std::vector<Swimmer> swimmers_;
    std::vector<AquariumPokemonActor> actors_;
    std::vector<AquariumTankRuntime> tanks_;
    std::vector<std::string> warnings_;
    std::size_t authored_tank_count_ = 0;
    std::size_t authored_warning_count_ = 0;
};

} // namespace pr::gameplay::world3d::aquarium
