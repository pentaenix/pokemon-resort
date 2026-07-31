#include "gameplay/world3d/doors/DoorTravel.hpp"
#include "gameplay/world3d/doors/DoorAnimationPolicy.hpp"
#include "gameplay/world3d/npc/NpcPopulationPolicy.hpp"
#include "gameplay/world3d/rendering/InteriorRenderPolicy.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
void require(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
}

int main() {
    using namespace pr::gameplay::world3d;
    using namespace pr::gameplay::world3d::doors;
    characters::LoadedWorldChunk outside{"outside", {}, 0, 0};
    outside.scene.id = "outside";
    DoorTriggerConfig trigger;
    trigger.id = "front_door";
    trigger.tile_x = 4;
    trigger.tile_y = 3;
    trigger.allowed_directions = {FacingDirection::North};
    trigger.link_id = "enter";
    trigger.script_id = "door_enter_default";
    outside.scene.door_triggers.push_back(trigger);
    outside.scene.links.push_back({"enter", "inside", "entry"});

    characters::LoadedWorldChunk inside{"inside", {}, 4096, 0};
    inside.scene.id = "inside";
    inside.scene.anchors.push_back({"entry", 2, 7, FacingDirection::South});
    const std::vector<characters::LoadedWorldChunk> chunks{outside, inside};

    const auto hit = findDoorTrigger(chunks, 4, 4, 4, 3, 0, -1);
    require(hit.has_value(), "northward movement should activate a north-facing door trigger");
    require(!findDoorTrigger(chunks, 3, 3, 4, 3, 1, 0).has_value(), "wrong approach direction must not activate the door");
    const auto destination = resolveDoorDestination(chunks, *hit);
    require(destination.has_value(), "door link should resolve its destination anchor");
    require(destination->world_tile_x == 4098 && destination->world_tile_y == 7, "destination should include isolated chunk origin");

    scripts::OverworldScript script;
    script.kind = scripts::ScriptKind::Door;
    script.valid = true;
    script.actions.push_back({});
    DoorSequenceController sequence;
    require(sequence.start(&script, *hit), "door sequence should accept a valid trigger hit");
    require(sequence.hit().chunk != hit->chunk && sequence.hit().trigger != hit->trigger,
        "door sequence must own stable trigger data across active-space changes");
    require(sequence.hit().trigger->id == "front_door", "owned door trigger should retain its metadata");

    characters::LoadedWorldChunk halo{"halo", {}, 0, 0};
    trigger.tile_x = 5;
    trigger.tile_y = -1;
    halo.scene.door_triggers = {trigger};
    require(findDoorTrigger({halo}, 5, 0, 5, -1, 0, -1).has_value(), "one-cell exterior halo triggers must activate before bounds rejection");
    require(isCardinalHaloTile(14, 10, 6, 10),
        "a south doorway anchor may occupy the map's one-cell halo");
    require(!isCardinalHaloTile(14, 10, 6, 11),
        "door anchors may not teleport farther than the one-cell halo");

    namespace fs = std::filesystem;
    const fs::path config_root = fs::temp_directory_path() / "pokemon_resort_door_travel_config_test";
    fs::remove_all(config_root);
    fs::create_directories(config_root / "config/gameplay/world3d");
    {
        std::ofstream config_file(config_root / "config/gameplay/world3d/door_travel.json");
        config_file << R"({
          "tileAnimation":{"fallbackDurationSeconds":0.4},
          "destinations":{"interior":{"landingOffsetPixels":{"right":2,"forward":3},"movementSecondsPerTile":0.2}},
          "maps":{"room":{"destinationType":"interior","landingOffsetPixels":{"right":-1,"forward":4},"movementSecondsPerTile":0.5}}
        })";
    }
    const DoorTravelConfig travel_config = loadDoorTravelConfig(config_root.string());
    require(travel_config.fallback_tile_animation_seconds == 0.4,
        "door animation fallback duration loads from door_travel.json");
    const auto& room_tuning = travel_config.destination("room", "interior");
    const auto [room_x, room_z] = landingWorldOffset(FacingDirection::North, room_tuning);
    require(room_x == -1.0f && room_z == -4.0f,
        "map-specific landing offsets rotate relative to destination facing");
    require(forcedMovementSpeed(16.0f, 1, 0.0, room_tuning) == 32.0f,
        "map movement duration controls the default forced-move speed");
    require(forcedMovementSpeed(16.0f, 1, 0.125, room_tuning) == 128.0f,
        "an authored MOVE_PLAYER duration overrides the map default");
    fs::remove_all(config_root);

    ForcedDoorMoveController forced_move;
    forced_move.start(FacingDirection::South, 1);
    require(forced_move.shouldRequestStep(false), "one-tile forced movement should request exactly one step");
    forced_move.reportStepAttempt(false);
    require(forced_move.remainingTiles() == 0, "forced movement consumes its count when the step starts");
    require(!forced_move.shouldRequestStep(true), "finishing the last step must not request another step");
    forced_move.reportMovementState(true);
    require(forced_move.active(), "forced movement remains active until its final interpolation settles");
    forced_move.reportMovementState(false);
    require(!forced_move.active(), "forced movement completes as soon as the requested tile settles");

    forced_move.start(FacingDirection::South, 1);
    forced_move.reportStepAttempt(true);
    forced_move.reportMovementState(false);
    require(!forced_move.active(), "a blocked forced movement must stop instead of retrying forever");

    require(!animationLoops(true, true), "script-triggered door UV motion must never inherit ambient looping");
    require(animationLoops(false, true), "ambient authored material motion may still loop");
    require(!animationLoops(false, false), "non-looping ambient material motion stays non-looping");
    require(animationSample(true, 99.0, 8) == 7.0,
        "triggered door motion clamps to and holds its final open sample");
    require(animationSample(true, 3.0, 8) == 3.0,
        "triggered door motion advances normally before its terminal sample");
    require(animationSample(false, 99.0, 8) == 99.0,
        "ambient animation retains its authored timeline");
    require(clampsTextureEdges(true),
        "a triggered door clamps its texture strip instead of wrapping repeated copies");
    require(!clampsTextureEdges(false),
        "ambient material motion retains its authored sampler behavior");
    require(animationUvOffset(true, 0.25f) == 0.25f,
        "a triggered door moves toward the transparent half of its authored texture");
    require(animationUvOffset(false, 0.25f) == -0.25f,
        "ambient material matrices keep the renderer's inverse sampling convention");
    require(!playerUsesMovementAnimation(true, true),
        "script-controlled entrance and exit movement keeps the player in an idle pose");
    require(playerUsesMovementAnimation(false, true),
        "ordinary physical movement still uses the walking animation");

    SceneConfig exterior_scene;
    exterior_scene.map_type = "exterior";
    SceneConfig interior_scene;
    interior_scene.map_type = "interior";
    require(npc::allowsGlobalDefaultPopulation(exterior_scene), "the exterior test world keeps its global NPC population");
    require(!npc::allowsGlobalDefaultPopulation(interior_scene), "interiors must not copy the exterior NPC population");
    require(rendering::shouldRenderFallbackTerrain(interior_scene),
        "an empty interior retains its buildable fallback grid");
    interior_scene.interior.shell_model_id = "complete_room_shell";
    require(!rendering::shouldRenderFallbackTerrain(interior_scene),
        "a complete imported shell must not fight with procedural floor geometry");
    return 0;
}
