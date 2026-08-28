#include "gameplay/world3d/aquarium/AquariumConfig.hpp"
#include "gameplay/world3d/aquarium/AquariumNavigation.hpp"
#include "gameplay/world3d/aquarium/AquariumPokemonMetrics.hpp"
#include "gameplay/world3d/aquarium/AquariumSimulation.hpp"
#include "gameplay/world3d/aquarium/AquariumInspectionCamera.hpp"
#include "gameplay/world3d/aquarium/AquariumInspectionFacing.hpp"
#include "gameplay/world3d/camera/FreeCameraCapture.hpp"
#include "gameplay/world3d/interiors/InteriorFloorCutout.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace aquarium = pr::gameplay::world3d::aquarium;
namespace fs = std::filesystem;

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(float actual, float expected, float tolerance = 0.02f) {
    return std::abs(actual - expected) <= tolerance;
}

void advanceInspection(
    aquarium::AquariumInspectionCamera& inspection,
    pr::gameplay::world3d::camera::Gen4FollowCamera& camera,
    int frames = 120) {
    for (int frame = 0; frame < frames; ++frame) {
        inspection.update(1.0 / 60.0, camera);
    }
}

fs::path projectRoot() {
#if defined(PR_SOURCE_DIR)
    return fs::path(PR_SOURCE_DIR);
#else
    return fs::current_path();
#endif
}

void positionParsingRejectsSilentZeroes() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() /
        ("pokemon_resort_aquarium_config_" + std::to_string(nonce));
    struct Cleanup {
        fs::path root;
        ~Cleanup() { std::error_code ignored; fs::remove_all(root, ignored); }
    } cleanup{root};
    const fs::path config = root / "config/gameplay/world3d/aquariums.json";
    fs::create_directories(config.parent_path());
    const auto writeConfig = [&](const char* x) {
        std::ofstream stream(config, std::ios::trunc);
        stream << "{\"maps\":[{\"mapId\":\"test\",\"tanks\":[{"
                  "\"placementId\":\"tank\",\"navigation\":\"tank.json\","
                  "\"pokemon\":[{\"species\":\"staryu\",\"positionMeters\":{"
                  "\"x\":" << x << ",\"y\":\"0.25\",\"z\":\"-1.5\"}}]}]}]}";
    };

    writeConfig("\"not-a-coordinate\"");
    std::string error;
    aquarium::loadAquariumCatalog(root.string(), &error);
    require(!error.empty(), "invalid position text must reject the edited config");

    writeConfig("\"1.25\"");
    const aquarium::AquariumCatalog catalog = aquarium::loadAquariumCatalog(root.string(), &error);
    require(error.empty() && catalog.maps.size() == 1U &&
            catalog.maps[0].tanks.size() == 1U &&
            catalog.maps[0].tanks[0].pokemon.size() == 1U,
        "numeric-string position config must remain compatible");
    const auto& position = catalog.maps[0].tanks[0].pokemon[0].starting_position_meters;
    require(near(position[0], 1.25f) && near(position[1], 0.25f) &&
            near(position[2], -1.5f),
        "position object must preserve all three authored coordinates");
}

void inspectionFacingRestoresEveryApproachDirection() {
    const std::array<pr::gameplay::world3d::FacingDirection, 4> directions{{
        pr::gameplay::world3d::FacingDirection::North,
        pr::gameplay::world3d::FacingDirection::South,
        pr::gameplay::world3d::FacingDirection::East,
        pr::gameplay::world3d::FacingDirection::West,
    }};
    for (const auto direction : directions) {
        aquarium::AquariumInspectionFacing facing;
        require(facing.begin(direction) == pr::gameplay::world3d::FacingDirection::North,
            "tank inspection must always present the player sprite facing north");
        require(facing.active(), "tank inspection facing override must remain active");
        const auto restored = facing.end();
        require(restored && *restored == direction,
            "leaving tank inspection must restore the exact approach direction");
        require(!facing.active() && !facing.end(),
            "restoring tank inspection facing must be safe on every exit path");
    }
}

