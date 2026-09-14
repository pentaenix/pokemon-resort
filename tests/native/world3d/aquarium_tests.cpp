#include "gameplay/world3d/aquarium/AquariumConfig.hpp"
#include "gameplay/world3d/aquarium/AquariumCrowdSimulation.hpp"
#include "gameplay/world3d/aquarium/AquariumNavigation.hpp"
#include "gameplay/world3d/aquarium/AquariumPokemonMetrics.hpp"
#include "gameplay/world3d/aquarium/AquariumSimulation.hpp"
#include "gameplay/world3d/aquarium/AquariumInspectionCamera.hpp"
#include "gameplay/world3d/aquarium/AquariumInspectionFacing.hpp"
#include "gameplay/attend/PokemonModelCatalog.hpp"
#include "gameplay/attend/rendering/AttendPokemonModel.hpp"
#include "gameplay/world3d/aquarium/rendering/AquariumPokemonRuntimeLod.hpp"
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

void runAquariumMotionTests();
void runAquariumSchoolTests();
void runAquariumEmissionTests();

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
        (projectRoot() / "tests/fixtures/legacy_aquarium/aquarium.navigation.json").string(), &error);
    require(navigation.valid, "aquarium navigation fixture must load");
    require(navigation.layers.size() == 3U, "all three exported swim layers must be retained");
    require(aquarium::containsPoint(navigation, {0.0f, -0.8f, 0.0f}),
        "underground water must be navigable");
    require(!aquarium::containsPoint(navigation, {2.5f, -0.8f, 0.5f}),
        "lower-layer obstacle holes must remain blocked");
}

