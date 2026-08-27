#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/characters/CharacterTerrainQuery.hpp"
#include "gameplay/world3d/scripts/OverworldScript.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pr::gameplay::world3d::doors {

struct DoorTriggerHit {
    const characters::LoadedWorldChunk* chunk = nullptr;
    const DoorTriggerConfig* trigger = nullptr;
};

struct DoorDestination {
    const characters::LoadedWorldChunk* chunk = nullptr;
    int world_tile_x = 0;
    int world_tile_y = 0;
    FacingDirection facing = FacingDirection::South;
};

FacingDirection directionForStep(int dx, int dy);
bool isCardinalHaloTile(int width, int height, int tile_x, int tile_y);

struct DoorDestinationTuning {
    float landing_right_pixels = 0.0f;
    float landing_forward_pixels = 0.0f;
    double movement_seconds_per_tile = 0.25;
};

struct DoorTravelConfig {
    double fallback_tile_animation_seconds = 0.28;
    DoorDestinationTuning interior_destination{};
    DoorDestinationTuning exterior_destination{};
    std::unordered_map<std::string, DoorDestinationTuning> map_overrides;

    const DoorDestinationTuning& destination(
        const std::string& map_id, const std::string& map_type) const;
};

DoorTravelConfig defaultDoorTravelConfig();
DoorTravelConfig loadDoorTravelConfig(const std::string& project_root);
std::pair<float, float> landingWorldOffset(
    FacingDirection facing, const DoorDestinationTuning& tuning);
float forcedMovementSpeed(
    float tile_size, int tiles, double action_duration_seconds,
    const DoorDestinationTuning& tuning);
std::optional<DoorTriggerHit> findDoorTrigger(
    const std::vector<characters::LoadedWorldChunk>& chunks,
    int from_world_x,
    int from_world_y,
    int to_world_x,
    int to_world_y,
    int step_dx,
    int step_dy);
std::optional<DoorDestination> resolveDoorDestination(
    const std::vector<characters::LoadedWorldChunk>& chunks,
    const DoorTriggerHit& hit);
const scripts::OverworldScript* findDoorScript(
    const scripts::ScriptCatalog& catalog,
    const std::string& script_id);

class DoorSequenceController {
public:
    bool start(
        const scripts::OverworldScript* script,
        DoorTriggerHit hit,
        const std::vector<characters::LoadedWorldChunk>& chunks);
    const scripts::ScriptAction* currentAction() const;
    void advance();
    void cancel();
    bool active() const { return script_ != nullptr; }
    const DoorTriggerHit& hit() const { return hit_; }

private:
    const scripts::OverworldScript* script_ = nullptr;
    characters::LoadedWorldChunk hit_chunk_{};
    DoorTriggerConfig hit_trigger_{};
    DoorTriggerHit hit_{};
    std::size_t action_index_ = 0;
};

class ForcedDoorMoveController {
public:
    void start(FacingDirection direction, int tiles);
    bool active() const { return active_; }
    FacingDirection direction() const { return direction_; }
    int remainingTiles() const { return remaining_tiles_; }
    bool shouldRequestStep(bool player_moving) const;
    void reportStepAttempt(bool blocked);
    void reportMovementState(bool player_moving);
    void cancel();

private:
    FacingDirection direction_ = FacingDirection::South;
    int remaining_tiles_ = 0;
    bool active_ = false;
};

} // namespace pr::gameplay::world3d::doors