void navigationHonorsUndergroundLayersAndHoles() {
    std::string error;
    const aquarium::AquariumNavigation navigation = aquarium::loadAquariumNavigation(
        (projectRoot() / "assets/overworld/models/aquarium/aquarium.navigation.json").string(), &error);
    require(navigation.valid, "aquarium navigation fixture must load");
    require(navigation.layers.size() == 3U, "all three exported swim layers must be retained");
    require(aquarium::containsPoint(navigation, {0.0f, -0.8f, 0.0f}),
        "underground water must be navigable");
    require(!aquarium::containsPoint(navigation, {2.5f, -0.8f, 0.5f}),
        "lower-layer obstacle holes must remain blocked");
}

void configuredDewgongMovesInsidePlacedTank() {
    pr::gameplay::world3d::SceneConfig scene;
    scene.id = "aquarium12";
    pr::gameplay::world3d::ModelPlacementConfig tank;
    tank.id = "aquarium";
    tank.x = 120.0f;
    tank.y = 0.0f;
    tank.z = 96.0f;
    tank.yaw_deg = 0.0f;
    tank.scale = 1.0f;
    scene.models.push_back(tank);
    pr::gameplay::world3d::ModelPlacementConfig cylinder;
    cylinder.id = "aquarium_c";
    cylinder.x = 296.0f;
    cylinder.y = 0.0f;
    cylinder.z = 88.0f;
    cylinder.scale = 1.0f;
    scene.models.push_back(cylinder);
    pr::gameplay::world3d::ModelPlacementConfig touch_pool;
    touch_pool.id = "aquarium_t";
    touch_pool.x = 104.0f;
    touch_pool.y = 0.0f;
    touch_pool.z = 216.0f;
    touch_pool.scale = 1.0f;
    scene.models.push_back(touch_pool);

    const aquarium::AquariumCatalog catalog = aquarium::loadAquariumCatalog(projectRoot().string());
    const aquarium::AquariumMapConfig* aquarium_map =
        aquarium::aquariumMapConfig(catalog, scene.id);
    require(aquarium_map && aquarium_map->tanks.size() == 3U,
        "aquarium12 camera/population config must load all three tanks");
    require(near(aquarium_map->pokemon_presentation.brightness, 1.08f) &&
            near(aquarium_map->pokemon_presentation.pokemon_brightness, 1.02f) &&
            near(aquarium_map->pokemon_presentation.ambient, 0.74f) &&
            near(aquarium_map->pokemon_presentation.directional, 0.34f) &&
            near(aquarium_map->pokemon_presentation.form_shadow, 0.24f) &&
            near(aquarium_map->pokemon_presentation.light_direction[2], 0.45f) &&
            near(aquarium_map->pokemon_presentation.tint[1], 0.99f),
        "aquarium Pokemon presentation must reproduce Attend's clear-scene lighting");
    require(near(aquarium_map->tanks[0].inspection_camera.focused_standoff_tiles, 8.7166932f) &&
            near(aquarium_map->tanks[0].inspection_camera.focused_near_clip, 12.0f) &&
            near(aquarium_map->tanks[0].inspection_camera.focused_wall_clip_radius_tiles, 1.5f) &&
            near(aquarium_map->tanks[0].inspection_camera.smooth, 640.0f) &&
            near(aquarium_map->tanks[0].inspection_camera.return_smooth, 2400.0f) &&
            aquarium_map->tanks[0].inspection_camera.has_framed_inspection_view &&
            near(aquarium_map->tanks[0].inspection_camera.inspection_behind_player_tiles, 11.0f),
        "focused view distance, clipping, and transition speed must remain data-driven");
    aquarium::AquariumSimulation simulation(
        projectRoot(), scene, aquarium_map);
    require(simulation.active(), "aquarium12 must instantiate its configured Pokemon");
    if (simulation.actors().size() != 9U) {
        for (const std::string& warning : simulation.warnings()) {
            std::cerr << "aquarium warning: " << warning << '\n';
        }
    }
    require(simulation.actors().size() == 9U,
        "three tanks must receive Dewgong, Clamperl, the school, Staryu, and Pyukumuku");
    require(simulation.actors().front().species == "dewgong", "Pokemon must resolve by species name");
    require(std::abs(simulation.actors().front().model_scale - aquarium_map->pokemon_scale) < 0.0001f,
        "one global scale must control every aquarium Pokemon");
    require(simulation.actors().front().animation == "idle_default",
        "Dewgong must use its authored idle animation while swimming");
    require(near(simulation.actors().front().presentation.brightness, 1.08f) &&
            near(simulation.actors().front().presentation.pokemon_brightness, 1.02f) &&
            near(simulation.actors().front().presentation.light_direction[2], 0.45f),
        "every aquarium actor must carry the scoped Attend presentation values");
    require(fs::path(simulation.actors().front().model_path).extension() == ".glbz",
        "Dewgong must use the Attend compiled model");
    const auto initial = simulation.actors().front().world_position;
    bool moved = false;
    bool visited_below_floor = initial[1] < 0.0f;
    const auto dewgong_metrics = aquarium::measureAquariumPokemon(
        simulation.actors().front().model_path, simulation.actors().front().form);
    require(dewgong_metrics.valid, "Dewgong rendered bounds must be measurable");
    for (int frame = 0; frame < 60 * 180; ++frame) {
        simulation.update(1.0 / 60.0);
        const auto& actor = simulation.actors().front();
        const float dx = actor.world_position[0] - initial[0];
        const float dy = actor.world_position[1] - initial[1];
        const float dz = actor.world_position[2] - initial[2];
        moved = moved || std::sqrt(dx * dx + dy * dy + dz * dz) > 2.0f;
        visited_below_floor = visited_below_floor || actor.world_position[1] < -0.1f;
        const float horizontal_extent = std::max({
            std::abs(dewgong_metrics.min_x), std::abs(dewgong_metrics.max_x),
            std::abs(dewgong_metrics.min_z), std::abs(dewgong_metrics.max_z)}) *
            actor.model_scale;
        require(std::abs(actor.world_position[0] - simulation.tanks()[0].world_center[0]) +
                    horizontal_extent <= simulation.tanks()[0].half_width_world + 0.01f &&
                std::abs(actor.world_position[2] - simulation.tanks()[0].world_center[2]) +
                    horizontal_extent <= simulation.tanks()[0].half_depth_world + 0.01f,
            "scaled Dewgong geometry must remain inside the tank footprint");
    }
    require(moved, "configured aquarium Pokemon must navigate continuously");
    require(visited_below_floor, "swimmer must be able to use the underground tank section");
    const auto clamperl_initial = simulation.actors()[1].world_position;
    const double clamperl_animation_time = simulation.actors()[1].animation_time_seconds;
    simulation.update(1.0);
    require(simulation.actors()[1].species == "clamperl", "cylinder tank must resolve Clamperl");
    require(simulation.actors()[1].world_position == clamperl_initial,
        "zero-speed Clamperl must remain stationary");
    require(simulation.actors()[1].animation_time_seconds > clamperl_animation_time,
        "stationary Clamperl must keep sampling its idle animation");
    const auto clamperl_metrics = aquarium::measureAquariumPokemon(
        simulation.actors()[1].model_path, simulation.actors()[1].form);
    const float rendered_clamperl_bottom = clamperl_initial[1] +
        clamperl_metrics.min_y * simulation.actors()[1].model_scale;
    require(std::abs(
            rendered_clamperl_bottom - simulation.tanks()[1].water_bottom_world) < 0.3f,
        "bottom anchor must rest Clamperl's rendered geometry on the water floor");

    float minimum_school_distance = 100000.0f;
    float school_min_y = 100000.0f;
    float school_max_y = -100000.0f;
    constexpr std::size_t kWishiwashiEnd = 7U;
    for (std::size_t i = 2; i < kWishiwashiEnd; ++i) {
        const auto& fish = simulation.actors()[i];
        require(fish.species == "wishiwashi" && fish.animation == "idle_default",
            "the cylinder school must use five idle-animated Wishiwashi");
        school_min_y = std::min(school_min_y, fish.world_position[1]);
        school_max_y = std::max(school_max_y, fish.world_position[1]);
        for (std::size_t j = i + 1; j < kWishiwashiEnd; ++j) {
            const auto& other = simulation.actors()[j];
            const float dx = fish.world_position[0] - other.world_position[0];
            const float dy = fish.world_position[1] - other.world_position[1];
            const float dz = fish.world_position[2] - other.world_position[2];
            minimum_school_distance = std::min(
                minimum_school_distance, std::sqrt(dx * dx + dy * dy + dz * dz));
        }
    }
    require(minimum_school_distance > 0.1f,
        "school separation must prevent Wishiwashi from overlapping");
    require(school_max_y - school_min_y > 0.1f,
        "school formation must include vertical motion instead of a straight line");

    const auto staryu_left_initial = simulation.actors()[7].world_position;
    const auto pyukumuku_initial = simulation.actors()[8].world_position;
    require(simulation.actors()[7].species == "staryu" &&
            simulation.actors()[7].animation == "idle_default" &&
            near(simulation.actors()[7].model_scale, aquarium_map->pokemon_scale, 0.0001f) &&
            near(simulation.actors()[7].world_pitch_degrees, -90.0f),
        "touch pool Staryu must face upward, idle, and retain the shared Pokemon scale");
    const auto staryu_metrics = aquarium::measureAquariumPokemon(
        simulation.actors()[7].model_path, simulation.actors()[7].form);
    const auto flat_staryu = aquarium::rotateAquariumPokemonMetrics(staryu_metrics, -90.0f);
    const float rendered_staryu_bottom = staryu_left_initial[1] +
        flat_staryu.min_y * simulation.actors()[7].model_scale;
    require(simulation.actors()[8].species == "pyukumuku" &&
            simulation.actors()[8].animation == "walk",
        "touch pool Pyukumuku must use the walking animation while wandering");
    std::string touch_navigation_error;
    const aquarium::AquariumNavigation touch_navigation = aquarium::loadAquariumNavigation(
        (projectRoot() / "assets/overworld/models/aquarium_t/aquarium_T.navigation.json").string(),
        &touch_navigation_error);
    require(touch_navigation.valid, "touch pool navigation fixture must load");
    require(near(touch_navigation.floor_level_y, 0.0f),
        "touch pool must preserve Aquarium Maker's physical floor height");
    require(near(rendered_staryu_bottom,
            touch_pool.y + (touch_navigation.floor_level_y + 0.2f) * 16.0f, 0.3f),
        "face-up Staryu must retain its authored clearance above the physical floor");
    require(near((staryu_left_initial[0] - touch_pool.x) / 16.0f, -1.15f) &&
            near((staryu_left_initial[2] - touch_pool.z) / 16.0f, -1.15f),
        "stationary Staryu must sit clear of both the glass and center rock");
    bool pyukumuku_moved = false;
    for (int frame = 0; frame < 60 * 90; ++frame) {
        simulation.update(1.0 / 60.0);
        const auto& pyukumuku = simulation.actors()[8];
        const aquarium::Point3 local{
            (pyukumuku.world_position[0] - touch_pool.x) / 16.0f,
            0.68f,
            (pyukumuku.world_position[2] - touch_pool.z) / 16.0f};
        require(aquarium::containsPoint(touch_navigation, local),
            "wandering Pyukumuku must stay in the touch pool and avoid its center rock");
        const float dx = pyukumuku.world_position[0] - pyukumuku_initial[0];
        const float dz = pyukumuku.world_position[2] - pyukumuku_initial[2];
        pyukumuku_moved = pyukumuku_moved || std::sqrt(dx * dx + dz * dz) > 1.0f;
    }
    require(pyukumuku_moved, "touch pool Pyukumuku must continuously walk around the rock");
    require(simulation.actors()[7].world_position == staryu_left_initial,
        "touch pool Staryu must remain fixed on the floor");

    aquarium::AquariumPokemonMetrics unit_metrics;
    unit_metrics.min_x = -1.0f;
    unit_metrics.max_x = 1.0f;
    unit_metrics.min_y = 0.0f;
    unit_metrics.max_y = 4.0f;
    unit_metrics.min_z = -2.0f;
    unit_metrics.max_z = 2.0f;
    unit_metrics.valid = true;
    const auto laid_flat = aquarium::rotateAquariumPokemonMetrics(unit_metrics, -90.0f);
    require(near(laid_flat.min_y, -2.0f) && near(laid_flat.max_y, 2.0f) &&
            near(laid_flat.min_z, -4.0f) && near(laid_flat.max_z, 0.0f),
        "pitched floor actors must use rotated rendered bounds for placement and clearance");

    aquarium::AquariumInspectionCamera inspection(simulation.tanks());
    pr::gameplay::world3d::camera::Gen4FollowCamera camera(
        pr::gameplay::world3d::camera::Gen4CameraPreset{});
    const pr::gameplay::world3d::camera::Vec3 player_position{120.0f, 0.0f, 144.0f};
    camera.setTarget(player_position);
    const auto follow_pose = camera.pose();
    require(inspection.tryBegin(
        player_position, pr::gameplay::world3d::FacingDirection::North,
        16.0f, camera), "facing a nearby tank must start inspection");
    advanceInspection(inspection, camera);
    require(inspection.activePlacementId() == "aquarium",
        "inspection must select the tank hit by the player's facing ray");
    const auto pose = camera.pose();
    require(near(pose.position.x, 120.0f) && near(pose.position.y, 82.46915f) &&
            near(pose.position.z, 320.0f) && pose.forward.z < 0.0f,
        "first inspection stage must use its explicit behind-player framing");
    const auto distance_to_player = [&](const auto& position) {
        const float dx = position.x - player_position.x;
        const float dy = position.y - player_position.y;
        const float dz = position.z - player_position.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    };
    require(pose.position.y < follow_pose.position.y &&
            distance_to_player(pose.position) < distance_to_player(follow_pose.position),
        "inspection must lower and move the valid follow camera closer");
    float screen_x = 0.0f;
    float screen_y = 0.0f;
    float depth = 0.0f;
    require(camera.worldToScreen(
            {120.0f, 14.4f, 96.0f}, 640, 480, screen_x, screen_y, depth) &&
            depth > pose.preset.near_clip,
        "focused tank must remain beyond the camera near clip instead of rendering black");
    require(inspection.enterFocused(16.0f, camera),
        "a second accept must enter the actor-free focused stage");
    require(inspection.focused() && inspection.hidesOverworldActors(),
        "focused stage must report that overworld actors should be hidden");
    require(near(inspection.wallClipRadiusWorld(16.0f), 24.0f),
        "focused stage must expose its wall-only camera clipping radius in world units");
    advanceInspection(inspection, camera);
    require(near(camera.pose().preset.near_clip, 12.0f),
        "focused stage must apply only its authored close-view near clip");
    inspection.beginExit(player_position, camera);
    require(inspection.returning(), "north-facing inspection must return smoothly");
    require(!inspection.hidesOverworldActors(),
        "overworld sprites must reappear as soon as focused inspection exits");
    const pr::gameplay::world3d::camera::Vec3 moving_player{136.0f, 0.0f, 144.0f};
    inspection.updateReturnTarget(moving_player);
    advanceInspection(inspection, camera, 18);
    require(!inspection.active() && near(camera.pose().preset.near_clip, 150.0f),
        "fast smooth return must restore follow mode and its original near clip");
    pr::gameplay::world3d::camera::Gen4FollowCamera expected_return_camera(
        pr::gameplay::world3d::camera::Gen4CameraPreset{});
    expected_return_camera.setTarget(moving_player);
    require(near(camera.pose().position.x, expected_return_camera.pose().position.x) &&
            near(camera.pose().position.y, expected_return_camera.pose().position.y) &&
            near(camera.pose().position.z, expected_return_camera.pose().position.z),
        "returning camera must rejoin follow mode at the moving player's latest position");

    camera.setTarget({296.0f, -100.0f, 120.0f});
    require(inspection.tryBegin(
        {296.0f, -100.0f, 120.0f}, pr::gameplay::world3d::FacingDirection::North,
        16.0f, camera), "cylinder inspection must tolerate a bad below-floor player sample");
    advanceInspection(inspection, camera);
    require(camera.pose().position.y > simulation.tanks()[1].floor_y_world,
        "inspection camera must never inherit a below-floor terrain sample");
    require(inspection.enterFocused(16.0f, camera),
        "cylinder must enter the captured north focus view");
    advanceInspection(inspection, camera);
    const auto cylinder_focus = camera.pose();
    require(near(cylinder_focus.position.x, 296.0f) &&
            near(cylinder_focus.position.y, 66.4691467f) &&
            near(cylinder_focus.position.z, 252.10233f),
        "cylinder north focus must reproduce the centered captured framing");
    const float cylinder_yaw = std::atan2(
        cylinder_focus.forward.x, cylinder_focus.forward.z) * 180.0f / 3.14159265358979323846f;
    const float cylinder_pitch = std::asin(cylinder_focus.forward.y) *
        180.0f / 3.14159265358979323846f;
    require(near(std::abs(cylinder_yaw), 180.0f, 0.02f) &&
            near(cylinder_pitch, -10.4799919f, 0.02f),
        "cylinder focus must reproduce the captured north yaw and pitch");

    inspection.beginExit({296.0f, -100.0f, 120.0f}, camera);
    advanceInspection(inspection, camera);
    camera.setTarget({208.0f, 0.0f, 112.0f});
    require(inspection.tryBegin(
        {208.0f, 0.0f, 112.0f}, pr::gameplay::world3d::FacingDirection::West,
        16.0f, camera), "main tank must be inspectable from its east side");
    inspection.update(0.0, camera);
    const auto side_inspection = camera.pose();
    require(near(side_inspection.position.x, 384.0f) &&
            near(side_inspection.position.y, 70.8869f) &&
            near(side_inspection.position.z, 112.0f),
        "side inspection must snap directly behind the player before close focus");
    require(inspection.enterFocused(16.0f, camera),
        "side approach must enter the captured focus view");
    inspection.update(1.0 / 60.0, camera);
    const auto side_focus = camera.pose();
    require(near(side_focus.position.x, 336.6991f) &&
            near(side_focus.position.y, 54.8869057f) &&
            near(side_focus.position.z, 96.0f),
        "west-facing focus must use captured distance/height and remove accidental lateral offset");
    const float side_yaw = std::atan2(
        side_focus.forward.x, side_focus.forward.z) * 180.0f / 3.14159265358979323846f;
    const float side_pitch = std::asin(side_focus.forward.y) *
        180.0f / 3.14159265358979323846f;
    require(near(side_yaw, -90.0f, 0.02f) && near(side_pitch, -10.7199888f, 0.02f),
        "west-facing focus must center a clean side-on view");
    inspection.beginExit({208.0f, 0.0f, 112.0f}, camera);
    require(!inspection.active() && near(camera.pose().preset.near_clip, 150.0f),
        "side-facing inspection must return instantly and restore the near clip");

    const auto& touch_tank = simulation.tanks()[2];
    aquarium::AquariumInspectionCamera touch_inspection(simulation.tanks());
    const pr::gameplay::world3d::camera::Vec3 touch_player{
        touch_tank.world_center[0],
        touch_tank.floor_y_world,
        touch_tank.world_center[2] + touch_tank.half_depth_world + 8.0f};
    camera.setTarget(touch_player);
    require(touch_inspection.tryBegin(
            touch_player, pr::gameplay::world3d::FacingDirection::North,
            16.0f, camera),
        "touch pool must be inspectable from its south edge");
    require(touch_inspection.enterFocused(16.0f, camera),
        "touch pool must enter its second focused stage");
    advanceInspection(touch_inspection, camera);
    const auto touch_focus = camera.pose();
    const pr::gameplay::world3d::camera::Vec3 touch_target{
        touch_tank.world_center[0],
        (touch_tank.water_bottom_world + touch_tank.water_top_world) * 0.5f,
        touch_tank.world_center[2]};
    const float target_dx = touch_target.x - touch_focus.position.x;
    const float target_dy = touch_target.y - touch_focus.position.y;
    const float target_dz = touch_target.z - touch_focus.position.z;
    const float target_distance = std::sqrt(
        target_dx * target_dx + target_dy * target_dy + target_dz * target_dz);
    const float target_alignment =
        (touch_focus.forward.x * target_dx + touch_focus.forward.y * target_dy +
            touch_focus.forward.z * target_dz) / target_distance;
    require(target_alignment > 0.999f && touch_focus.forward.y < -0.2f,
        "touch-pool focus must look down at the shallow water center, not into the distance");
}

