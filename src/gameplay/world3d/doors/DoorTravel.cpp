#include "gameplay/world3d/doors/DoorTravel.hpp"

#include "core/config/Json.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace pr::gameplay::world3d::doors {
namespace fs = std::filesystem;

namespace {

double numOr(const JsonValue* value, double fallback) {
    return value && value->isNumber() ? value->asNumber() : fallback;
}

DoorDestinationTuning parseDestinationTuning(
    const JsonValue* value, DoorDestinationTuning fallback) {
    if (!value || !value->isObject()) return fallback;
    if (const JsonValue* offset = value->get("landingOffsetPixels"); offset && offset->isObject()) {
        fallback.landing_right_pixels = static_cast<float>(
            numOr(offset->get("right"), fallback.landing_right_pixels));
        fallback.landing_forward_pixels = static_cast<float>(
            numOr(offset->get("forward"), fallback.landing_forward_pixels));
    }
    fallback.movement_seconds_per_tile = std::clamp(
        numOr(value->get("movementSecondsPerTile"), fallback.movement_seconds_per_tile),
        0.01,
        10.0);
    return fallback;
}

} // namespace

FacingDirection directionForStep(int dx, int dy) {
    if (dy < 0) return FacingDirection::North;
    if (dx > 0) return FacingDirection::East;
    if (dx < 0) return FacingDirection::West;
    return FacingDirection::South;
}

bool isCardinalHaloTile(int width, int height, int tile_x, int tile_y) {
    width = std::max(1, width);
    height = std::max(1, height);
    const bool vertical_halo = (tile_y == -1 || tile_y == height) &&
        tile_x >= 0 && tile_x < width;
    const bool horizontal_halo = (tile_x == -1 || tile_x == width) &&
        tile_y >= 0 && tile_y < height;
    return vertical_halo || horizontal_halo;
}

DoorTravelConfig defaultDoorTravelConfig() {
    return DoorTravelConfig{};
}

const DoorDestinationTuning& DoorTravelConfig::destination(
    const std::string& map_id, const std::string& map_type) const {
    if (const auto it = map_overrides.find(map_id); it != map_overrides.end()) return it->second;
    return map_type == "interior" ? interior_destination : exterior_destination;
}

