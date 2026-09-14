#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/aquarium/AquariumConfig.hpp"
#include "gameplay/world3d/aquarium/AquariumCrowdSimulation.hpp"
#include "gameplay/world3d/aquarium/AquariumFormationSimulation.hpp"
#include "gameplay/world3d/aquarium/AquariumNavigation.hpp"
#include "gameplay/world3d/aquarium/AquariumBodyNavigation.hpp"
#include "gameplay/world3d/aquarium/AquariumMotion.hpp"
#include "gameplay/world3d/aquarium/AquariumSchoolSimulation.hpp"
#include "gameplay/world3d/aquarium/AquariumConvexWater.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <memory>
#include <map>
#include <tuple>
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
    float animation_playback_rate = 1.0f;
    AquariumPokemonPresentationConfig presentation;
    float world_roll_degrees = 0.0f;
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
    // Legacy/fallback radius used only when an exact exhibit-light outline is unavailable.
    float light_corner_radius_world = 0.0f;
    // Exact player-tank outline in tank-local metres. When present, exhibit
    // spill follows asymmetric corners and edited footprints instead of
    // approximating the tank as one uniformly rounded rectangle.
    PolygonRing light_boundary_local_meters;
    // Empty for authored tanks; player tanks carry their saved exhibit look.
    std::string exhibit_preset_id;
    int brightness_level = 4;
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
    PolygonRing light_boundary_local_meters;
    std::string exhibit_preset_id = "river";
    int brightness_level = 4;
};

struct AquariumMotionDiagnostics {
    std::uint64_t route_queries = 0;
    std::uint64_t route_expanded_nodes = 0;
    std::uint64_t recoveries = 0;
    std::uint64_t unreachable_routes = 0;
    double route_milliseconds = 0.0;
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
    const AquariumMotionDiagnostics& motionDiagnostics() const { return motion_diagnostics_; }
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
        enum class Behavior { Stationary, Wander, School, Escort, Timid, Jelly };
        enum class ActivityPhase { Continuous, Resting, Moving, ReturningToRest };