void focusViewSupportsEveryTankFace() {
    aquarium::AquariumTankRuntime tank;
    tank.placement_id = "four_faces";
    tank.world_center = {0.0f, 0.0f, 0.0f};
    tank.half_width_world = 20.0f;
    tank.half_depth_world = 10.0f;
    tank.units_per_meter_world = 16.0f;
    tank.water_bottom_world = -16.0f;
    tank.water_top_world = 64.0f;
    tank.inspection_camera.smooth = 0.0f;
    tank.inspection_camera.interaction_reach_tiles = 2.0f;
    tank.inspection_camera.focused_standoff_tiles = 10.0f;

    struct Case {
        pr::gameplay::world3d::FacingDirection facing;
        pr::gameplay::world3d::camera::Vec3 player;
        pr::gameplay::world3d::camera::Vec3 expected_camera;
    };
    const std::array<Case, 4> cases{{
        {pr::gameplay::world3d::FacingDirection::North, {0.0f, 0.0f, 20.0f}, {0.0f, 66.4f, 170.0f}},
        {pr::gameplay::world3d::FacingDirection::South, {0.0f, 0.0f, -20.0f}, {0.0f, 66.4f, -170.0f}},
        {pr::gameplay::world3d::FacingDirection::East, {-30.0f, 0.0f, 0.0f}, {-180.0f, 54.88f, 0.0f}},
        {pr::gameplay::world3d::FacingDirection::West, {30.0f, 0.0f, 0.0f}, {180.0f, 54.88f, 0.0f}}
    }};
    for (const Case& test : cases) {
        aquarium::AquariumInspectionCamera inspection({tank});
        pr::gameplay::world3d::camera::Gen4FollowCamera camera(
            pr::gameplay::world3d::camera::Gen4CameraPreset{});
        camera.setTarget(test.player);
        require(inspection.tryBegin(test.player, test.facing, 16.0f, camera),
            "all four cardinal tank faces must be interactable");
        require(inspection.enterFocused(16.0f, camera),
            "all four cardinal tank faces must have a generated focus view");
        inspection.update(0.0, camera);
        const auto position = camera.pose().position;
        require(near(position.x, test.expected_camera.x) &&
                near(position.y, test.expected_camera.y) &&
                near(position.z, test.expected_camera.z),
            "focus camera must be centered outside the approached tank face");
    }
}

