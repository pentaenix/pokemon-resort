#include "gameplay/world3d/aquarium/AquariumConfig.hpp"
#include "gameplay/world3d/aquarium/AquariumNavigation.hpp"
#include "gameplay/world3d/aquarium/AquariumPokemonMetrics.hpp"
#include "gameplay/world3d/aquarium/AquariumSimulation.hpp"
#include "gameplay/world3d/aquarium/AquariumInspectionCamera.hpp"
#include "gameplay/world3d/camera/FreeCameraCapture.hpp"
#include "gameplay/world3d/interiors/InteriorFloorCutout.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace aquarium = pr::gameplay::world3d::aquarium;
namespace fs = std::filesystem;

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

fs::path projectRoot() {
#if defined(PR_SOURCE_DIR)
    return fs::path(PR_SOURCE_DIR);
#else
    return fs::current_path();
#endif
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
    tank.x = 112.0f;
    tank.y = 0.0f;
    tank.z = 88.0f;
    tank.yaw_deg = 0.0f;
    tank.scale = 1.0f;
    scene.models.push_back(tank);
    pr::gameplay::world3d::ModelPlacementConfig cylinder;
    cylinder.id = "aquarium_c";
    cylinder.x = 336.0f;
    cylinder.y = 0.0f;
    cylinder.z = 80.0f;
    cylinder.scale = 1.0f;
    scene.models.push_back(cylinder);

    const aquarium::AquariumCatalog catalog = aquarium::loadAquariumCatalog(projectRoot().string());
    aquarium::AquariumSimulation simulation(
        projectRoot(), scene, aquarium::aquariumMapConfig(catalog, scene.id));
    require(simulation.active(), "aquarium12 must instantiate its configured Pokemon");
    require(simulation.actors().size() == 7U,
        "both tanks must receive Dewgong, Clamperl, and the five-fish school");
    require(simulation.actors().front().species == "dewgong", "Pokemon must resolve by species name");
    require(std::abs(simulation.actors().front().model_scale - 0.3f) < 0.0001f,
        "one global scale must control every aquarium Pokemon");
    require(simulation.actors().front().animation == "idle_default",
        "Dewgong must use its authored idle animation while swimming");
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
    for (std::size_t i = 2; i < simulation.actors().size(); ++i) {
        const auto& fish = simulation.actors()[i];
        require(fish.species == "wishiwashi" && fish.animation == "idle_default",
            "the cylinder school must use five idle-animated Wishiwashi");
        school_min_y = std::min(school_min_y, fish.world_position[1]);
        school_max_y = std::max(school_max_y, fish.world_position[1]);
        for (std::size_t j = i + 1; j < simulation.actors().size(); ++j) {
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

    aquarium::AquariumInspectionCamera inspection(simulation.tanks());
    pr::gameplay::world3d::camera::Gen4FollowCamera camera(
        pr::gameplay::world3d::camera::Gen4CameraPreset{});
    const pr::gameplay::world3d::camera::Vec3 player_position{112.0f, 0.0f, 136.0f};
    camera.setTarget(player_position);
    const auto follow_pose = camera.pose();
    require(inspection.tryBegin(
        {112.0f, 0.0f, 136.0f}, pr::gameplay::world3d::FacingDirection::North,
        16.0f, camera), "facing a nearby tank must start inspection");
    inspection.update(1.0 / 60.0, camera);
    require(inspection.activePlacementId() == "aquarium",
        "inspection must select the tank hit by the player's facing ray");
    const auto pose = camera.pose();
    require(pose.position.z > 136.0f && pose.forward.z < 0.0f && pose.position.y > 0.0f,
        "instant inspection camera must sit behind the player above the room floor");
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
            {112.0f, 14.4f, 88.0f}, 640, 480, screen_x, screen_y, depth) &&
            depth > pose.preset.near_clip,
        "focused tank must remain beyond the camera near clip instead of rendering black");
    inspection.close();
    require(!inspection.active(), "inspection must close without changing aquarium simulation");

    camera.setTarget({336.0f, -100.0f, 112.0f});
    require(inspection.tryBegin(
        {336.0f, -100.0f, 112.0f}, pr::gameplay::world3d::FacingDirection::North,
        16.0f, camera), "cylinder inspection must tolerate a bad below-floor player sample");
    inspection.update(1.0 / 60.0, camera);
    require(camera.pose().position.y > simulation.tanks()[1].floor_y_world,
        "inspection camera must never inherit a below-floor terrain sample");
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
        navigationHonorsUndergroundLayersAndHoles();
        configuredDewgongMovesInsidePlacedTank();
        polygonCutoutPreservesPartialFloorCells();
        freeCameraCaptureIsPasteReady();
        std::cout << "aquarium_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "aquarium_tests: " << error.what() << '\n';
        return 1;
    }
}