        std::size_t actor_index = 0;
        AquariumPokemonActor actor;
        AquariumNavigation navigation;
        AquariumConvexWater convex_water;
        TankTransform tank;
        Point3 local_position{};
        Point3 local_target{};
        Point3 previous_local_position{};
        Point3 velocity{};
        Point3 snapshot_velocity{};
        float previous_world_yaw_degrees = 0.0f;
        float previous_world_pitch_degrees = 0.0f;
        float speed = 0.5f;
        float turn_speed = 120.0f;
        float radius = 0.1f;
        float lower_extent = 0.0f;
        float upper_extent = 0.0f;
        float body_half_width = 0.1f;
        float body_half_height = 0.1f;
        float body_half_length = 0.1f;
        bool floor_navigation = false;
        float floor_navigation_y = 0.0f;
        Behavior behavior = Behavior::Wander;
        std::string school_id;
        float school_phase = 0.0f;
        float school_elapsed = 0.0f;
        bool school_detouring = false;
        Point3 school_target{};
        float school_speed = 0.0f;
        bool school_grouped = false;
        Point3 school_visibility_goal{};
        float school_visibility_seconds = 0.0f;
        float school_steering_seconds = 0.0f;
        bool school_goal_visible = false;
        float vertical_movement_scale = 0.34f;
        float base_pitch_degrees = 0.0f;
        float swim_pitch_degrees = 0.0f;
        float motion_smoothing_seconds = 0.0f;
        float pitch_turn_speed = 20.0f;
        bool forward_only = false;
        bool continuous_cruise = false;
        bool habitat_tour = false;
        unsigned tour_destination = 0;
        float cruise_check_seconds = 0.0f;
        Point3 cruise_direction{};
        float cruise_speed_scale = 1.0f;
        AquariumCruiseBank cruise_bank;
        Point3 motion_direction{};
        bool motion_direction_initialized = false;
        float travel_pitch_degrees = 0.0f;
        AquariumMotionProgress motion_progress;
        AquariumMotionBlockage motion_blockage = AquariumMotionBlockage::None;
        std::shared_ptr<AquariumBodyNavigation> body_navigation;
        std::shared_ptr<AquariumRouteQuery> route_query;
        std::vector<Point3> route_waypoints;
        Point3 route_goal{};
        bool route_direct_valid = false;
        float route_visibility_seconds = 0.0f;
        float route_retry_seconds = 0.0f;
        Point3 recovery_target{};
        bool recovering = false;
        float yield_seconds = 0.0f;
        float diagnostic_cooldown = 0.0f;
        std::string follow_actor_id;
        float follow_distance = 0.65f;
        float follow_vertical_gap = 0.08f;
        AquariumFormationMode formation_mode = AquariumFormationMode::Attached;
        Point3 volume_center{};
        Point3 volume_half_extent{};
        std::string tank_id;
        std::mt19937 rng;
        bool player_built = false;
        bool random_start = false;
        bool timid_moving = false;
        float behavior_timer_seconds = 0.0f;
        float idle_seconds_minimum = 0.0f;
        float idle_seconds_maximum = 0.0f;
        float local_move_distance = 0.0f;
        float flee_radius = 0.0f;
        float flee_distance = 0.0f;
        float flee_speed_multiplier = 1.0f;
        std::vector<std::string> threat_species;
        Point3 jelly_anchor{};
        bool jelly_next_ascending = true;
        float movement_blocked_seconds = 0.0f;
        float crowd_body_scale = 0.68f;
        ActivityPhase activity_phase = ActivityPhase::Continuous;
        std::string movement_animation;
        std::string idle_animation;
        float idle_pitch_degrees = 0.0f;
        float move_seconds_minimum = 0.0f;
        float move_seconds_maximum = 0.0f;
        float rest_seconds_minimum = 0.0f;
        float rest_seconds_maximum = 0.0f;
        float roaming_height = 0.0f;
        bool rest_at_bottom = false;
        float activity_timer_seconds = 0.0f;
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
    bool chooseTimidTarget(Swimmer& swimmer);
    void updateTimid(Swimmer& swimmer, float dt);
    bool chooseJellyTarget(Swimmer& swimmer);
    void updateJelly(Swimmer& swimmer, float dt);
    bool chooseActivityTarget(Swimmer& swimmer);
    bool chooseActivityRestTarget(Swimmer& swimmer);
    void updateActivity(Swimmer& swimmer, float dt);
    void beginActivityRest(Swimmer& swimmer);
    void beginActivityMove(Swimmer& swimmer);
    bool containsBody(const Swimmer& swimmer, Point3 origin) const;
    AquariumBodyClearance bodyClearance(const Swimmer& swimmer) const;
    bool beginMotionRecovery(Swimmer& swimmer);
    void advanceNavigationQueries();
    bool initialSpawnAvailable(const Swimmer& swimmer, Point3 origin) const;
    Point3 crowdAdjustedDirection(const Swimmer& swimmer, Point3 direction);
    bool crowdAllowsMove(const Swimmer& swimmer, Point3 candidate);
    bool shouldRetargetBlockedMovement(Swimmer& swimmer, float dt);
    void resolveCrowdPenetrations();
    bool segmentNavigable(const Swimmer& swimmer, Point3 from, Point3 to) const;
    std::optional<Point3> resolveStartingPosition(
        Swimmer& swimmer,
        const AquariumPokemonConfig& config,
        int copy,
        int count);
    void updateSchool(Swimmer& swimmer, float dt);
    void prepareSchools(float dt);
    void updateEscort(
        Swimmer& swimmer,
        const AquariumFormationSteering& steering,
        float dt);
    bool moveTowardTarget(Swimmer& swimmer, Point3 target, float dt);
    bool moveWithFormationVelocity(
        Swimmer& swimmer,
        Point3 desired_velocity,
        float dt);
    bool chooseEscortDetour(
        Swimmer& swimmer,
        Point3 desired_velocity,
        Point3 anchor);
    void simulateStep(float dt);
    void syncActor(Swimmer& swimmer);

    std::filesystem::path project_root_;
    std::vector<Swimmer> swimmers_;
    std::vector<AquariumPokemonActor> actors_;
    std::vector<AquariumTankRuntime> tanks_;
    std::vector<std::string> warnings_;
    std::size_t authored_tank_count_ = 0;
    std::size_t authored_warning_count_ = 0;
    double simulation_accumulator_seconds_ = 0.0;
    std::vector<AquariumCrowdBody> crowd_neighbours_scratch_;
    using BodyNavigationKey = std::tuple<std::string, float, float, float, bool, float, float>;
    std::map<BodyNavigationKey, std::shared_ptr<AquariumBodyNavigation>> body_navigation_cache_;
    AquariumMotionDiagnostics motion_diagnostics_;
    std::size_t navigation_cursor_ = 0;
    std::map<std::string, AquariumSchoolState> schools_;
};

} // namespace pr::gameplay::world3d::aquarium