void aquariumRuntimeLodPreservesAnimationAndReducesGeometry() {
    namespace attend = pr::gameplay::attend::rendering;
    namespace aquarium_rendering = pr::gameplay::world3d::aquarium::rendering;
    const auto verify = [&](const char* filename, const char* animation) {
        std::string error;
        const auto source = attend::loadAttendPokemonModel(
            (projectRoot() / "assets/pokemon_attend/pokemon_models" / filename).string(),
            &error);
        require(source.valid, "source aquarium Pokemon model must load");
        const auto lod = aquarium_rendering::buildAquariumPokemonRuntimeLod(source);
        require(lod.replacements.size() == source.primitives.size(),
            "runtime LOD must preserve the source primitive layout");
        require(lod.statistics.render_triangles > 0U &&
                lod.statistics.render_triangles < lod.statistics.source_triangles,
            "runtime LOD must reduce source triangles conservatively");
        require(attend::findAttendPokemonAnimation(source, animation) != nullptr,
            "source model lost a required swimming animation");
        for (std::size_t i = 0; i < lod.replacements.size(); ++i) {
            if (!lod.replacements[i]) continue;
            const auto& reduced = *lod.replacements[i];
            const auto& original = source.primitives[i];
            require(reduced.material == original.material &&
                    reduced.skin == original.skin &&
                    reduced.mesh_node == original.mesh_node,
                "runtime LOD must preserve material and skin bindings");
            require(std::all_of(reduced.indices.begin(), reduced.indices.end(), [&](auto index) {
                return index < reduced.vertices.size();
            }), "runtime LOD emitted an invalid vertex index");
        }
    };
    verify("pm0350_00_Milotic.glbz", "slot6_02");
    verify("pm0382_00_Kyogre.glbz", "slot4_00");
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

    const aquarium::AquariumCatalog catalog = aquarium::loadAquariumCatalog(
        (projectRoot() / "tests/fixtures/legacy_aquarium").string());
    const aquarium::AquariumMapConfig* aquarium_map =
        aquarium::aquariumMapConfig(catalog, scene.id);
    require(aquarium_map && aquarium_map->tanks.size() == 3U,
        "aquarium12 camera/population config must load all three tanks");
    require(aquarium_map->construction.enabled &&
            aquarium_map->construction.allowed_cells.size() == 220U &&
            aquarium_map->construction.has_return_cell &&
            aquarium_map->construction.return_cell.column == 12 &&
            aquarium_map->construction.return_cell.row == 16 &&
            aquarium_map->construction.return_facing == "south",
        "aquarium12 must expose its audited room-wide construction mask");
    const auto allows_construction = [&](int column, int row) {
        return std::any_of(aquarium_map->construction.allowed_cells.begin(),
            aquarium_map->construction.allowed_cells.end(), [&](const auto& cell) {
                return cell.column == column && cell.row == row;
            });
    };
    require(allows_construction(22, 16) && allows_construction(1, 1) &&
            !allows_construction(12, 10) && !allows_construction(4, 11) &&
            !allows_construction(17, 4),
        "room-wide construction must preserve entrance circulation and authored tanks");
    const aquarium::AquariumMapConfig* builder_lab =
        aquarium::aquariumMapConfig(catalog, "aquarium_builder_lab");
    require(builder_lab && builder_lab->tanks.empty() &&
            builder_lab->construction.enabled &&
            builder_lab->construction.allowed_cells.size() == 340U &&
            builder_lab->construction.has_return_cell &&
            builder_lab->construction.return_cell.column == 12 &&
            builder_lab->construction.return_cell.row == 16 &&
            builder_lab->construction.return_facing == "north" &&
            builder_lab->construction.has_room_trim_color &&
            builder_lab->construction.room_trim_color[0] == 67 &&
            builder_lab->construction.room_trim_color[1] == 105 &&
            builder_lab->construction.room_trim_color[2] == 148 &&
            builder_lab->construction.room_trim_color[3] == 255 &&
            !aquarium_map->construction.has_room_trim_color,
        "builder lab must expose a separate empty full-room construction surface");
    require(near(builder_lab->building_presentation.camera.far_clip_tiles, 128.0f) &&
            near(aquarium_map->building_presentation.camera.far_clip_tiles, 0.0f),
        "extended camera depth must remain local to the Builder Lab");
    require(aquarium_map->building_presentation.camera.enabled &&
            near(aquarium_map->building_presentation.camera.distance_behind_player_tiles,
                catalog.building_presentation.camera.distance_behind_player_tiles) &&
            near(aquarium_map->building_presentation.camera.height_above_player_tiles,
                catalog.building_presentation.camera.height_above_player_tiles) &&
            near(aquarium_map->building_presentation.camera.near_clip_tiles,
                catalog.building_presentation.camera.near_clip_tiles) &&
            aquarium_map->building_presentation.lighting.enabled &&
            near(aquarium_map->building_presentation.lighting.brightness,
                catalog.building_presentation.lighting.brightness) &&
            near(aquarium_map->building_presentation.lighting.tint[0],
                catalog.building_presentation.lighting.tint[0]) &&
            near(aquarium_map->building_presentation.lighting.tint[2],
                catalog.building_presentation.lighting.tint[2]) &&
            aquarium_map->building_presentation.tank_lighting.enabled &&
            near(aquarium_map->building_presentation.tank_lighting.brightness,
                catalog.building_presentation.tank_lighting.brightness) &&
            near(aquarium_map->building_presentation.tank_lighting.spill_opacity,
                catalog.building_presentation.tank_lighting.spill_opacity) &&
            near(aquarium_map->building_presentation.tank_lighting.spill_reach_tiles,
                catalog.building_presentation.tank_lighting.spill_reach_tiles) &&
            near(aquarium_map->building_presentation.tank_lighting.water_attenuation_intensity,
                catalog.building_presentation.tank_lighting.water_attenuation_intensity) &&
            near(aquarium_map->building_presentation.tank_lighting.water_surface_speed,
                catalog.building_presentation.tank_lighting.water_surface_speed) &&
            near(aquarium_map->building_presentation.tank_lighting.sand_darkening,
                catalog.building_presentation.tank_lighting.sand_darkening) &&
            near(catalog.building_presentation.tank_lighting.water_attenuation_intensity, 2.4f) &&
            near(catalog.building_presentation.tank_lighting.water_surface_speed, 0.7f) &&
            near(catalog.building_presentation.tank_lighting.sand_darkening, 0.18f) &&
            builder_lab->building_presentation.camera.enabled &&
            near(builder_lab->building_presentation.camera.distance_behind_player_tiles,
                catalog.building_presentation.camera.distance_behind_player_tiles) &&
            near(builder_lab->building_presentation.camera.height_above_player_tiles,
                catalog.building_presentation.camera.height_above_player_tiles) &&
            builder_lab->building_presentation.lighting.enabled &&
            near(builder_lab->building_presentation.lighting.brightness,
                catalog.building_presentation.lighting.brightness),
        "aquarium maps must inherit the aquarium-only building presentation");
    pr::gameplay::world3d::camera::Gen4CameraPreset base_camera;
    const auto aquarium_camera = aquarium::aquariumBuildingCameraPreset(
        base_camera, aquarium_map->building_presentation.camera, 16.0f);
    const auto builder_camera = aquarium::aquariumBuildingCameraPreset(
        base_camera, builder_lab->building_presentation.camera, 16.0f);
    const float camera_horizontal =
        aquarium_map->building_presentation.camera.distance_behind_player_tiles * 16.0f;
    const float camera_vertical =
        aquarium_map->building_presentation.camera.height_above_player_tiles * 16.0f;
    constexpr float radians_to_degrees = 57.29577951308232f;
    require(near(aquarium_camera.distance,
                std::hypot(camera_horizontal, camera_vertical)) &&
            near(aquarium_camera.pitch_deg,
                -std::atan2(camera_vertical, camera_horizontal) * radians_to_degrees) &&
            near(aquarium_camera.near_clip, 8.0f) &&
            near(builder_camera.near_clip, 8.0f) &&
            near(aquarium_camera.far_clip, base_camera.far_clip) &&
            near(builder_camera.far_clip, 2048.0f),
        "aquarium camera framing and local clip controls did not derive the expected view");
    const auto lab_allows_construction = [&](int column, int row) {
        return std::any_of(builder_lab->construction.allowed_cells.begin(),
            builder_lab->construction.allowed_cells.end(), [&](const auto& cell) {
                return cell.column == column && cell.row == row;
            });
    };
    require(lab_allows_construction(1, 1) && lab_allows_construction(22, 16) &&
            lab_allows_construction(12, 8) && !lab_allows_construction(12, 1) &&
            !lab_allows_construction(12, 16),
        "builder lab construction mask must keep both doorway circulation lanes clear");
    require(near(aquarium_map->pokemon_presentation.brightness,
                catalog.pokemon_presentation.brightness *
                    catalog.building_presentation.tank_lighting.brightness) &&
            near(aquarium_map->pokemon_presentation.pokemon_brightness, 1.02f) &&
            near(aquarium_map->pokemon_presentation.ambient, 0.74f) &&
            near(aquarium_map->pokemon_presentation.directional, 0.34f) &&
            near(aquarium_map->pokemon_presentation.form_shadow, 0.24f) &&
            near(aquarium_map->pokemon_presentation.light_direction[2], 0.45f) &&
            near(aquarium_map->pokemon_presentation.tint[0],
                catalog.pokemon_presentation.tint[0] *
                    catalog.building_presentation.tank_lighting.tint[0]) &&
            near(aquarium_map->pokemon_presentation.tint[1],
                catalog.pokemon_presentation.tint[1] *
                    catalog.building_presentation.tank_lighting.tint[1]) &&
            near(aquarium_map->pokemon_presentation.tint[2],
                catalog.pokemon_presentation.tint[2] *
                    catalog.building_presentation.tank_lighting.tint[2]),
        "aquarium Pokemon presentation must inherit the independent tank light");
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
    require(near(simulation.actors().front().presentation.brightness,
                aquarium_map->pokemon_presentation.brightness) &&
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
        (projectRoot() / "tests/fixtures/legacy_aquarium/aquarium_T.navigation.json").string(),
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
    require(inspection.focused() && !inspection.hidesOverworldActors(),
        "whole-tank focused stage must keep the player visible");
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
    require(near(cylinder_focus.position.x, 296.0f) && cylinder_focus.position.y>0 && cylinder_focus.position.z>120,
        "cylinder overview must remain centered outside the tank");
    const float cylinder_yaw = std::atan2(
        cylinder_focus.forward.x, cylinder_focus.forward.z) * 180.0f / 3.14159265358979323846f;
    const float cylinder_pitch = std::asin(cylinder_focus.forward.y) *
        180.0f / 3.14159265358979323846f;
    require(near(std::abs(cylinder_yaw), 180.0f, 0.02f) &&
            near(cylinder_pitch, -20.0f, 0.05f),
        "cylinder overview must preserve north approach and readable downward pitch");

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
    require(side_focus.position.x>208 && side_focus.position.y>0 && near(side_focus.position.z,96.0f),
        "west-facing overview must be outside the tank without lateral offset");
    const float side_yaw = std::atan2(
        side_focus.forward.x, side_focus.forward.z) * 180.0f / 3.14159265358979323846f;
    const float side_pitch = std::asin(side_focus.forward.y) *
        180.0f / 3.14159265358979323846f;
    require(near(side_yaw, -90.0f, 0.02f) && near(side_pitch, -20.0f, 0.05f),
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
    simulation.replacePlayerTanks({});
    require(simulation.actors().size() == 9U && simulation.tanks().size() == 3U,
        "replacing player populations disturbed authored aquarium simulation state");
}

void playerTankPopulationUsesRuntimeNavigation() {
    pr::gameplay::world3d::SceneConfig scene;
    aquarium::AquariumSimulation simulation(projectRoot(), scene, nullptr);
    const auto models = pr::gameplay::attend::discoverPokemonModels(
        projectRoot() / "assets/pokemon_attend/pokemon_models");
    const auto wishiwashi = std::find_if(models.begin(), models.end(), [](const auto& model) {
        return model.id == "wishiwashi";
    });
    require(wishiwashi != models.end(),
        "player navigation fixture requires the Wishiwashi Attend model");

    aquarium::AquariumNavigation navigation;
    navigation.export_units_per_meter = 16.0f;
    navigation.floor_level_y = 0.0f;
    navigation.valid = true;
    aquarium::SwimVolumeLayer layer;
    layer.id = "player-water";
    layer.y_bottom = -1.5f;
    layer.y_top = 2.5f;
    layer.polygons = {{{
        {-1.5f, -1.5f}, {1.5f, -1.5f}, {1.5f, 1.5f}, {-1.5f, 1.5f}},
        {{-0.18f, -0.65f}, {0.18f, -0.65f}, {0.18f, 0.65f}, {-0.18f, 0.65f}}}};
    navigation.layers.push_back(layer);
    navigation.suggested_spawns.push_back({-0.8f, -0.8f, 0.0f});

    aquarium::AquariumSwimmerDefinition swimmer;
    swimmer.actor.id = "player-tank:placeholder";
    swimmer.actor.species = "wishiwashi";
    swimmer.actor.form = "00";
    swimmer.actor.model_path = wishiwashi->path;
    swimmer.actor.animation = "idle_default";
    swimmer.actor.model_scale = 0.27f;
    swimmer.movement.id = swimmer.actor.id;
    swimmer.movement.species = swimmer.actor.species;
    swimmer.movement.behavior = "school";
    swimmer.movement.speed_meters_per_second = 0.34f;
    swimmer.movement.turn_degrees_per_second = 180.0f;
    swimmer.movement.body_radius_meters = 0.03f;
    swimmer.movement.has_starting_position = true;
    swimmer.movement.starting_position_meters = {-0.8f, -0.8f, 0.0f};
    swimmer.seed = 147U;
    swimmer.formation_index = 0;
    swimmer.formation_count = 1;

    aquarium::AquariumPlayerTankSimulationInput tank;
    tank.tank_id = "player-tank";
    tank.navigation = navigation;
    tank.world_origin = {200.0f, 0.0f, 160.0f};
    tank.inspection_camera.has_framed_inspection_view = true;
    tank.inspection_camera.interaction_reach_tiles = 1.5f;
    tank.inspection_camera.focused_standoff_tiles = 3.5f;
    tank.swimmers.push_back(swimmer);
    simulation.replacePlayerTanks({tank});
    require(simulation.actors().size() == 1U && simulation.tanks().size() == 1U,
        "player tank was not installed into the shared aquarium simulation");
    const auto initial = simulation.actors().front().world_position;
    require(initial[1] < 0.0f,
        "player swimmer ignored its generated below-floor spawn");

    aquarium::AquariumInspectionCamera inspection(simulation.tanks());
    pr::gameplay::world3d::camera::Gen4FollowCamera camera(
        pr::gameplay::world3d::camera::Gen4CameraPreset{});
    const pr::gameplay::world3d::camera::Vec3 player{
        tank.world_origin[0], tank.world_origin[1], tank.world_origin[2] + 32.0f};
    camera.setTarget(player);
    require(inspection.tryBegin(
            player, pr::gameplay::world3d::FacingDirection::North, 16.0f, camera) &&
            inspection.activePlacementId() == tank.tank_id &&
            inspection.enterFocused(16.0f, camera) && inspection.focused(),
        "player-built tank did not support the existing two-stage inspection zoom");

    bool moved = false;
    float five_second_motion = 0.0f;
    auto previous = initial;
    for (int frame = 0; frame < 60 * 30; ++frame) {
        simulation.update(1.0 / 60.0);
        const auto& actor = simulation.actors().front();
        const aquarium::Point3 local{
            (actor.world_position[0] - tank.world_origin[0]) / 16.0f,
            (actor.world_position[1] - tank.world_origin[1]) / 16.0f,
            (actor.world_position[2] - tank.world_origin[2]) / 16.0f};
        require(aquarium::containsPoint(navigation, local),
            "player swimmer escaped its generated navigation layers or entered a dry hole");
        const float dx = actor.world_position[0] - initial[0];
        const float dy = actor.world_position[1] - initial[1];
        const float dz = actor.world_position[2] - initial[2];
        moved = moved || std::sqrt(dx * dx + dy * dy + dz * dz) > 1.0f;
        const float frame_dx = actor.world_position[0] - previous[0];
        const float frame_dy = actor.world_position[1] - previous[1];
        const float frame_dz = actor.world_position[2] - previous[2];
        five_second_motion += std::sqrt(
            frame_dx * frame_dx + frame_dy * frame_dy + frame_dz * frame_dz);
        previous = actor.world_position;
        if ((frame + 1) % (60 * 5) == 0) {
            require(five_second_motion > 1.0f,
                "school swimmer became stuck against a dry tunnel or tank wall");
            five_second_motion = 0.0f;
        }
    }
    require(moved, "player-tank placeholder remained a static rendered actor");
    simulation.replacePlayerTanks({});
    require(simulation.actors().empty() && simulation.tanks().empty(),
        "replacing player tanks left stale simulated actors behind");
}

void unpositionedCrawlerRestsOnPlayerTankBottom() {
    pr::gameplay::world3d::SceneConfig scene;
    aquarium::AquariumSimulation simulation(projectRoot(), scene, nullptr);
    const auto models = pr::gameplay::attend::discoverPokemonModels(
        projectRoot() / "assets/pokemon_attend/pokemon_models");
    const auto model = std::find_if(models.begin(), models.end(), [](const auto& candidate) {
        return candidate.id == "wishiwashi";
    });
    require(model != models.end(), "crawler anchor fixture requires a measured Pokemon model");

    aquarium::AquariumNavigation navigation;
    navigation.export_units_per_meter = 16.0f;
    navigation.floor_level_y = 0.0f;
    navigation.valid = true;
    aquarium::SwimVolumeLayer layer;
    layer.id = "deep-player-water";
    layer.y_bottom = -2.0f;
    layer.y_top = 2.0f;
    layer.polygons = {{{
        {-2.0f, -2.0f}, {2.0f, -2.0f}, {2.0f, 2.0f}, {-2.0f, 2.0f}}}};
    navigation.layers.push_back(layer);
    navigation.suggested_spawns.push_back({0.0f, 0.0f, 0.0f});

    aquarium::AquariumSwimmerDefinition crawler;
    crawler.actor.id = "player-tank:crawler";
    crawler.actor.species = "crawler";
    crawler.actor.form = "00";
    crawler.actor.model_path = model->path;
    crawler.actor.animation = "idle_default";
    crawler.actor.model_scale = 0.14f;
    crawler.movement.id = crawler.actor.id;
    crawler.movement.species = crawler.actor.species;
    crawler.movement.behavior = "wander";
    crawler.movement.speed_meters_per_second = 0.55f;
    crawler.movement.vertical_anchor = "bottom";
    crawler.movement.movement_plane = "floor";
    crawler.movement.body_radius_meters = 0.03f;

    aquarium::AquariumPlayerTankSimulationInput tank;
    tank.tank_id = "player-tank";
    tank.navigation = navigation;
    tank.swimmers.push_back(crawler);
    simulation.replacePlayerTanks({tank});
    require(simulation.actors().size() == 1U,
        "unpositioned player-tank crawler did not spawn");
    const auto metrics = aquarium::measureAquariumPokemon(
        model->path, crawler.actor.form);
    const float rendered_bottom = simulation.actors().front().world_position[1] +
        metrics.min_y * crawler.actor.model_scale;
    require(near(rendered_bottom, layer.y_bottom * navigation.export_units_per_meter + 0.16f, 0.3f),
        "unpositioned crawler retained the volume midpoint instead of resting on the bottom");
    const float rendered_radius_world = std::max({
        std::abs(metrics.min_x), std::abs(metrics.max_x),
        std::abs(metrics.min_z), std::abs(metrics.max_z)}) * crawler.actor.model_scale;
    constexpr float expected_comfort_world = 0.12f * 16.0f;
    for (int frame = 0; frame < 60 * 20; ++frame) {
        simulation.update(1.0 / 60.0);
        const auto& actor = simulation.actors().front();
        require(std::abs(actor.world_position[0]) + rendered_radius_world +
                    expected_comfort_world <= 32.01f &&
                std::abs(actor.world_position[2]) + rendered_radius_world +
                    expected_comfort_world <= 32.01f,
            "player-tank crawler approached the glass inside its rendered comfort envelope");
    }
}

void timidReefPokemonIdleRandomlyAndFleePredators() {
    pr::gameplay::world3d::SceneConfig scene;
    aquarium::AquariumSimulation simulation(projectRoot(), scene, nullptr);
    const auto models = pr::gameplay::attend::discoverPokemonModels(
        projectRoot() / "assets/pokemon_attend/pokemon_models");
    const auto model = std::find_if(models.begin(), models.end(), [](const auto& candidate) {
        return candidate.id == "wishiwashi";
    });
    require(model != models.end(), "timid reef fixture requires a measured Pokemon model");

    aquarium::AquariumNavigation navigation;
    navigation.export_units_per_meter = 16.0f;
    navigation.valid = true;
    aquarium::SwimVolumeLayer layer;
    layer.id = "reef-floor";
    layer.y_bottom = -1.0f;
    layer.y_top = 2.0f;
    layer.polygons = {{{
        {-4.0f, -4.0f}, {4.0f, -4.0f}, {4.0f, 4.0f}, {-4.0f, 4.0f}}}};
    navigation.layers.push_back(layer);
    navigation.suggested_spawns.push_back({0.0f, 0.0f, 0.0f});

    const auto timidDefinition = [&](const std::string& id, std::uint32_t seed) {
        aquarium::AquariumSwimmerDefinition swimmer;
        swimmer.actor.id = id;
        swimmer.actor.species = "corsola";
        swimmer.actor.form = "00";
        swimmer.actor.model_path = model->path;
        swimmer.actor.animation = "idle_default";
        swimmer.actor.model_scale = 0.10f;
        swimmer.movement.id = id;
        swimmer.movement.species = "corsola";
        swimmer.movement.behavior = "timid";
        swimmer.movement.speed_meters_per_second = 0.10f;
        swimmer.movement.turn_degrees_per_second = 55.0f;
        swimmer.movement.body_radius_meters = 0.03f;
        swimmer.movement.vertical_anchor = "bottom";
        swimmer.movement.movement_plane = "floor";
        swimmer.movement.random_start = true;
        swimmer.movement.idle_seconds_minimum = 8.0f;
        swimmer.movement.idle_seconds_maximum = 18.0f;
        swimmer.movement.local_move_distance_meters = 0.28f;
        swimmer.movement.threat_species = {"mareanie", "toxapex"};
        swimmer.movement.flee_radius_meters = 1.6f;
        swimmer.movement.flee_distance_meters = 0.9f;
        swimmer.movement.flee_speed_multiplier = 3.5f;
        swimmer.seed = seed;
        return swimmer;
    };

    aquarium::AquariumPlayerTankSimulationInput tank;
    tank.tank_id = "random-reef";
    tank.navigation = navigation;
    tank.swimmers = {timidDefinition("corsola-a", 31U), timidDefinition("corsola-b", 79U)};
    simulation.replacePlayerTanks({tank});
    require(simulation.actors().size() == 2U,
        "random timid reef Pokemon did not spawn");
    const auto first_spawn = simulation.actors()[0].world_position;
    const auto second_spawn = simulation.actors()[1].world_position;
    require(std::hypot(first_spawn[0] - second_spawn[0], first_spawn[2] - second_spawn[2]) > 1.0f,
        "timid reef Pokemon reused one deterministic center spawn");
    for (int frame = 0; frame < 60 * 7; ++frame) simulation.update(1.0 / 60.0);
    require(simulation.actors()[0].world_position == first_spawn &&
            simulation.actors()[1].world_position == second_spawn,
        "timid reef Pokemon wandered during its guaranteed long idle interval");

    auto corsola = timidDefinition("corsola-threatened", 101U);
    corsola.movement.random_start = false;
    corsola.movement.has_starting_position = true;
    corsola.movement.starting_position_meters = {0.0f, 0.0f, 0.0f};
    aquarium::AquariumSwimmerDefinition mareanie;
    mareanie.actor.id = "mareanie";
    mareanie.actor.species = "mareanie";
    mareanie.actor.form = "00";
    mareanie.actor.model_path = model->path;
    mareanie.actor.animation = "idle_default";
    mareanie.actor.model_scale = 0.10f;
    mareanie.movement.id = mareanie.actor.id;
    mareanie.movement.species = mareanie.actor.species;
    mareanie.movement.behavior = "stationary";
    mareanie.movement.movement_plane = "floor";
    mareanie.movement.vertical_anchor = "bottom";
    mareanie.movement.has_starting_position = true;
    mareanie.movement.starting_position_meters = {0.55f, 0.0f, 0.0f};
    mareanie.movement.body_radius_meters = 0.03f;

    tank.tank_id = "threatened-reef";
    tank.swimmers = {corsola, mareanie};
    simulation.replacePlayerTanks({tank});
    require(simulation.actors().size() == 2U,
        "Corsola predator-avoidance fixture did not spawn");
    const auto distanceFromThreat = [&] {
        const auto& prey = simulation.actors()[0].world_position;
        const auto& threat = simulation.actors()[1].world_position;
        return std::hypot(prey[0] - threat[0], prey[2] - threat[2]);
    };
    const float initial_distance = distanceFromThreat();
    for (int frame = 0; frame < 60 * 2; ++frame) simulation.update(1.0 / 60.0);
    require(distanceFromThreat() > initial_distance + 4.0f,
        "Corsola did not flee from nearby Mareanie");
}

void leaderFormationTracksVelocityWithoutCounterSwimming() {
    aquarium::AquariumFormationBody leader;
    leader.id = "leader";
    leader.tank_id = "tank";
    leader.origin = {0.0f, 0.08f, 0.10f};
    leader.velocity = {0.0f, 0.8f, 1.0f};
    leader.pitch_degrees = -18.0f;
    leader.half_width = 0.65f;
    leader.half_height = 0.32f;
    leader.half_length = 1.1f;
    aquarium::AquariumFormationBody previous_leader = leader;
    previous_leader.origin = {0.0f, 0.0f, 0.0f};
    previous_leader.pitch_degrees = -12.0f;

    aquarium::AquariumFormationBody follower;
    follower.id = "follower";
    follower.tank_id = "tank";
    follower.origin = {0.72f, -0.1f, -0.12f};
    follower.velocity = {0.0f, 0.0f, 0.4f};
    follower.half_width = 0.12f;
    follower.half_height = 0.09f;
    follower.half_length = 0.22f;
    std::vector<aquarium::AquariumFormationBody> bodies{leader, follower};

    aquarium::AquariumFormationInput input;
    input.follower = follower;
    input.leader = leader;
    input.previous_leader = previous_leader;
    input.neighbours = &bodies;
    input.role_phase_radians = 0.0f;
    input.dt_seconds = 0.1f;
    const auto climbing = aquarium::steerAquariumFormation(input);
    require(climbing.desired_velocity[1] > 0.45f,
        "formation steering did not inherit the leader's climb velocity");
    require(climbing.desired_velocity[2] > 0.0f,
        "attached follower counter-swam against its moving leader");
    require(std::abs(climbing.anchor_velocity[0]) > 0.01f ||
            std::abs(climbing.anchor_velocity[1] - leader.velocity[1]) > 0.01f,
        "rotating leader did not impart attachment-region velocity");

    input.follower.origin = {0.0f, 0.0f, 4.0f};
    input.follower.velocity = {0.0f, 0.0f, 0.1f};
    const auto follower_ahead = aquarium::steerAquariumFormation(input);
    require(follower_ahead.desired_velocity[2] > 0.0f,
        "follower ahead of the formation was told to perform a backward U-turn");

    aquarium::AquariumFormationBody overlapping = follower;
    overlapping.id = "overlapping";
    bodies.push_back(overlapping);
    input.follower = follower;
    input.neighbours = &bodies;
    const auto separated = aquarium::steerAquariumFormation(input);
    require(std::abs(separated.desired_velocity[0] - climbing.desired_velocity[0]) > 0.01f ||
            std::abs(separated.desired_velocity[1] - climbing.desired_velocity[1]) > 0.01f,
        "mesh-envelope overlap did not contribute separation steering");
}

void largePlayerTankKyogreNavigatesWhileIdling() {
    pr::gameplay::world3d::SceneConfig scene;
    aquarium::AquariumSimulation simulation(projectRoot(), scene, nullptr);
    const auto models = pr::gameplay::attend::discoverPokemonModels(
        projectRoot() / "assets/pokemon_attend/pokemon_models");
    const auto kyogre = std::find_if(models.begin(), models.end(), [](const auto& model) {
        return model.id == "kyogre";
    });
    const auto remoraid = std::find_if(models.begin(), models.end(), [](const auto& model) {
        return model.id == "remoraid";
    });
    require(kyogre != models.end(), "Kyogre movement fixture requires its Attend model");
    require(remoraid != models.end(), "Kyogre escort fixture requires the Remoraid Attend model");

    aquarium::AquariumNavigation navigation;
    navigation.export_units_per_meter = 16.0f;
    navigation.floor_level_y = 0.0f;
    navigation.valid = true;
    aquarium::SwimVolumeLayer layer;
    layer.id = "large-player-water";
    layer.y_bottom = -8.8f;
    layer.y_top = 10.0f;
    layer.polygons = {{{
        {-8.5f, -11.0f}, {8.5f, -11.0f}, {8.5f, 11.0f}, {-8.5f, 11.0f}}}};
    navigation.layers.push_back(layer);
    navigation.suggested_spawns.push_back({0.0f, 0.6f, 0.0f});

    aquarium::AquariumSwimmerDefinition swimmer;
    swimmer.actor.id = "large-player-tank:kyogre";
    swimmer.actor.species = "kyogre";
    swimmer.actor.form = "00";
    swimmer.actor.model_path = kyogre->path;
    swimmer.actor.animation = "idle_default";
    swimmer.actor.model_scale = 0.27f;
    swimmer.movement.id = swimmer.actor.id;
    swimmer.movement.species = swimmer.actor.species;
    swimmer.movement.animation = swimmer.actor.animation;
    swimmer.movement.behavior = "school";
    swimmer.movement.speed_meters_per_second = 0.65f;
    swimmer.movement.turn_degrees_per_second = 48.0f;
    swimmer.movement.body_radius_meters = 0.35f;
    swimmer.movement.vertical_movement_scale = 0.62f;
    swimmer.movement.swim_pitch_degrees = 12.0f;
    swimmer.movement.motion_smoothing_seconds = 0.48f;
    swimmer.movement.pitch_turn_degrees_per_second = 8.0f;
    swimmer.seed = 382U;

    aquarium::AquariumPlayerTankSimulationInput tank;
    tank.tank_id = "large-player-tank";
    tank.navigation = navigation;
    for (int index = 0; index < 4; ++index) {
        aquarium::AquariumSwimmerDefinition escort;
        escort.actor.id = "large-player-tank:remoraid:" + std::to_string(index);
        escort.actor.species = "remoraid";
        escort.actor.form = "00";
        escort.actor.model_path = remoraid->path;
        escort.actor.animation = "walk";
        escort.actor.model_scale = 0.15f;
        escort.movement.id = escort.actor.id;
        escort.movement.species = escort.actor.species;
        escort.movement.animation = escort.actor.animation;
        escort.movement.behavior = "escort";
        // Deliberately unlike Kyogre: escort behavior must derive cruise speed
        // from the followed actor rather than this population-policy hint.
        escort.movement.speed_meters_per_second = 0.05f;
        escort.movement.turn_degrees_per_second = 72.0f;
        escort.movement.body_radius_meters = 0.05f;
        escort.movement.swim_pitch_degrees = 9.0f;
        escort.movement.follow_actor_id = swimmer.actor.id;
        escort.movement.follow_distance_meters = 0.52f;
        escort.movement.follow_vertical_gap_meters = 0.06f;
        escort.movement.motion_smoothing_seconds = 0.28f;
        escort.movement.pitch_turn_degrees_per_second = 18.0f;
        escort.movement.forward_only = true;
        escort.formation_index = index;
        escort.formation_count = 4;
        escort.seed = 500U + static_cast<std::uint32_t>(index);
        tank.swimmers.push_back(std::move(escort));
    }
    // Deliberately author followers first. Runtime installation must still
    // place their leader before calculating the initial formation.
    tank.swimmers.push_back(swimmer);
    simulation.replacePlayerTanks({tank});
    require(simulation.actors().size() == 5U,
        "Kyogre and its four Remoraid escorts did not fit in the player tank");
    require(simulation.actors()[1].animation_time_seconds !=
            simulation.actors()[2].animation_time_seconds &&
            simulation.actors()[2].animation_playback_rate !=
            simulation.actors()[3].animation_playback_rate,
        "Remoraid escorts began with synchronized animation phases and rates");
    const auto& spawned = simulation.actors();
    constexpr float kMaximumInitialEscortDistanceWorld = 96.0f;
    for (std::size_t left = 0; left < spawned.size(); ++left) {
        for (std::size_t right = left + 1; right < spawned.size(); ++right) {
            float distance_squared = 0.0f;
            for (int axis = 0; axis < 3; ++axis) {
                const float delta = spawned[left].world_position[axis] -
                    spawned[right].world_position[axis];
                distance_squared += delta * delta;
            }
            require(distance_squared > 4.0f,
                "initial aquarium residents spawned on top of each other");
        }
        if (left > 0) {
            float leader_distance_squared = 0.0f;
            for (int axis = 0; axis < 3; ++axis) {
                const float delta = spawned[left].world_position[axis] -
                    spawned.front().world_position[axis];
                leader_distance_squared += delta * delta;
            }
            require(leader_distance_squared <
                    kMaximumInitialEscortDistanceWorld *
                        kMaximumInitialEscortDistanceWorld,
                "escort spawned far from its leader before simulation began");
        }
    }
    const auto initial = simulation.actors().front().world_position;
    float minimum_y = initial[1];
    float maximum_y = initial[1];
    float maximum_pitch = 0.0f;
    float maximum_escort_pitch = 0.0f;
    float maximum_escort_vertical_spread = 0.0f;
    float minimum_escort_average_y = std::numeric_limits<float>::max();
    float maximum_escort_average_y = std::numeric_limits<float>::lowest();
    float minimum_horizontal_alignment = 1.0f;
    float leader_vertical_travel = 0.0f;
    float escort_vertical_travel = 0.0f;
    int horizontal_alignment_samples = 0;
    int vertical_alignment_samples = 0;
    int vertical_alignment_matches = 0;
    std::vector<aquarium::Point3> previous_positions;
    for (const auto& actor : simulation.actors()) {
        previous_positions.push_back(actor.world_position);
    }
    for (int frame = 0; frame < 600; ++frame) {
        simulation.update(1.0 / 60.0);
        const auto& actor = simulation.actors().front();
        minimum_y = std::min(minimum_y, actor.world_position[1]);
        maximum_y = std::max(maximum_y, actor.world_position[1]);
        maximum_pitch = std::max(maximum_pitch, std::abs(actor.world_pitch_degrees));
        float escort_minimum_y = simulation.actors()[1].world_position[1];
        float escort_maximum_y = escort_minimum_y;
        float escort_average_y = 0.0f;
        float escort_average_dy = 0.0f;
        const float leader_dx = actor.world_position[0] - previous_positions[0][0];
        const float leader_dy = actor.world_position[1] - previous_positions[0][1];
        const float leader_dz = actor.world_position[2] - previous_positions[0][2];
        const float leader_horizontal = std::sqrt(
            leader_dx * leader_dx + leader_dz * leader_dz);
        for (std::size_t index = 1; index < simulation.actors().size(); ++index) {
            const auto& escort = simulation.actors()[index];
            maximum_escort_pitch = std::max(
                maximum_escort_pitch, std::abs(escort.world_pitch_degrees));
            escort_minimum_y = std::min(escort_minimum_y, escort.world_position[1]);
            escort_maximum_y = std::max(escort_maximum_y, escort.world_position[1]);
            escort_average_y += escort.world_position[1];
            escort_average_dy += escort.world_position[1] - previous_positions[index][1];
            if (frame > 120 && leader_horizontal > 0.0001f) {
                const float follower_dx =
                    escort.world_position[0] - previous_positions[index][0];
                const float follower_dz =
                    escort.world_position[2] - previous_positions[index][2];
                const float follower_horizontal = std::sqrt(
                    follower_dx * follower_dx + follower_dz * follower_dz);
                if (follower_horizontal > 0.0001f) {
                    minimum_horizontal_alignment = std::min(
                        minimum_horizontal_alignment,
                        (leader_dx * follower_dx + leader_dz * follower_dz) /
                            (leader_horizontal * follower_horizontal));
                    ++horizontal_alignment_samples;
                }
            }
        }
        escort_average_y /= static_cast<float>(simulation.actors().size() - 1U);
        escort_average_dy /= static_cast<float>(simulation.actors().size() - 1U);
        if (frame > 120) {
            minimum_escort_average_y = std::min(minimum_escort_average_y, escort_average_y);
            maximum_escort_average_y = std::max(maximum_escort_average_y, escort_average_y);
            if (std::abs(leader_dy) > 0.001f) {
                leader_vertical_travel += std::abs(leader_dy);
                escort_vertical_travel += std::abs(escort_average_dy);
                ++vertical_alignment_samples;
                if (leader_dy * escort_average_dy >= 0.0f) ++vertical_alignment_matches;
            }
        }
        maximum_escort_vertical_spread = std::max(
            maximum_escort_vertical_spread, escort_maximum_y - escort_minimum_y);
        for (std::size_t index = 0; index < simulation.actors().size(); ++index) {
            previous_positions[index] = simulation.actors()[index].world_position;
        }
    }
    const auto moved = simulation.actors().front().world_position;
    const float dx = moved[0] - initial[0];
    const float dy = moved[1] - initial[1];
    const float dz = moved[2] - initial[2];
    require(dx * dx + dy * dy + dz * dz > 1.0f,
        "roaming Kyogre remained stationary while its idle animation played");
    require(maximum_y - minimum_y > 8.0f && maximum_pitch > 1.0f,
        "Kyogre patrol did not visibly rise, descend, and pitch along its route");
    require(maximum_escort_pitch > 1.0f && maximum_escort_vertical_spread > 1.0f &&
            maximum_escort_average_y - minimum_escort_average_y > 2.0f,
        "Remoraid escorts did not pitch, separate vertically, and follow Kyogre's depth");
    require(horizontal_alignment_samples > 100 && minimum_horizontal_alignment >= -0.01f,
        "an attached Remoraid counter-swam during Kyogre's turn");
    require(vertical_alignment_samples > 50 &&
            static_cast<float>(vertical_alignment_matches) /
                static_cast<float>(vertical_alignment_samples) > 0.62f &&
            escort_vertical_travel > leader_vertical_travel * 0.60f,
        "Remoraid vertical velocity did not keep pace with Kyogre");
    const auto& leader = simulation.actors().front();
    for (std::size_t index = 1; index < simulation.actors().size(); ++index) {
        const auto& escort = simulation.actors()[index];
        const float escort_dx = escort.world_position[0] - leader.world_position[0];
        const float escort_dy = escort.world_position[1] - leader.world_position[1];
        const float escort_dz = escort.world_position[2] - leader.world_position[2];
        require(escort_dx * escort_dx + escort_dy * escort_dy + escort_dz * escort_dz <
                96.0f * 96.0f,
            "Remoraid escort failed to remain in Kyogre's local flock");
    }

    aquarium::AquariumPlayerTankSimulationInput school_tank;
    school_tank.tank_id = "school-spawn-tank";
    school_tank.navigation = navigation;
    for (int index = 0; index < 6; ++index) {
        aquarium::AquariumSwimmerDefinition schooler;
        schooler.actor.id = "school-spawn-tank:remoraid:" + std::to_string(index);
        schooler.actor.species = "remoraid";
        schooler.actor.model_path = remoraid->path;
        schooler.actor.animation = "walk";
        schooler.actor.model_scale = 0.15f;
        schooler.movement.id = "remoraid-school";
        schooler.movement.behavior = "school";
        schooler.movement.speed_meters_per_second = 0.35f;
        schooler.movement.body_radius_meters = 0.05f;
        schooler.formation_index = index;
        schooler.formation_count = 6;
        schooler.seed = 800U + static_cast<std::uint32_t>(index);
        school_tank.swimmers.push_back(std::move(schooler));
    }
    simulation.replacePlayerTanks({school_tank});
    require(simulation.actors().size() == 6U,
        "separated initial school placement rejected valid members");
    const auto& school = simulation.actors();
    for (std::size_t index = 1; index < school.size(); ++index) {
        float distance_squared = 0.0f;
        for (int axis = 0; axis < 3; ++axis) {
            const float delta = school[index].world_position[axis] -
                school.front().world_position[axis];
            distance_squared += delta * delta;
        }
        require(distance_squared > 4.0f && distance_squared < 96.0f * 96.0f,
            "school member did not begin separated inside its local group");
    }
}

void crowdAvoidanceAndJellyDriftStayNatural() {
    aquarium::AquariumCrowdBody self;
    self.center = {0.0f, 0.0f, 0.0f};
    self.horizontal_radius = 0.25f;
    self.vertical_radius = 0.25f;
    aquarium::AquariumCrowdBody neighbour = self;
    neighbour.center = {0.42f, 0.0f, 0.0f};
    const std::vector<aquarium::AquariumCrowdBody> neighbours{neighbour};
    const auto steered = aquarium::steerAquariumCrowd(
        self, neighbours, {1.0f, 0.0f, 0.0f});
    require(steered[0] < 1.0f,
        "nearby aquarium body did not deflect an approaching swimmer");
    require(!aquarium::aquariumCrowdMoveAllowed(
            self, {0.45f, 0.0f, 0.0f}, neighbours),
        "crowd solver allowed a swimmer to enter another animal's body core");

    pr::gameplay::world3d::SceneConfig scene;
    aquarium::AquariumSimulation simulation(projectRoot(), scene, nullptr);
    aquarium::AquariumNavigation navigation;
    navigation.export_units_per_meter = 16.0f;
    navigation.valid = true;
    aquarium::SwimVolumeLayer layer;
    layer.id = "jelly-water";
    layer.y_bottom = -2.0f;
    layer.y_top = 2.0f;
    layer.polygons = {{{
        {-4.0f, -4.0f}, {4.0f, -4.0f}, {4.0f, 4.0f}, {-4.0f, 4.0f}}}};
    navigation.layers.push_back(layer);
    navigation.suggested_spawns.push_back({0.0f, 0.0f, 0.0f});

    aquarium::AquariumPlayerTankSimulationInput tank;
    tank.tank_id = "jelly-tank";
    tank.navigation = navigation;
    for (int index = 0; index < 4; ++index) {
        aquarium::AquariumSwimmerDefinition jelly;
        jelly.actor.id = "jelly:" + std::to_string(index);
        jelly.actor.species = index < 2 ? "tentacool" : "frillish";
        jelly.actor.model_path = "baked-envelope-fixture.glbz";
        jelly.actor.animation = "slot6_00";
        jelly.actor.model_scale = 1.0f;
        jelly.movement.id = jelly.actor.id;
        jelly.movement.species = jelly.actor.species;
        jelly.movement.behavior = "jelly";
        jelly.movement.speed_meters_per_second = 0.11f;
        jelly.movement.turn_degrees_per_second = 32.0f;
        jelly.movement.motion_smoothing_seconds = 0.72f;
        jelly.movement.body_radius_meters = 0.04f;
        jelly.movement.random_start = true;
        jelly.movement.has_baked_physical_envelope = true;
        jelly.movement.baked_physical_envelope = {
            -4.0f, 4.0f, -4.0f, 4.0f, -4.0f, 4.0f};
        jelly.seed = 720U + static_cast<std::uint32_t>(index);
        tank.swimmers.push_back(std::move(jelly));
    }
    simulation.replacePlayerTanks({tank});
    require(simulation.actors().size() == 4U,
        "jelly drift fixture could not place separated residents");
    const auto initial = simulation.actors().front().world_position;
    float minimum_y = initial[1];
    float maximum_y = initial[1];
    float maximum_horizontal_offset = 0.0f;
    constexpr float kMinimumCenterDistanceWorld = 3.4f;
    for (int frame = 0; frame < 60 * 20; ++frame) {
        simulation.update(1.0 / 60.0);
        const auto& actors = simulation.actors();
        minimum_y = std::min(minimum_y, actors.front().world_position[1]);
        maximum_y = std::max(maximum_y, actors.front().world_position[1]);
        maximum_horizontal_offset = std::max(maximum_horizontal_offset, std::hypot(
            actors.front().world_position[0] - initial[0],
            actors.front().world_position[2] - initial[2]));
        for (std::size_t left = 0; left < actors.size(); ++left) {
            for (std::size_t right = left + 1; right < actors.size(); ++right) {
                const float dx = actors[left].world_position[0] -
                    actors[right].world_position[0];
                const float dy = actors[left].world_position[1] -
                    actors[right].world_position[1];
                const float dz = actors[left].world_position[2] -
                    actors[right].world_position[2];
                require(std::sqrt(dx * dx + dy * dy + dz * dz) >=
                        kMinimumCenterDistanceWorld,
                    "moving aquarium Pokemon overlapped their central body cores");
            }
        }
    }
    require(maximum_y - minimum_y > 5.0f &&
            maximum_y - minimum_y > maximum_horizontal_offset,
        "jelly profile did not favor a slow local rise-and-fall motion");
}

void intermittentBenthicSwimmerRestsAndResumes() {
    pr::gameplay::world3d::SceneConfig scene;
    aquarium::AquariumSimulation simulation(projectRoot(), scene, nullptr);
    aquarium::AquariumNavigation navigation;
    navigation.export_units_per_meter = 16.0f;
    navigation.valid = true;
    aquarium::SwimVolumeLayer layer;
    layer.id = "benthic-water";
    layer.y_bottom = -2.0f;
    layer.y_top = 2.0f;
    layer.polygons = {{{
        {-4.0f, -4.0f}, {4.0f, -4.0f}, {4.0f, 4.0f}, {-4.0f, 4.0f}}}};
    navigation.layers.push_back(layer);
    navigation.suggested_spawns.push_back({0.0f, 0.0f, 0.0f});

    aquarium::AquariumSwimmerDefinition resident;
    resident.actor.id = "benthic:huntail";
    resident.actor.species = "huntail";
    resident.actor.model_path = "baked-envelope-fixture.glbz";
    resident.actor.animation = "slot6_02";
    resident.actor.model_scale = 1.0f;
    resident.movement.id = resident.actor.id;
    resident.movement.species = resident.actor.species;
    resident.movement.animation = resident.actor.animation;
    resident.movement.idle_animation = "slot6_00";
    resident.movement.behavior = "wander";
    resident.movement.speed_meters_per_second = 0.9f;
    resident.movement.turn_degrees_per_second = 90.0f;
    resident.movement.vertical_anchor = "bottom";
    resident.movement.move_seconds_minimum = 0.8f;
    resident.movement.move_seconds_maximum = 1.0f;
    resident.movement.rest_seconds_minimum = 0.8f;
    resident.movement.rest_seconds_maximum = 1.0f;
    resident.movement.roaming_height_meters = 0.75f;
    resident.movement.rest_at_bottom = true;
    resident.movement.has_baked_physical_envelope = true;
    resident.movement.baked_physical_envelope = {
        -2.0f, 2.0f, -2.0f, 2.0f, -2.0f, 2.0f};
    resident.seed = 601U; // Deterministically starts in the resting phase.

    aquarium::AquariumPlayerTankSimulationInput tank;
    tank.tank_id = "benthic-rest-tank";
    tank.navigation = navigation;
    tank.swimmers.push_back(std::move(resident));
    simulation.replacePlayerTanks({tank});
    require(simulation.actors().size() == 1U,
        "intermittent benthic fixture did not spawn");
    require(simulation.actors().front().animation == "slot6_00",
        "intermittent benthic resident did not begin at rest");
    const float resting_y = simulation.actors().front().world_position[1];
    float maximum_y = resting_y;
    bool used_movement_animation = false;
    bool resumed_idle_animation = false;
    for (int frame = 0; frame < 60 * 8; ++frame) {
        simulation.update(1.0 / 60.0);
        const auto& actor = simulation.actors().front();
        maximum_y = std::max(maximum_y, actor.world_position[1]);
        if (actor.animation == "slot6_02") used_movement_animation = true;
        if (used_movement_animation && actor.animation == "slot6_00") {
            resumed_idle_animation = true;
        }
    }
    require(used_movement_animation && resumed_idle_animation,
        "intermittent benthic resident did not alternate movement and rest animations");
    require(maximum_y > resting_y + 2.0f,
        "intermittent benthic resident never left the bottom to swim");
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
        const auto toward=camera.pose().forward;
        require(position.y>tank.floor_y_world && position.x*toward.x+position.z*toward.z<0,
            "overview camera must remain outside the approached tank face");
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

int main(int argc, char** argv) {
    try {
        runAquariumEmissionTests();
        if (argc > 1 && std::string(argv[1]) == "--emission-only") return 0;
        if (argc > 1 && std::string(argv[1]) == "--school-only") { runAquariumSchoolTests(); return 0; }
        runAquariumMotionTests();
        if (argc > 1 && std::string(argv[1]) == "--motion-only") return 0;
        positionParsingRejectsSilentZeroes();
        inspectionFacingRestoresEveryApproachDirection();
        navigationHonorsUndergroundLayersAndHoles();
        aquariumRuntimeLodPreservesAnimationAndReducesGeometry();
        if (!(argc > 1 && std::string(argv[1]) == "--simulation-only"))
            configuredDewgongMovesInsidePlacedTank();
        playerTankPopulationUsesRuntimeNavigation();
        unpositionedCrawlerRestsOnPlayerTankBottom();
        timidReefPokemonIdleRandomlyAndFleePredators();
        leaderFormationTracksVelocityWithoutCounterSwimming();
        largePlayerTankKyogreNavigatesWhileIdling();
        crowdAvoidanceAndJellyDriftStayNatural();
        intermittentBenthicSwimmerRestsAndResumes();
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