void polygonCutoutPreservesPartialFloorCells() {
    pr::gameplay::world3d::SceneConfig scene;
    scene.grid.tile_size = 16.0f;
    pr::gameplay::world3d::ModelPlacementConfig placement;
    placement.id = "rounded_tank";
    placement.x = 8.0f;
    placement.z = 8.0f;
    scene.models.push_back(placement);
    pr::gameplay::world3d::InteriorFloorCutoutConfig cutout;
    cutout.placement_id = "rounded_tank";
    cutout.local_polygon = {{{-4.0f, -4.0f}, {4.0f, -4.0f},
        {4.0f, 4.0f}, {-4.0f, 4.0f}}};
    scene.interior.floor_cutouts.push_back(cutout);
    const auto triangles = pr::gameplay::world3d::interiors::clipFloorCellAgainstCutouts(
        scene, 0, 0, 16.0f);
    float area = 0.0f;
    for (const auto& triangle : triangles) {
        area += std::abs(
            (triangle[1].x - triangle[0].x) * (triangle[2].z - triangle[0].z) -
            (triangle[2].x - triangle[0].x) * (triangle[1].z - triangle[0].z)) * 0.5f;
        for (const auto& vertex : triangle) {
            require(vertex.u >= -0.0001f && vertex.u <= 1.0001f &&
                vertex.v >= -0.0001f && vertex.v <= 1.0001f,
                "clipped floor UVs must remain in the original tile range");
        }
    }
    require(std::abs(area - 192.0f) < 0.01f,
        "polygon installation must remove only its exact 8x8 area from a 16x16 floor cell");
}

