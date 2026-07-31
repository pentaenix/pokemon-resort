#include "gameplay/world3d/npc/NpcActorDriver.hpp"
#include "gameplay/world3d/npc/NpcPopulationPolicy.hpp"

#include "gameplay/world3d/interactions/InteractionSequence.hpp"

#include "core/app/AppPaths.hpp"
#include "core/config/ConfigLoader.hpp"
#include "core/config/Json.hpp"
#include "core/save/SavePaths.hpp"
#include "gameplay/world3d/data/JsonOverworldLoader.hpp"
#include "gameplay/world3d/followers/FollowerConfig.hpp"
#include "gameplay/world3d/followers/NatureIdlePlanner.hpp"
#include "gameplay/world3d/terrain/TerrainSurface.hpp"
#include "resort/services/PokemonResortService.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <utility>

namespace pr::gameplay::world3d::npc {

namespace fs = std::filesystem;

namespace {

std::string strOr(const JsonValue* value, const std::string& fallback) {
    return value && value->isString() ? value->asString() : fallback;
}

int intOr(const JsonValue* value, int fallback) {
    return value && value->isNumber() ? static_cast<int>(value->asNumber()) : fallback;
}

double numOr(const JsonValue* value, double fallback) {
    return value && value->isNumber() ? value->asNumber() : fallback;
}

bool boolOr(const JsonValue* value, bool fallback) {
    return value && value->isBool() ? value->asBool() : fallback;
}

FacingDirection facingForStep(int dx, int dy) {
    if (dx < 0) return FacingDirection::West;
    if (dx > 0) return FacingDirection::East;
    if (dy < 0) return FacingDirection::North;
    return FacingDirection::South;
}

FacingDirection facingFromString(const std::string& value, FacingDirection fallback = FacingDirection::South) {
    std::string lower;
    lower.reserve(value.size());
    for (unsigned char ch : value) {
        lower.push_back(static_cast<char>(std::tolower(ch)));
    }
    if (lower == "north" || lower == "up") return FacingDirection::North;
    if (lower == "east" || lower == "right") return FacingDirection::East;
    if (lower == "south" || lower == "down") return FacingDirection::South;
    if (lower == "west" || lower == "left") return FacingDirection::West;
    return fallback;
}

NpcBehaviorKind behaviorFromString(const std::string& value) {
    std::string lower;
    lower.reserve(value.size());
    for (unsigned char ch : value) {
        lower.push_back(static_cast<char>(std::tolower(ch)));
    }
    if (lower == "idle") return NpcBehaviorKind::IdleRotate;
    if (lower == "slow_rotate" || lower == "slowrotate") return NpcBehaviorKind::SlowRotate;
    if (lower == "wander") return NpcBehaviorKind::Wander;
    if (lower == "destination_roam" || lower == "destinationroam" || lower == "long_roam") {
        return NpcBehaviorKind::DestinationRoam;
    }
    if (lower == "path") return NpcBehaviorKind::Path;
    return NpcBehaviorKind::Static;
}

NpcBehaviorKind behaviorFromScriptId(const std::string& value, NpcBehaviorKind fallback) {
    std::string lower;
    lower.reserve(value.size());
    for (unsigned char ch : value) {
        lower.push_back(static_cast<char>(std::tolower(ch)));
    }
    if (lower == "npc_wander") return NpcBehaviorKind::Wander;
    if (lower == "npc_patrol") return NpcBehaviorKind::Path;
    if (lower == "npc_follow") return NpcBehaviorKind::FollowActor;
    if (lower == "npc_idle_rotate") return NpcBehaviorKind::IdleRotate;
    if (lower == "npc_slow_rotate") return NpcBehaviorKind::SlowRotate;
    if (lower == "npc_destination_roam") return NpcBehaviorKind::DestinationRoam;
    if (lower == "npc_static") return NpcBehaviorKind::Static;
    return fallback;
}

PokemonCollisionMode pokemonCollisionModeFromString(const std::string& value) {
    std::string lower;
    lower.reserve(value.size());
    for (unsigned char ch : value) {
        lower.push_back(static_cast<char>(std::tolower(ch)));
    }
    if (lower == "swap") return PokemonCollisionMode::Swap;
    return PokemonCollisionMode::BlockCell;
}

std::string canonicalSpeciesId(std::string value) {
    std::string out;
    out.reserve(value.size());
    bool last_was_separator = false;
    for (unsigned char ch : value) {
        if (std::isalnum(ch)) {
            out.push_back(static_cast<char>(std::tolower(ch)));
            last_was_separator = false;
        } else if (!last_was_separator) {
            out.push_back('_');
            last_was_separator = true;
        }
    }
    while (!out.empty() && out.front() == '_') out.erase(out.begin());
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out;
}

SDL_Rect firstSouthFrameRect(const CharacterSpriteDefinition& character) {
    int frame = 0;
    if (!character.idle.frames.empty()) {
        frame = std::max(0, character.idle.frames.front());
    }
    return SDL_Rect{
        frame * std::max(1, character.frame_width),
        character.row_south * std::max(1, character.frame_height),
        std::max(1, character.frame_width),
        std::max(1, character.frame_height)};
}

std::vector<std::pair<int, int>> adjacentTilesByDistance(int tx, int ty, int from_x, int from_y) {
    std::vector<std::pair<int, int>> tiles{
        {tx, ty - 1},
        {tx + 1, ty},
        {tx, ty + 1},
        {tx - 1, ty},
    };
    std::stable_sort(tiles.begin(), tiles.end(), [from_x, from_y](const auto& a, const auto& b) {
        const int da = std::abs(a.first - from_x) + std::abs(a.second - from_y);
        const int db = std::abs(b.first - from_x) + std::abs(b.second - from_y);
        return da < db;
    });
    return tiles;
}

std::optional<std::pair<int, int>> parseTilePair(const JsonValue* value) {
    if (!value) {
        return std::nullopt;
    }
    if (value->isArray()) {
        const auto& arr = value->asArray();
        if (arr.size() >= 2 && arr[0].isNumber() && arr[1].isNumber()) {
            return std::pair<int, int>{
                static_cast<int>(arr[0].asNumber()),
                static_cast<int>(arr[1].asNumber())};
        }
    }
    if (value->isObject()) {
        return std::pair<int, int>{
            intOr(value->get("x"), 0),
            intOr(value->get("y"), 0)};
    }
    return std::nullopt;
}

} // namespace

NpcActorDriver::NpcActorDriver(std::string project_root, const SceneConfig& scene)
    : project_root_(std::move(project_root)),
      scene_(&scene),
      terrain_query_(characters::makeLocalCharacterTerrainQuery(scene)),
      movement_config_(characters::loadCharacterMovementConfig(project_root_)),
      resort_pokemon_spawn_config_(loadResortPokemonSpawnConfig(project_root_)) {}

void NpcActorDriver::initializeDefaultSceneActors(const camera::Vec3& player_position) {
    actors_.clear();
    reserved_tiles_.clear();
    const float tile_size = std::max(1.0f, scene_->grid.tile_size);
    reserved_tiles_.push_back({
        static_cast<int>(std::floor(player_position.x / tile_size)),
        static_cast<int>(std::floor(player_position.z / tile_size))});

    if (!allowsGlobalDefaultPopulation(*scene_)) {
        return;
    }

    const std::vector<NpcActorDefinition> testing_actors = loadTestingActorDefinitions();
    std::vector<std::size_t> owner_indices;
    for (const NpcActorDefinition& definition : testing_actors) {
        std::optional<std::size_t> added = definition.use_random_spawn
            ? addActorAtRandomValidTile(definition)
            : addActorAtTile(definition, definition.spawn_tile_x, definition.spawn_tile_y);
        if (added) {
            owner_indices.push_back(*added);
        }
    }
    for (const std::size_t index : owner_indices) {
        if (index < actors_.size() &&
            actors_[index].definition.kind == NpcActorKind::Human &&
            actors_[index].definition.spawn_partner_pokemon) {
            addPartnerPokemonForActor(actors_[index]);
        }
    }

    const std::vector<ResortPokemonSpawnInfo> resort_pokemon = resortPokemonSpawnList();
    for (std::size_t i = 1; i < resort_pokemon.size(); ++i) {
        const ResortPokemonSpawnInfo& pokemon = resort_pokemon[i];
        NpcActorDefinition definition{};
        definition.id = pokemon.id;
        definition.character_package_path = pokemon.character_package_path;
        definition.kind = NpcActorKind::Pokemon;
        definition.behavior = NpcBehaviorKind::Wander;
        definition.movement_speed_profile = "walk";
        definition.script_id = "npc_wander";
        definition.pokemon_form_id = pokemon.form_id;
        definition.pokemon_shiny = pokemon.shiny;
        definition.pokemon_species_slug = pokemon.species_slug;
        definition.pokemon_display_name = pokemon.display_name;
        definition.resort_box_id = pokemon.box_id;
        definition.resort_slot_index = pokemon.slot_index;
        addActorAtRandomValidTile(definition);
    }

    if (!testing_actors.empty()) {
        return;
    }

    const fs::path watanave_path = fs::path(project_root_) / "assets" / "characters" / "npc" / "watanave.charbin";
    if (!fs::exists(watanave_path)) {
        return;
    }

    const std::optional<std::size_t> watanave = addActorAtRandomValidTile(NpcActorDefinition{
        "npc_watanave",
        watanave_path.string(),
        NpcActorKind::Human,
        NpcBehaviorKind::Wander,
        {},
        "walk"});
    if (!watanave) {
        return;
    }

    try {
        const data::CharacterPackageMetadata metadata =
            data::loadCharacterPackageMetadata(watanave_path.string());
        if (!metadata.partner_pokemon || metadata.partner_pokemon->pokemon_id.empty()) {
            return;
        }
        const std::string partner_path = pokemonCharbinPathForSpecies(metadata.partner_pokemon->pokemon_id);
        if (partner_path.empty()) {
            std::cerr << "[Overworld3D][NPC] Watanave partner Pokemon package not found for species '"
                      << metadata.partner_pokemon->pokemon_id << "'\n";
            return;
        }
        addActorAtRandomValidTile(NpcActorDefinition{
            "npc_watanave_partner",
            partner_path,
            NpcActorKind::Pokemon,
            NpcBehaviorKind::FollowActor,
            "npc_watanave",
            "walk"});
    } catch (const std::exception& ex) {
        std::cerr << "[Overworld3D][NPC] Could not load Watanave partner metadata: "
                  << ex.what() << '\n';
    }
}

void NpcActorDriver::setTerrainQuery(std::shared_ptr<characters::CharacterTerrainQuery> terrain_query) {
    if (!terrain_query) return;
    terrain_query_ = std::move(terrain_query);
    for (Actor& actor : actors_) {
        actor.motor.setTerrainQuery(terrain_query_);
        if (!actor.moving) {
            actor.terrain_binding = terrain_query_->bindActorStanding(
                actor.tile_x, actor.tile_y, actor.position.x, actor.position.z);
            actor.position.y = actor.terrain_binding.simulation_y;
            actor.motor.resetToTile(actor.tile_x, actor.tile_y, actor.position);
        }
    }
}

void NpcActorDriver::update(double dt) {
    for (std::size_t i = 0; i < actors_.size(); ++i) {
        Actor& actor = actors_[i];
        const bool interaction_locked = interaction_locked_actor_ && *interaction_locked_actor_ == i;
        if (actor.animator) {
            if (!actor.definition.persistent_activity_id.empty()) {
                if (actor.moving && actor.animator->activitySessionActive()) {
                    // Persistent idle sheets are often single-direction strips.
                    // A collision swap may move an otherwise static NPC, so
                    // switch immediately to its directional walk sheet.
                    actor.animator->cancelActivitySession();
                } else if (!actor.moving && !actor.animator->activitySessionActive()) {
                    actor.animator->startActivitySession(actor.definition.persistent_activity_id);
                }
            }
            bool swimming = false;
            if (actor.definition.kind == NpcActorKind::Pokemon &&
                scene_->water_terrain.pokemon_swim_animation_enabled && terrain_query_) {
                const float tile_size = terrain_query_->tileSize();
                const int water_tx = static_cast<int>(std::floor(actor.position.x / tile_size));
                const int water_ty = static_cast<int>(std::floor(actor.position.z / tile_size));
                swimming = terrain_query_->tileIsActualWater(water_tx, water_ty);
            }
            actor.animator->setSwimming(swimming);
            actor.animator->setMoving(interaction_locked ? false : actor.moving);
            actor.animator->setRunning(actor.running);
            actor.animator->setPlaybackSpeedMultiplier(
                actor.moving && !interaction_locked
                    ? static_cast<double>(actor.move_speed_units_per_second / std::max(1.0f, movement_config_.walkSpeed()))
                    : 1.0);
            const FacingDirection animation_facing =
                actor.animator->activitySessionActive() && actor.definition.persistent_activity_facing
                ? *actor.definition.persistent_activity_facing
                : actor.facing;
            actor.animator->setFacing(animation_facing);
            if (!interaction_locked || actor.animator->activitySessionActive()) {
                actor.animator->update(dt);
            }
            actor.source_rect = actor.animator->sourceRect();
        }
        if (actor.interaction_jump_elapsed_seconds >= 0.0) {
            actor.interaction_jump_elapsed_seconds += std::max(0.0, dt);
            if (actor.interaction_jump_elapsed_seconds >= 0.35) {
                actor.interaction_jump_elapsed_seconds = -1.0;
                actor.interaction_jump_height_pixels = 0;
            }
        }

        if (interaction_locked) {
            actor.moving = false;
            actor.path.clear();
            actor.motor.stop();
            actor.target_tile_x = actor.tile_x;
            actor.target_tile_y = actor.tile_y;
            actor.wait_seconds = 0.25;
            continue;
        }

        if (actor.moving) {
            const bool finished = actor.motor.update(dt);
            actor.move_t = actor.motor.moveT();
            actor.position = actor.motor.position();
            actor.terrain_binding = actor.motor.terrainBinding();
            if (finished) {
                finishMovement(actor);
            }
        }
    }

    for (std::size_t i = 0; i < actors_.size(); ++i) {
        Actor& actor = actors_[i];
        if (interaction_locked_actor_ && *interaction_locked_actor_ == i) {
            continue;
        }
        if (actor.moving) {
            continue;
        }

        if (tryEvacuateReservedTile(actor)) {
            continue;
        }

        if (actor.definition.behavior == NpcBehaviorKind::IdleRotate) {
            updateIdleRotate(actor, dt);
        } else if (actor.definition.behavior == NpcBehaviorKind::SlowRotate) {
            updateSlowRotate(actor, dt);
        } else if (actor.definition.behavior == NpcBehaviorKind::Wander) {
            updateRandomWalk(actor, dt);
        } else if (actor.definition.behavior == NpcBehaviorKind::DestinationRoam) {
            updateDestinationRoam(actor, dt);
        } else if (actor.definition.behavior == NpcBehaviorKind::Path) {
            updatePath(actor, dt);
        } else if (actor.definition.behavior == NpcBehaviorKind::FollowActor) {
            const std::optional<std::size_t> target = findActor(actor.definition.follow_target_id);
            if (target && *target != i) {
                updateFollowActor(actor, actors_[*target], dt);
            }
        }
    }
}

void NpcActorDriver::setPlayerReservedTile(int tx, int ty) {
    if (reserved_tiles_.empty()) {
        reserved_tiles_.push_back({tx, ty});
    } else {
        reserved_tiles_.front() = {tx, ty};
    }
    player_reserved_tile_count_ = std::max<std::size_t>(1U, std::min<std::size_t>(player_reserved_tile_count_, reserved_tiles_.size()));
}

void NpcActorDriver::setReservedTiles(std::vector<std::pair<int, int>> tiles, std::size_t player_reserved_tile_count) {
    reserved_tiles_ = std::move(tiles);
    player_reserved_tile_count_ = std::min(player_reserved_tile_count, reserved_tiles_.size());
}

bool NpcActorDriver::canPlayerEnterTile(int from_tx, int from_ty, int to_tx, int to_ty) {
    for (Actor& actor : actors_) {
        const bool actor_on_tile = actor.tile_x == to_tx && actor.tile_y == to_ty;
        const bool actor_targeting_tile =
            actor.moving && actor.target_tile_x == to_tx && actor.target_tile_y == to_ty;
        if (!actor_on_tile && !actor_targeting_tile) {
            continue;
        }

        if (actor.definition.kind != NpcActorKind::Pokemon ||
            pokemon_collision_mode_ == PokemonCollisionMode::BlockCell ||
            followerPokemonSettledWithTarget(actor)) {
            return false;
        }

        if (actor_on_tile && !actor.moving) {
            actorCanYieldFromTile(actor, from_tx, from_ty);
        }
    }
    return true;
}

std::optional<std::string> NpcActorDriver::interactableActorIdAtTile(int tx, int ty) const {
    for (const Actor& actor : actors_) {
        if (actor.moving) {
            continue;
        }
        if (actor.tile_x == tx && actor.tile_y == ty) {
            return actor.definition.id;
        }
    }
    return std::nullopt;
}

bool NpcActorDriver::setInteractionLockedActor(const std::string& actor_id) {
    const std::optional<std::size_t> index = findActor(actor_id);
    if (!index || *index >= actors_.size()) {
        interaction_locked_actor_.reset();
        return false;
    }
    Actor& actor = actors_[*index];
    if (actor.moving) {
        interaction_locked_actor_.reset();
        return false;
    }
    actor.path.clear();
    actor.wait_seconds = 0.25;
    actor.target_tile_x = actor.tile_x;
    actor.target_tile_y = actor.tile_y;
    interaction_locked_actor_ = *index;
    return true;
}

std::optional<NpcInteractionActorInfo> NpcActorDriver::interactionActorInfo(const std::string& actor_id) const {
    const std::optional<std::size_t> index = findActor(actor_id);
    if (!index || *index >= actors_.size()) {
        return std::nullopt;
    }
    const Actor& actor = actors_[*index];
    NpcInteractionActorInfo info{};
    info.kind = actor.definition.kind;
    info.tile_x = actor.tile_x;
    info.tile_y = actor.tile_y;
    info.display_name = actor.display_name.empty() ? actor.definition.id : actor.display_name;
    info.species_name = actor.character.species_name;
    info.pokemon_size = actor.character.pokemon_size;
    info.pokemon_types = actor.character.pokemon_types;
    info.dialogue_lines = actor.dialogue_lines;
    info.npc_interaction_mode = actor.npc_interaction_mode;
    info.runtime_interaction_script_id = actor.runtime_interaction_script_id;
    info.species_slug = actor.definition.pokemon_species_slug;
    info.form_id = actor.definition.pokemon_form_id;
    info.shiny = actor.definition.pokemon_shiny;
    info.resort_box_id = actor.definition.resort_box_id;
    info.resort_slot_index = actor.definition.resort_slot_index;
    return info;
}

bool NpcActorDriver::faceInteractionLockedActorTowardTile(int tx, int ty) {
    if (!interaction_locked_actor_ || *interaction_locked_actor_ >= actors_.size()) {
        return false;
    }
    Actor& actor = actors_[*interaction_locked_actor_];
    actor.facing = interactions::facingTowardTiles(actor.tile_x, actor.tile_y, tx, ty, actor.facing);
    if (actor.animator) {
        actor.animator->setFacing(actor.facing);
        actor.source_rect = actor.animator->sourceRect();
    }
    return true;
}

bool NpcActorDriver::faceInteractionLockedActor(FacingDirection facing) {
    if (!interaction_locked_actor_ || *interaction_locked_actor_ >= actors_.size()) {
        return false;
    }
    Actor& actor = actors_[*interaction_locked_actor_];
    actor.facing = facing;
    if (actor.animator) {
        actor.animator->setFacing(actor.facing);
        actor.source_rect = actor.animator->sourceRect();
    }
    return true;
}

bool NpcActorDriver::startInteractionSessionForLockedActor() {
    if (!interaction_locked_actor_ || *interaction_locked_actor_ >= actors_.size()) {
        return false;
    }
    Actor& actor = actors_[*interaction_locked_actor_];
    if (actor.definition.kind != NpcActorKind::Pokemon || !actor.animator) {
        return false;
    }
    const std::optional<std::string> action_id =
        interactions::interactionActivityIdForPokemonSize(actor.character.pokemon_size);
    return action_id && actor.animator->startActivitySession(*action_id);
}

void NpcActorDriver::requestInteractionSessionExitForLockedActor() {
    if (!interaction_locked_actor_ || *interaction_locked_actor_ >= actors_.size()) {
        return;
    }
    Actor& actor = actors_[*interaction_locked_actor_];
    if (actor.animator) {
        actor.animator->requestActivityExit();
    }
}

bool NpcActorDriver::lockedActorInteractionSessionReady() const {
    if (!interaction_locked_actor_ || *interaction_locked_actor_ >= actors_.size()) {
        return true;
    }
    const Actor& actor = actors_[*interaction_locked_actor_];
    return !actor.animator || !actor.animator->activitySessionActive() || actor.animator->activityStayActive();
}

bool NpcActorDriver::lockedActorInteractionSessionFinished() const {
    if (!interaction_locked_actor_ || *interaction_locked_actor_ >= actors_.size()) {
        return true;
    }
    const Actor& actor = actors_[*interaction_locked_actor_];
    return !actor.animator || actor.animator->activityFinished();
}

bool NpcActorDriver::triggerInteractionJump(int height_pixels) {
    if (!interaction_locked_actor_ || *interaction_locked_actor_ >= actors_.size()) {
        return false;
    }
    Actor& actor = actors_[*interaction_locked_actor_];
    if (actor.definition.kind != NpcActorKind::Pokemon) {
        return false;
    }
    actor.interaction_jump_elapsed_seconds = 0.0;
    actor.interaction_jump_height_pixels = std::max(1, height_pixels);
    return true;
}

void NpcActorDriver::clearInteractionLockedActor() {
    interaction_locked_actor_.reset();
}

void NpcActorDriver::collectBillboardDraws(
    const camera::Gen4FollowCamera& camera,
    int viewport_w,
    int viewport_h,
    std::vector<rendering::CharacterBillboardDraw>& out) const {
    for (const Actor& actor : actors_) {
        rendering::CharacterBillboardDraw draw{};
        draw.character = &actor.character;
        draw.source_rect = actor.source_rect;
        draw.activity_id = actor.animator ? actor.animator->textureSheetId() : std::string{};
        draw.draw_shadow = !actor.animator || !actor.animator->swimming();
        draw.use_run_texture = actor.animator ? actor.animator->running() : actor.running;
        int jump_offset_y_px = 0;
        if (actor.interaction_jump_elapsed_seconds >= 0.0) {
            const double phase = std::clamp(actor.interaction_jump_elapsed_seconds / 0.35, 0.0, 1.0);
            jump_offset_y_px = -static_cast<int>(std::lround(
                4.0 * phase * (1.0 - phase) * static_cast<double>(actor.interaction_jump_height_pixels)));
        }
        draw.placement = rendering::buildCharacterBillboardPlacement(
            *scene_,
            camera,
            actor.terrain_binding,
            actor.character,
            actor.position,
            actor.source_rect,
            viewport_w,
            viewport_h,
            1.0f,
            jump_offset_y_px);
        out.push_back(draw);
    }
}

std::vector<NpcEffectActorSnapshot> NpcActorDriver::effectActorSnapshots() const {
    std::vector<NpcEffectActorSnapshot> out;
    out.reserve(actors_.size());
    for (const Actor& actor : actors_) {
        const bool actual_water = terrain_query_
            ? terrain_query_->tileIsActualWater(actor.tile_x, actor.tile_y)
            : scene_ && terrain::isActualWaterTile(*scene_, actor.tile_x, actor.tile_y);
        out.push_back(NpcEffectActorSnapshot{
            actor.definition.id,
            actor.position,
            actual_water,
            actor.moving});
    }
    return out;
}

std::optional<std::size_t> NpcActorDriver::addActorAtRandomValidTile(const NpcActorDefinition& definition) {
    const std::optional<std::pair<int, int>> tile = randomValidTile();
    if (!tile) {
        return std::nullopt;
    }

    return addActor(definition, tile->first, tile->second);
}

std::optional<std::size_t> NpcActorDriver::addActorAtTile(const NpcActorDefinition& definition, int tx, int ty) {
    if (!validWalkTile(tx, ty) || tileOccupied(tx, ty) || tileReserved(tx, ty)) {
        std::cerr << "[Overworld3D][NPC] Skipping actor '" << definition.id
                  << "': spawn tile is invalid or occupied (" << tx << ", " << ty << ")\n";
        return std::nullopt;
    }
    return addActor(definition, tx, ty);
}

std::optional<std::size_t> NpcActorDriver::addActor(const NpcActorDefinition& definition, int tx, int ty) {
    try {
        Actor actor{};
        actor.definition = definition;
        actor.runtime_interaction_script_id = definition.runtime_interaction_script_id;
        actor.character = data::loadCharacterDefinition(
            project_root_,
            definition.character_package_path,
            data::CharacterAppearanceSelection{definition.pokemon_form_id, definition.pokemon_shiny});
        std::string speed_profile = definition.movement_speed_profile.empty()
            ? "walk"
            : definition.movement_speed_profile;
        try {
            const data::CharacterPackageMetadata metadata =
                data::loadCharacterPackageMetadata(definition.character_package_path);
            if (!metadata.movement_speed_profile.empty()) {
                speed_profile = metadata.movement_speed_profile;
            }
            actor.display_name = metadata.display_name;
            actor.dialogue_lines = metadata.dialogue_lines;
            actor.npc_interaction_mode = metadata.npc_interaction_mode;
        } catch (const std::exception&) {
            // Character loading already validates the package; metadata is optional for movement.
        }
        if (!definition.pokemon_display_name.empty()) actor.display_name = definition.pokemon_display_name;
        actor.running = speed_profile == "run" && actor.character.has_run;
        actor.move_speed_units_per_second = actor.running
            ? movement_config_.runSpeed()
            : movement_config_.speedForProfile(speed_profile);
        actor.base_move_speed_units_per_second = actor.move_speed_units_per_second;
        actor.animator = std::make_unique<characters::SpriteSheetAnimator>(actor.character);
        actor.source_rect = firstSouthFrameRect(actor.character);
        actor.tile_x = tx;
        actor.tile_y = ty;
        actor.spawn_tile_x = tx;
        actor.spawn_tile_y = ty;
        actor.target_tile_x = actor.tile_x;
        actor.target_tile_y = actor.tile_y;
        actor.facing = definition.facing;
        const float tile_size = std::max(1.0f, scene_->grid.tile_size);
        const float wx = (static_cast<float>(actor.tile_x) + 0.5f) * tile_size;
        const float wz = (static_cast<float>(actor.tile_y) + 0.5f) * tile_size;
        actor.terrain_binding = terrain_query_
            ? terrain_query_->bindActorStanding(actor.tile_x, actor.tile_y, wx, wz)
            : terrain::bindActorStanding(*scene_, actor.tile_x, actor.tile_y, wx, wz);
        actor.position = camera::Vec3{wx, actor.terrain_binding.simulation_y, wz};
        actor.motor.setTerrainQuery(terrain_query_);
        actor.motor.setMoveSpeedUnitsPerSecond(actor.move_speed_units_per_second);
        actor.motor.resetToTile(actor.tile_x, actor.tile_y, actor.position);
        actor.move_start = actor.position;
        actor.move_target = actor.position;
        if (actor.animator) {
            actor.animator->setFacing(actor.facing);
            if (!definition.persistent_activity_id.empty()) {
                actor.animator->startActivitySession(definition.persistent_activity_id);
            }
            actor.source_rect = actor.animator->sourceRect();
        }
        actor.wait_seconds = 0.25 + (static_cast<double>(rng_() % 80U) / 100.0);
        actors_.push_back(std::move(actor));
        return actors_.size() - 1U;
    } catch (const std::exception& ex) {
        std::cerr << "[Overworld3D][NPC] Skipping actor '" << definition.id
                  << "': " << ex.what() << '\n';
        return std::nullopt;
    }
}

void NpcActorDriver::startStep(Actor& actor, int dx, int dy) {
    const int tx = actor.tile_x + dx;
    const int ty = actor.tile_y + dy;
    startStepToTile(actor, tx, ty);
}

void NpcActorDriver::startStepToTile(Actor& actor, int tx, int ty, bool allow_reserved_tile) {
    if (!validWalkTile(tx, ty) ||
        (actor.definition.kind == NpcActorKind::Human && terrain::isActualWaterTile(*scene_, tx, ty)) ||
        tileOccupied(tx, ty, &actor) ||
        (!allow_reserved_tile && tileReserved(tx, ty))) {
        return;
    }
    if (!canStepBetweenTiles(actor.tile_x, actor.tile_y, tx, ty)) {
        return;
    }

    const int dx = tx - actor.tile_x;
    const int dy = ty - actor.tile_y;
    actor.motor.setTerrainQuery(terrain_query_);
    actor.motor.setMoveSpeedUnitsPerSecond(actor.move_speed_units_per_second);
    const characters::GridActorMotor::StepResult step = actor.motor.tryStartStep(dx, dy);
    if (!step.started) {
        return;
    }
    actor.facing = facingForStep(dx, dy);
    actor.target_tile_x = tx;
    actor.target_tile_y = ty;
    actor.move_start = actor.position;
    actor.move_target = actor.motor.moveTarget();
    actor.move_t = 0.0f;
    actor.moving = true;
}

void NpcActorDriver::updateRandomWalk(Actor& actor, double dt) {
    actor.wait_seconds -= dt;
    if (actor.wait_seconds > 0.0) {
        return;
    }

    static constexpr int kDirs[4][2] = {
        {0, -1},
        {1, 0},
        {0, 1},
        {-1, 0},
    };
    std::vector<std::pair<int, int>> candidates;
    candidates.reserve(4);
    for (const auto& dir : kDirs) {
        const int tx = actor.tile_x + dir[0];
        const int ty = actor.tile_y + dir[1];
        if (validWalkTile(tx, ty) &&
            (actor.definition.kind != NpcActorKind::Human || !terrain::isActualWaterTile(*scene_, tx, ty)) &&
            canStepBetweenTiles(actor.tile_x, actor.tile_y, tx, ty) &&
            !tileOccupied(tx, ty, &actor) && !tileReserved(tx, ty)) {
            candidates.push_back({dir[0], dir[1]});
        }
    }

    if (candidates.empty()) {
        actor.wait_seconds = 0.5;
        return;
    }

    std::uniform_int_distribution<std::size_t> pick(0, candidates.size() - 1U);
    const auto [dx, dy] = candidates[pick(rng_)];
    startStep(actor, dx, dy);
}

void NpcActorDriver::updateIdleRotate(Actor& actor, double dt) {
    actor.wait_seconds -= dt;
    if (actor.wait_seconds > 0.0) {
        return;
    }
    static constexpr FacingDirection kDirections[4] = {
        FacingDirection::South,
        FacingDirection::West,
        FacingDirection::East,
        FacingDirection::North,
    };
    std::uniform_int_distribution<int> pick(0, 3);
    actor.facing = kDirections[pick(rng_)];
    actor.wait_seconds = 1.0 + (static_cast<double>(rng_() % 180U) / 100.0);
}

void NpcActorDriver::updateSlowRotate(Actor& actor, double dt) {
    actor.wait_seconds -= dt;
    if (actor.wait_seconds > 0.0) return;
    static constexpr FacingDirection kDirections[4] = {
        FacingDirection::South,
        FacingDirection::West,
        FacingDirection::East,
        FacingDirection::North,
    };
    std::uniform_int_distribution<int> pick(0, 3);
    FacingDirection next = actor.facing;
    for (int attempt = 0; attempt < 4 && next == actor.facing; ++attempt) {
        next = kDirections[pick(rng_)];
    }
    actor.facing = next;
    const double low = std::max(0.1, actor.definition.rotate_wait_min_seconds);
    const double high = std::max(low, actor.definition.rotate_wait_max_seconds);
    actor.wait_seconds = std::uniform_real_distribution<double>(low, high)(rng_);
}

void NpcActorDriver::updateDestinationRoam(Actor& actor, double dt) {
    actor.wait_seconds -= dt;
    if (actor.wait_seconds > 0.0) return;

    if (actor.path.empty()) {
        const int width = std::max(0, scene_->grid.width);
        const int height = std::max(0, scene_->grid.height);
        if (width == 0 || height == 0) {
            actor.wait_seconds = 1.0;
            return;
        }
        const int start_index = actor.tile_y * width + actor.tile_x;
        std::vector<int> previous(static_cast<std::size_t>(width * height), -1);
        std::vector<int> distance(static_cast<std::size_t>(width * height), -1);
        std::deque<std::pair<int, int>> frontier;
        frontier.push_back({actor.tile_x, actor.tile_y});
        distance[static_cast<std::size_t>(start_index)] = 0;
        static constexpr int kDirs[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
        while (!frontier.empty()) {
            const auto [x, y] = frontier.front();
            frontier.pop_front();
            const int from_index = y * width + x;
            for (const auto& dir : kDirs) {
                const int nx = x + dir[0];
                const int ny = y + dir[1];
                if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
                const int next_index = ny * width + nx;
                if (distance[static_cast<std::size_t>(next_index)] >= 0 ||
                    !validWalkTile(nx, ny) ||
                    terrain::isActualWaterTile(*scene_, nx, ny) ||
                    !canStepBetweenTiles(x, y, nx, ny)) {
                    continue;
                }
                distance[static_cast<std::size_t>(next_index)] =
                    distance[static_cast<std::size_t>(from_index)] + 1;
                previous[static_cast<std::size_t>(next_index)] = from_index;
                frontier.push_back({nx, ny});
            }
        }

        std::vector<int> goals;
        const int minimum_distance = std::max(2, actor.definition.roam_min_distance_tiles);
        for (int index = 0; index < width * height; ++index) {
            if (distance[static_cast<std::size_t>(index)] < minimum_distance) continue;
            const int gx = index % width;
            const int gy = index / width;
            if (tileOccupied(gx, gy, &actor) || tileReserved(gx, gy)) continue;
            goals.push_back(index);
        }
        if (goals.empty()) {
            actor.wait_seconds = 1.0;
            return;
        }
        const int goal = goals[std::uniform_int_distribution<std::size_t>(0, goals.size() - 1U)(rng_)];
        std::vector<std::pair<int, int>> reverse_path;
        for (int cursor = goal; cursor != start_index && cursor >= 0;
             cursor = previous[static_cast<std::size_t>(cursor)]) {
            reverse_path.push_back({cursor % width, cursor / width});
        }
        for (auto it = reverse_path.rbegin(); it != reverse_path.rend(); ++it) {
            actor.path.push_back(*it);
        }
    }

    while (!actor.path.empty()) {
        const auto [next_x, next_y] = actor.path.front();
        if (next_x == actor.tile_x && next_y == actor.tile_y) {
            actor.path.pop_front();
            continue;
        }
        if (terrain::isActualWaterTile(*scene_, next_x, next_y) ||
            !validWalkTile(next_x, next_y) ||
            tileOccupied(next_x, next_y, &actor) ||
            tileReserved(next_x, next_y)) {
            actor.path.clear();
            actor.wait_seconds = 0.5;
            return;
        }
        actor.path.pop_front();
        startStepToTile(actor, next_x, next_y);
        return;
    }
}

void NpcActorDriver::updatePath(Actor& actor, double dt) {
    if (actor.definition.path_points.empty()) {
        updateIdleRotate(actor, dt);
        return;
    }

    actor.wait_seconds -= dt;
    if (actor.wait_seconds > 0.0) {
        return;
    }

    if (actor.path.empty()) {
        const NpcPathPoint& relative =
            actor.definition.path_points[actor.path_point_index % actor.definition.path_points.size()];
        const int goal_x = actor.spawn_tile_x + relative.x;
        const int goal_y = actor.spawn_tile_y + relative.y;
        actor.path_point_index = (actor.path_point_index + 1U) % actor.definition.path_points.size();
        if (goal_x == actor.tile_x && goal_y == actor.tile_y) {
            return;
        }
        if (!validWalkTile(goal_x, goal_y)) {
            actor.wait_seconds = 0.25;
            return;
        }
        const auto route = followers::buildFollowerPath(
            *scene_,
            followers::GridPoint{actor.tile_x, actor.tile_y},
            followers::GridPoint{goal_x, goal_y});
        for (const followers::GridPoint& step : route) {
            actor.path.push_back({step.x, step.y});
        }
    }

    while (!actor.path.empty()) {
        const auto [next_x, next_y] = actor.path.front();
        if (next_x == actor.tile_x && next_y == actor.tile_y) {
            actor.path.pop_front();
            continue;
        }
        if (std::abs(next_x - actor.tile_x) + std::abs(next_y - actor.tile_y) != 1) {
            actor.path.clear();
            break;
        }
        if (validWalkTile(next_x, next_y) &&
            !tileOccupied(next_x, next_y, &actor) &&
            !tileReserved(next_x, next_y)) {
            actor.path.pop_front();
            startStepToTile(actor, next_x, next_y);
            return;
        }
        actor.path.clear();
        break;
    }
    actor.wait_seconds = 0.25;
}

void NpcActorDriver::updateFollowActor(Actor& actor, const Actor& target, double dt) {
    actor.move_speed_units_per_second = std::max(
        actor.base_move_speed_units_per_second,
        target.running ? movement_config_.runSpeed() : target.move_speed_units_per_second);
    actor.running = target.running && actor.character.has_run;

    if (!actor.have_follow_last_target_tile) {
        actor.follow_last_target_tile_x = target.tile_x;
        actor.follow_last_target_tile_y = target.tile_y;
        actor.have_follow_last_target_tile = true;
    }
    if (target.tile_x != actor.follow_last_target_tile_x ||
        target.tile_y != actor.follow_last_target_tile_y) {
        if (actor.path.empty() ||
            actor.path.back().first != actor.follow_last_target_tile_x ||
            actor.path.back().second != actor.follow_last_target_tile_y) {
            actor.path.push_back({actor.follow_last_target_tile_x, actor.follow_last_target_tile_y});
            if (actor.path.size() > 32) {
                actor.path.pop_front();
            }
        }
        actor.follow_last_target_tile_x = target.tile_x;
        actor.follow_last_target_tile_y = target.tile_y;
        actor.wait_seconds = 0.0;
    }

    actor.wait_seconds -= dt;
    if (actor.wait_seconds > 0.0) {
        return;
    }

    const int dx = target.tile_x - actor.tile_x;
    const int dy = target.tile_y - actor.tile_y;
    const int manhattan = std::abs(dx) + std::abs(dy);
    if (manhattan <= 1 && actor.path.empty()) {
        if (actor.definition.follower_pokemon_freedom && tryFollowerFreedomStep(actor, target)) {
            return;
        }
        actor.facing = facingForStep(dx, dy == 0 ? 1 : dy);
        actor.wait_seconds = 0.2;
        return;
    }

    if (actor.path.empty()) {
        for (const auto& [goal_x, goal_y] : adjacentTilesByDistance(target.tile_x, target.tile_y, actor.tile_x, actor.tile_y)) {
            if (!validWalkTile(goal_x, goal_y) || tileOccupied(goal_x, goal_y, &actor) || tileReserved(goal_x, goal_y)) {
                continue;
            }
            const followers::GridPoint occupied{target.tile_x, target.tile_y};
            const auto path = followers::buildFollowerPath(
                *scene_,
                followers::GridPoint{actor.tile_x, actor.tile_y},
                followers::GridPoint{goal_x, goal_y},
                &occupied);
            for (const followers::GridPoint& step : path) {
                actor.path.push_back({step.x, step.y});
            }
            if (!actor.path.empty()) {
                break;
            }
        }
    }

    while (!actor.path.empty()) {
        const auto [next_x, next_y] = actor.path.front();
        if (next_x == actor.tile_x && next_y == actor.tile_y) {
            actor.path.pop_front();
            continue;
        }
        if (std::abs(next_x - actor.tile_x) + std::abs(next_y - actor.tile_y) != 1) {
            actor.path.clear();
            break;
        }
        if (validWalkTile(next_x, next_y) &&
            !tileOccupied(next_x, next_y, &actor) &&
            !tileReserved(next_x, next_y)) {
            actor.path.pop_front();
            startStepToTile(actor, next_x, next_y);
            return;
        }
        actor.path.clear();
        break;
    }
    actor.wait_seconds = 0.25;
}

bool NpcActorDriver::tryFollowerFreedomStep(Actor& actor, const Actor& target) {
    if (target.moving || actor.moving || actor.definition.kind != NpcActorKind::Pokemon) {
        return false;
    }
    std::vector<std::pair<int, int>> candidates = adjacentTilesByDistance(target.tile_x, target.tile_y, actor.tile_x, actor.tile_y);
    std::shuffle(candidates.begin(), candidates.end(), rng_);
    for (const auto& [tx, ty] : candidates) {
        if (tx == actor.tile_x && ty == actor.tile_y) {
            continue;
        }
        if (validWalkTile(tx, ty) &&
            !tileOccupied(tx, ty, &actor) &&
            !tileReserved(tx, ty)) {
            startStepToTile(actor, tx, ty);
            return actor.moving;
        }
    }
    return false;
}

bool NpcActorDriver::tryEvacuateReservedTile(Actor& actor) {
    if (actor.moving || !tileReserved(actor.tile_x, actor.tile_y)) {
        return false;
    }

    std::vector<std::pair<int, int>> candidates =
        adjacentTilesByDistance(actor.tile_x, actor.tile_y, actor.tile_x, actor.tile_y);
    std::shuffle(candidates.begin(), candidates.end(), rng_);
    for (const auto& [tx, ty] : candidates) {
        if (validWalkTile(tx, ty) &&
            !tileOccupied(tx, ty, &actor) &&
            !tileReserved(tx, ty)) {
            actor.path.clear();
            actor.wait_seconds = 0.0;
            startStepToTile(actor, tx, ty);
            return actor.moving;
        }
    }

    actor.wait_seconds = 0.05;
    return false;
}

void NpcActorDriver::finishMovement(Actor& actor) {
    actor.position = actor.motor.position();
    actor.tile_x = actor.target_tile_x;
    actor.tile_y = actor.target_tile_y;
    actor.terrain_binding = actor.motor.terrainBinding();
    actor.moving = false;
    if (actor.definition.behavior == NpcBehaviorKind::FollowActor) {
        actor.wait_seconds = actor.definition.follower_pokemon_freedom
            ? 0.4 + (static_cast<double>(rng_() % 120U) / 100.0)
            : 0.05;
    } else if (actor.definition.behavior == NpcBehaviorKind::Path) {
        actor.wait_seconds = 0.1;
    } else if (actor.definition.behavior == NpcBehaviorKind::DestinationRoam) {
        if (!actor.path.empty()) {
            actor.wait_seconds = 0.02;
        } else {
            const double low = std::max(0.1, actor.definition.roam_wait_min_seconds);
            const double high = std::max(low, actor.definition.roam_wait_max_seconds);
            actor.wait_seconds = std::uniform_real_distribution<double>(low, high)(rng_);
        }
    } else {
        actor.wait_seconds = 0.35 + (static_cast<double>(rng_() % 120U) / 100.0);
    }
}

std::optional<std::size_t> NpcActorDriver::findActor(const std::string& id) const {
    for (std::size_t i = 0; i < actors_.size(); ++i) {
        if (actors_[i].definition.id == id) {
            return i;
        }
    }
    return std::nullopt;
}

std::vector<NpcActorDefinition> NpcActorDriver::loadTestingActorDefinitions() {
    const fs::path config_path = fs::path(project_root_) / "config" / "character_testing" / "characters.json";
    if (!fs::exists(config_path)) {
        return {};
    }

    std::vector<NpcActorDefinition> definitions;
    try {
        const JsonValue root = parseJsonFile(config_path.string());
        pokemon_collision_mode_ =
            pokemonCollisionModeFromString(strOr(root.get("pokemonCollisionMode"), "blockCell"));
        const JsonValue* characters = root.get("characters");
        if (!characters || !characters->isArray()) {
            return {};
        }

        for (const JsonValue& item : characters->asArray()) {
            if (!item.isObject()) {
                continue;
            }

            NpcActorDefinition definition;
            definition.id = strOr(item.get("id"), "");
            definition.movement_speed_profile = strOr(item.get("movementSpeedProfile"), definition.movement_speed_profile);
            definition.behavior = behaviorFromString(strOr(item.get("behaviorProfile"), "static"));
            definition.script_id = strOr(item.get("scriptId"), "");
            definition.behavior = behaviorFromScriptId(definition.script_id, definition.behavior);
            definition.persistent_activity_id = strOr(item.get("persistentActivityId"), "");
            if (const JsonValue* activity_facing = item.get("persistentActivityFacing");
                activity_facing && activity_facing->isString()) {
                definition.persistent_activity_facing = facingFromString(activity_facing->asString());
            }
            definition.rotate_wait_min_seconds = numOr(
                item.get("rotateWaitMinSeconds"), definition.rotate_wait_min_seconds);
            definition.rotate_wait_max_seconds = numOr(
                item.get("rotateWaitMaxSeconds"), definition.rotate_wait_max_seconds);
            definition.roam_min_distance_tiles = static_cast<int>(numOr(
                item.get("roamMinDistanceTiles"), definition.roam_min_distance_tiles));
            definition.roam_wait_min_seconds = numOr(
                item.get("roamWaitMinSeconds"), definition.roam_wait_min_seconds);
            definition.roam_wait_max_seconds = numOr(
                item.get("roamWaitMaxSeconds"), definition.roam_wait_max_seconds);
            definition.facing = facingFromString(strOr(item.get("facing"), "south"));
            definition.follower_pokemon_freedom = boolOr(item.get("followerPokemonFreedom"), false);
            definition.spawn_partner_pokemon = boolOr(item.get("spawnPartnerPokemon"), true);

            const std::string character_path = strOr(item.get("characterPath"), "");
            if (!character_path.empty()) {
                const fs::path path(character_path);
                definition.character_package_path = path.is_absolute()
                    ? path.string()
                    : (fs::path(project_root_) / path).string();
            }
            const std::string kind = strOr(item.get("kind"), "");
            if (kind == "pokemon") {
                definition.kind = NpcActorKind::Pokemon;
            } else if (kind == "human" || kind == "npc") {
                definition.kind = NpcActorKind::Human;
            } else if (!definition.character_package_path.empty()) {
                try {
                    const data::CharacterPackageMetadata metadata =
                        data::loadCharacterPackageMetadata(definition.character_package_path);
                    definition.kind = metadata.character_type == "pokemon"
                        ? NpcActorKind::Pokemon
                        : NpcActorKind::Human;
                } catch (const std::exception&) {
                    definition.kind = NpcActorKind::Human;
                }
            }

            if (definition.id.empty() || definition.character_package_path.empty()) {
                continue;
            }

            if (const std::optional<std::pair<int, int>> spawn = parseTilePair(item.get("spawn"))) {
                definition.use_random_spawn = false;
                definition.spawn_tile_x = spawn->first;
                definition.spawn_tile_y = spawn->second;
            } else if (const std::optional<std::pair<int, int>> spawn = parseTilePair(item.get("spawnTile"))) {
                definition.use_random_spawn = false;
                definition.spawn_tile_x = spawn->first;
                definition.spawn_tile_y = spawn->second;
            }

            if (const JsonValue* path = item.get("path"); path && path->isArray()) {
                for (const JsonValue& point : path->asArray()) {
                    if (const std::optional<std::pair<int, int>> parsed = parseTilePair(&point)) {
                        definition.path_points.push_back(NpcPathPoint{parsed->first, parsed->second});
                    }
                }
            }

            definitions.push_back(std::move(definition));
        }
    } catch (const std::exception& ex) {
        std::cerr << "[Overworld3D][NPC] Could not load character testing config: "
                  << ex.what() << '\n';
        return {};
    }
    return definitions;
}

void NpcActorDriver::addPartnerPokemonForActor(const Actor& owner) {
    try {
        const data::CharacterPackageMetadata metadata =
            data::loadCharacterPackageMetadata(owner.definition.character_package_path);
        if (!metadata.partner_pokemon || metadata.partner_pokemon->pokemon_id.empty()) {
            return;
        }
        const std::string partner_path = pokemonCharbinPathForSpecies(metadata.partner_pokemon->pokemon_id);
        if (partner_path.empty()) {
            std::cerr << "[Overworld3D][NPC] Partner Pokemon package not found for actor '"
                      << owner.definition.id << "' species '"
                      << metadata.partner_pokemon->pokemon_id << "'\n";
            return;
        }

        NpcActorDefinition partner;
        partner.id = owner.definition.id + "_partner";
        partner.character_package_path = partner_path;
        partner.kind = NpcActorKind::Pokemon;
        partner.behavior = NpcBehaviorKind::FollowActor;
        partner.follow_target_id = owner.definition.id;
        partner.movement_speed_profile = "walk";
        partner.follower_pokemon_freedom = owner.definition.follower_pokemon_freedom;
        partner.facing = owner.facing;

        for (const auto& [tx, ty] : adjacentTilesByDistance(owner.tile_x, owner.tile_y, owner.tile_x, owner.tile_y)) {
            if (validWalkTile(tx, ty) && !tileOccupied(tx, ty) && !tileReserved(tx, ty)) {
                addActorAtTile(partner, tx, ty);
                return;
            }
        }
        addActorAtRandomValidTile(partner);
    } catch (const std::exception& ex) {
        std::cerr << "[Overworld3D][NPC] Could not load partner metadata for actor '"
                  << owner.definition.id << "': " << ex.what() << '\n';
    }
}

std::optional<std::pair<int, int>> NpcActorDriver::randomValidTile() {
    std::vector<std::pair<int, int>> valid_tiles;
    valid_tiles.reserve(static_cast<std::size_t>(std::max(0, scene_->grid.width * scene_->grid.height)));
    for (int ty = 0; ty < scene_->grid.height; ++ty) {
        for (int tx = 0; tx < scene_->grid.width; ++tx) {
            if (validWalkTile(tx, ty) && !tileOccupied(tx, ty) && !tileReserved(tx, ty)) {
                valid_tiles.push_back({tx, ty});
            }
        }
    }
    if (valid_tiles.empty()) {
        return std::nullopt;
    }
    std::uniform_int_distribution<std::size_t> pick(0, valid_tiles.size() - 1U);
    return valid_tiles[pick(rng_)];
}

bool NpcActorDriver::validWalkTile(int tx, int ty) const {
    if (!terrain_query_ || !terrain_query_->containsTile(tx, ty)) {
        return false;
    }
    if (terrain_query_->tileBlocked(tx, ty)) {
        return false;
    }
    return true;
}

bool NpcActorDriver::canStepBetweenTiles(int from_tx, int from_ty, int to_tx, int to_ty) const {
    if (!terrain_query_) return false;
    const int dx = to_tx - from_tx;
    const int dy = to_ty - from_ty;
    if (std::abs(dx) + std::abs(dy) != 1) return false;
    if (!validWalkTile(from_tx, from_ty) || !validWalkTile(to_tx, to_ty)) return false;
    if (terrain_query_->canTraverseTerrainEdge(from_tx, from_ty, to_tx, to_ty, dx, dy)) {
        return true;
    }
    return terrain_query_->tileBaseHeightUnits(from_tx, from_ty) ==
        terrain_query_->tileBaseHeightUnits(to_tx, to_ty);
}

bool NpcActorDriver::tileOccupied(int tx, int ty, const Actor* mover) const {
    return std::any_of(actors_.begin(), actors_.end(), [&](const Actor& actor) {
        if (mover && &actor == mover) {
            return false;
        }
        if (mover &&
            ((actor.definition.behavior == NpcBehaviorKind::FollowActor &&
              actor.definition.follow_target_id == mover->definition.id) ||
             (mover->definition.behavior == NpcBehaviorKind::FollowActor &&
              mover->definition.follow_target_id == actor.definition.id))) {
            return false;
        }
        return (actor.tile_x == tx && actor.tile_y == ty) ||
               (actor.moving && actor.target_tile_x == tx && actor.target_tile_y == ty);
    });
}

bool NpcActorDriver::tileReserved(int tx, int ty) const {
    return std::any_of(reserved_tiles_.begin(), reserved_tiles_.end(), [&](const auto& tile) {
        return tile.first == tx && tile.second == ty;
    });
}

bool NpcActorDriver::tileReservedByNonPlayer(int tx, int ty) const {
    const std::size_t begin = std::min(player_reserved_tile_count_, reserved_tiles_.size());
    return std::any_of(reserved_tiles_.begin() + begin, reserved_tiles_.end(), [&](const auto& tile) {
        return tile.first == tx && tile.second == ty;
    });
}

NpcActorDriver::Actor* NpcActorDriver::actorAtTile(int tx, int ty) {
    for (Actor& actor : actors_) {
        if ((actor.tile_x == tx && actor.tile_y == ty) ||
            (actor.moving && actor.target_tile_x == tx && actor.target_tile_y == ty)) {
            return &actor;
        }
    }
    return nullptr;
}

bool NpcActorDriver::followerPokemonSettledWithTarget(const Actor& actor) const {
    if (actor.definition.kind != NpcActorKind::Pokemon ||
        actor.definition.behavior != NpcBehaviorKind::FollowActor ||
        actor.definition.follow_target_id.empty()) {
        return false;
    }

    const std::optional<std::size_t> target_index = findActor(actor.definition.follow_target_id);
    if (!target_index || *target_index >= actors_.size()) {
        return false;
    }

    const Actor& target = actors_[*target_index];
    const int actor_tx = actor.moving ? actor.target_tile_x : actor.tile_x;
    const int actor_ty = actor.moving ? actor.target_tile_y : actor.tile_y;
    const int target_tx = target.moving ? target.target_tile_x : target.tile_x;
    const int target_ty = target.moving ? target.target_tile_y : target.tile_y;
    return std::abs(actor_tx - target_tx) + std::abs(actor_ty - target_ty) <= 1;
}

bool NpcActorDriver::actorCanYieldFromTile(Actor& actor, int player_from_tx, int player_from_ty) {
    if (actor.moving) {
        return false;
    }

    std::vector<std::pair<int, int>> candidates;
    candidates.push_back({player_from_tx, player_from_ty});
    for (const auto& candidate : adjacentTilesByDistance(actor.tile_x, actor.tile_y, player_from_tx, player_from_ty)) {
        if (std::find(candidates.begin(), candidates.end(), candidate) == candidates.end()) {
            candidates.push_back(candidate);
        }
    }

    for (const auto& [tx, ty] : candidates) {
        if (tx == actor.tile_x && ty == actor.tile_y) {
            continue;
        }
        const bool player_from_tile = tx == player_from_tx && ty == player_from_ty;
        if (validWalkTile(tx, ty) &&
            !tileOccupied(tx, ty, &actor) &&
            ((player_from_tile && !tileReservedByNonPlayer(tx, ty)) || !tileReserved(tx, ty))) {
            startStepToTile(actor, tx, ty, player_from_tile);
            return actor.moving;
        }
    }
    return false;
}

std::vector<ResortPokemonSpawnInfo> NpcActorDriver::resortPokemonSpawnList() const {
    std::vector<ResortPokemonSpawnInfo> out;
    if (!resort_pokemon_spawn_config_.enabled || resort_pokemon_spawn_config_.max_pokemon == 0) {
        return out;
    }
    try {
        const TitleScreenConfig title_config =
            loadConfigFromJson((fs::path(project_root_) / "config" / "title_screen.json").string());
        const fs::path save_directory = resolveSaveDirectory(title_config.persistence, project_root_);
        resort::PokemonResortService resort_service(resortProfileDatabasePath(save_directory, title_config.persistence));
        resort_service.ensureProfile(resort_pokemon_spawn_config_.profile_id);
        for (const resort::PokemonSlotView& slot :
             resort_service.getBoxSlotViews(
                 resort_pokemon_spawn_config_.profile_id,
                 resort_pokemon_spawn_config_.box_id)) {
            const std::string path = pokemonCharbinPathForResortSlot(slot.species_slug, slot.species_name);
            if (!path.empty()) {
                out.push_back(ResortPokemonSpawnInfo{
                    "resort_box" + std::to_string(resort_pokemon_spawn_config_.box_id + 1) +
                        "_slot_" + std::to_string(slot.slot_index),
                    path,
                    slot.species_slug,
                    slot.species_name,
                    slot.display_name,
                    resort_pokemon_spawn_config_.box_id,
                    slot.slot_index,
                    slot.form_key.empty() ? "default" : slot.form_key,
                    slot.shiny});
                if (out.size() >= static_cast<std::size_t>(resort_pokemon_spawn_config_.max_pokemon)) {
                    break;
                }
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "[Overworld3D][NPC] Could not read Resort box Pokemon: "
                  << ex.what() << '\n';
    }
    return out;
}

std::string NpcActorDriver::pokemonCharbinPathForSpecies(const std::string& species) const {
    const std::string canonical = canonicalSpeciesId(species);
    if (canonical.empty()) {
        return {};
    }
    const std::string direct =
        followers::resolveFollowerPokemonCharbinPath(project_root_, canonical);
    if (fs::exists(direct)) {
        return direct;
    }
    return {};
}

std::string NpcActorDriver::pokemonCharbinPathForResortSlot(
    const std::string& species_slug,
    const std::string& species_name) const {
    if (const std::string from_slug = pokemonCharbinPathForSpecies(species_slug); !from_slug.empty()) {
        return from_slug;
    }
    if (const std::string from_name = pokemonCharbinPathForSpecies(species_name); !from_name.empty()) {
        return from_name;
    }
    return {};
}

} // namespace pr::gameplay::world3d::npc