DoorTravelConfig loadDoorTravelConfig(const std::string& project_root) {
    DoorTravelConfig config = defaultDoorTravelConfig();
    try {
        const JsonValue root = parseJsonFile(
            (fs::path(project_root) / "config" / "gameplay" / "world3d" / "door_travel.json").string());
        if (!root.isObject()) return config;
        if (const JsonValue* animation = root.get("tileAnimation");
            animation && animation->isObject()) {
            config.fallback_tile_animation_seconds = std::clamp(
                numOr(animation->get("fallbackDurationSeconds"), config.fallback_tile_animation_seconds),
                0.0,
                10.0);
        }
        if (const JsonValue* destinations = root.get("destinations");
            destinations && destinations->isObject()) {
            config.interior_destination = parseDestinationTuning(
                destinations->get("interior"), config.interior_destination);
            config.exterior_destination = parseDestinationTuning(
                destinations->get("exterior"), config.exterior_destination);
        }
        if (const JsonValue* maps = root.get("maps"); maps && maps->isObject()) {
            for (const auto& [map_id, value] : maps->asObject()) {
                if (!value.isObject()) continue;
                const std::string type = value.get("destinationType") && value.get("destinationType")->isString()
                    ? value.get("destinationType")->asString()
                    : "exterior";
                const DoorDestinationTuning base = type == "interior"
                    ? config.interior_destination
                    : config.exterior_destination;
                config.map_overrides[map_id] = parseDestinationTuning(&value, base);
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "[Overworld3D] Could not load door travel config: " << ex.what() << '\n';
    }
    return config;
}

std::pair<float, float> landingWorldOffset(
    FacingDirection facing, const DoorDestinationTuning& tuning) {
    float forward_x = 0.0f;
    float forward_z = 0.0f;
    float right_x = 0.0f;
    float right_z = 0.0f;
    switch (facing) {
        case FacingDirection::North: forward_z = -1.0f; right_x = 1.0f; break;
        case FacingDirection::South: forward_z = 1.0f; right_x = -1.0f; break;
        case FacingDirection::East: forward_x = 1.0f; right_z = 1.0f; break;
        case FacingDirection::West: forward_x = -1.0f; right_z = -1.0f; break;
    }
    return {
        forward_x * tuning.landing_forward_pixels + right_x * tuning.landing_right_pixels,
        forward_z * tuning.landing_forward_pixels + right_z * tuning.landing_right_pixels,
    };
}

float forcedMovementSpeed(
    float tile_size, int tiles, double action_duration_seconds,
    const DoorDestinationTuning& tuning) {
    const int safe_tiles = std::max(1, tiles);
    const double duration = action_duration_seconds > 0.0
        ? action_duration_seconds
        : tuning.movement_seconds_per_tile * static_cast<double>(safe_tiles);
    return static_cast<float>(std::max(1.0, tile_size * safe_tiles / std::max(0.01, duration)));
}

std::optional<DoorTriggerHit> findDoorTrigger(
    const std::vector<characters::LoadedWorldChunk>& chunks,
    int,
    int,
    int to_world_x,
    int to_world_y,
    int step_dx,
    int step_dy) {
    const FacingDirection approach = directionForStep(step_dx, step_dy);
    for (const characters::LoadedWorldChunk& chunk : chunks) {
        for (const DoorTriggerConfig& trigger : chunk.scene.door_triggers) {
            if (chunk.origin_tile_x + trigger.tile_x != to_world_x ||
                chunk.origin_tile_y + trigger.tile_y != to_world_y) {
                continue;
            }
            if (std::find(trigger.allowed_directions.begin(), trigger.allowed_directions.end(), approach) ==
                trigger.allowed_directions.end()) {
                continue;
            }
            return DoorTriggerHit{&chunk, &trigger};
        }
    }
    return std::nullopt;
}

std::optional<DoorDestination> resolveDoorDestination(
    const std::vector<characters::LoadedWorldChunk>& chunks,
    const DoorTriggerHit& hit) {
    if (!hit.chunk || !hit.trigger) return std::nullopt;
    const auto link = std::find_if(hit.chunk->scene.links.begin(), hit.chunk->scene.links.end(), [&](const MapLinkConfig& item) {
        return item.id == hit.trigger->link_id;
    });
    if (link == hit.chunk->scene.links.end()) return std::nullopt;
    const auto destination_chunk = std::find_if(chunks.begin(), chunks.end(), [&](const characters::LoadedWorldChunk& item) {
        return item.id == link->destination_map_id || item.scene.id == link->destination_map_id;
    });
    if (destination_chunk == chunks.end()) return std::nullopt;
    const auto anchor = std::find_if(destination_chunk->scene.anchors.begin(), destination_chunk->scene.anchors.end(), [&](const MapAnchorConfig& item) {
        return item.id == link->destination_anchor_id;
    });
    if (anchor == destination_chunk->scene.anchors.end()) return std::nullopt;
    return DoorDestination{
        &*destination_chunk,
        destination_chunk->origin_tile_x + anchor->tile_x,
        destination_chunk->origin_tile_y + anchor->tile_y,
        anchor->facing};
}

const scripts::OverworldScript* findDoorScript(
    const scripts::ScriptCatalog& catalog,
    const std::string& script_id) {
    const auto script = std::find_if(catalog.scripts.begin(), catalog.scripts.end(), [&](const scripts::OverworldScript& item) {
        return item.kind == scripts::ScriptKind::Door && item.id == script_id && item.valid;
    });
    return script == catalog.scripts.end() ? nullptr : &*script;
}

bool DoorSequenceController::start(const scripts::OverworldScript* script, DoorTriggerHit hit) {
    if (!script || script->kind != scripts::ScriptKind::Door || !hit.trigger || !hit.chunk) return false;
    script_ = script;
    hit_chunk_ = *hit.chunk;
    hit_trigger_ = *hit.trigger;
    hit_ = DoorTriggerHit{&hit_chunk_, &hit_trigger_};
    action_index_ = 0;
    return true;
}

const scripts::ScriptAction* DoorSequenceController::currentAction() const {
    if (!script_ || action_index_ >= script_->actions.size()) return nullptr;
    return &script_->actions[action_index_];
}

void DoorSequenceController::advance() {
    if (!script_) return;
    ++action_index_;
    if (action_index_ >= script_->actions.size()) cancel();
}

void DoorSequenceController::cancel() {
    script_ = nullptr;
    hit_ = {};
    action_index_ = 0;
}

void ForcedDoorMoveController::start(FacingDirection direction, int tiles) {
    direction_ = direction;
    remaining_tiles_ = std::max(1, tiles);
    active_ = true;
}

bool ForcedDoorMoveController::shouldRequestStep(bool player_moving) const {
    return active_ && !player_moving && remaining_tiles_ > 0;
}

void ForcedDoorMoveController::reportStepAttempt(bool blocked) {
    if (!active_) return;
    if (blocked) {
        remaining_tiles_ = 0;
        return;
    }
    if (remaining_tiles_ > 0) --remaining_tiles_;
}

void ForcedDoorMoveController::reportMovementState(bool player_moving) {
    if (active_ && !player_moving && remaining_tiles_ <= 0) active_ = false;
}

void ForcedDoorMoveController::cancel() {
    remaining_tiles_ = 0;
    active_ = false;
}

} // namespace pr::gameplay::world3d::doors