void freeCameraCaptureIsPasteReady() {
    const std::string capture =
        pr::gameplay::world3d::camera::formatFreeCameraCapture({
            "aquarium12",
            "aquarium",
            {1.25f, -2.5f, 3.75f},
            42.5f,
            -17.25f,
            {112.0f, 0.0f, 136.0f},
            pr::gameplay::world3d::FacingDirection::North});
    require(capture ==
        "{\"mapId\":\"aquarium12\",\"nearestPlacementId\":\"aquarium\","
        "\"cameraPosition\":[1.25,-2.5,3.75],\"yawDegrees\":42.5,"
        "\"pitchDegrees\":-17.25,\"playerPosition\":[112,0,136],"
        "\"playerFacing\":\"north\"}",
        "free-camera capture must be stable paste-ready JSON");
}

} // namespace

int main() {
    try {
        positionParsingRejectsSilentZeroes();
        inspectionFacingRestoresEveryApproachDirection();
        navigationHonorsUndergroundLayersAndHoles();
        configuredDewgongMovesInsidePlacedTank();
        focusViewSupportsEveryTankFace();
        polygonCutoutPreservesPartialFloorCells();
        freeCameraCaptureIsPasteReady();
        std::cout << "aquarium_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "aquarium_tests: " << error.what() << '\n';
        return 1;
    }
}
