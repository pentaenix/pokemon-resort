#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"
#include "gameplay/world3d/characters/CharacterMovementConfig.hpp"
#include "gameplay/world3d/characters/GridActorMotor.hpp"
#include "gameplay/world3d/characters/SpriteSheetAnimator.hpp"
#include "gameplay/world3d/npc/ResortPokemonSpawnConfig.hpp"
#include "gameplay/world3d/rendering/BillboardPlacement.hpp"
#include "gameplay/world3d/terrain/ActorTerrainBinding.hpp"

#include <SDL.h>

#include <deque>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace pr::gameplay::world3d::npc {

enum class NpcActorKind {
    Human,
    Pokemon,
};

enum class NpcBehaviorKind {
    Static,
    IdleRotate,
    Wander,
    Path,
    FollowActor,
};

enum class PokemonCollisionMode {
    BlockCell,
    Swap,
};

struct NpcPathPoint {
    int x = 0;
    int y = 0;
};

struct NpcActorDefinition {
    std::string id;
    std::string character_package_path;
    NpcActorKind kind = NpcActorKind::Human;
    NpcBehaviorKind behavior = NpcBehaviorKind::Static;
    std::string follow_target_id;
    std::string movement_speed_profile = "walk";
    std::string script_id;
    bool use_random_spawn = true;
    int spawn_tile_x = 0;
    int spawn_tile_y = 0;
    FacingDirection facing = FacingDirection::South;
    bool follower_pokemon_freedom = false;
    std::vector<NpcPathPoint> path_points;
    std::string pokemon_form_id = "default";
    bool pokemon_shiny = false;
};

struct NpcInteractionActorInfo {
    NpcActorKind kind = NpcActorKind::Human;
    int tile_x = 0;
    int tile_y = 0;
    std::string display_name;
    std::string species_name;
    std::string pokemon_size = "small";
    std::vector<std::string> pokemon_types;
    std::vector<std::string> dialogue_lines;
};

struct ResortPokemonSpawnInfo {
    std::string id;
    std::string character_package_path;
    std::string species_slug;
    std::string species_name;
    int box_id = 0;
    int slot_index = 0;
    std::string form_id = "default";
    bool shiny = false;
};

class NpcActorDriver {
public:
    NpcActorDriver(std::string project_root, const SceneConfig& scene);

    void initializeDefaultSceneActors(const camera::Vec3& player_position);
    std::vector<ResortPokemonSpawnInfo> resortPokemonSpawnList() const;
    void setTerrainQuery(std::shared_ptr<characters::CharacterTerrainQuery> terrain_query);
    void update(double dt);
    void setPlayerReservedTile(int tx, int ty);
    void setReservedTiles(std::vector<std::pair<int, int>> tiles, std::size_t player_reserved_tile_count);
    bool canPlayerEnterTile(int from_tx, int from_ty, int to_tx, int to_ty);
    std::optional<std::string> interactableActorIdAtTile(int tx, int ty) const;
    std::optional<NpcInteractionActorInfo> interactionActorInfo(const std::string& actor_id) const;
    bool setInteractionLockedActor(const std::string& actor_id);
    bool faceInteractionLockedActorTowardTile(int tx, int ty);
    bool faceInteractionLockedActor(FacingDirection facing);
    bool startInteractionSessionForLockedActor();
    void requestInteractionSessionExitForLockedActor();
    bool lockedActorInteractionSessionReady() const;
    bool lockedActorInteractionSessionFinished() const;
    void clearInteractionLockedActor();
    void collectBillboardDraws(
        const camera::Gen4FollowCamera& camera,
        int viewport_w,
        int viewport_h,
        std::vector<rendering::CharacterBillboardDraw>& out) const;

    bool empty() const { return actors_.empty(); }

private:
    struct Actor {
        NpcActorDefinition definition;
        CharacterSpriteDefinition character;
        std::unique_ptr<characters::SpriteSheetAnimator> animator;
        camera::Vec3 position{};
        camera::Vec3 move_start{};
        camera::Vec3 move_target{};
        characters::GridActorMotor motor{};
        terrain::ActorTerrainBinding terrain_binding{};
        SDL_Rect source_rect{};
        int tile_x = 0;
        int tile_y = 0;
        int target_tile_x = 0;
        int target_tile_y = 0;
        int follow_last_target_tile_x = 0;
        int follow_last_target_tile_y = 0;
        int spawn_tile_x = 0;
        int spawn_tile_y = 0;
        std::size_t path_point_index = 0;
        float move_t = 1.0f;
        double wait_seconds = 0.0;
        bool moving = false;
        bool running = false;
        bool have_follow_last_target_tile = false;
        FacingDirection facing = FacingDirection::South;
        float move_speed_units_per_second = 64.0f;
        float base_move_speed_units_per_second = 64.0f;
        std::deque<std::pair<int, int>> path;
        std::string display_name;
        std::vector<std::string> dialogue_lines;
    };

    std::optional<std::size_t> addActorAtRandomValidTile(const NpcActorDefinition& definition);
    std::optional<std::size_t> addActorAtTile(const NpcActorDefinition& definition, int tx, int ty);
    std::optional<std::size_t> addActor(const NpcActorDefinition& definition, int tx, int ty);
    void addPartnerPokemonForActor(const Actor& owner);
    void startStep(Actor& actor, int dx, int dy);
    void startStepToTile(Actor& actor, int tx, int ty, bool allow_reserved_tile = false);
    void updateRandomWalk(Actor& actor, double dt);
    void updateIdleRotate(Actor& actor, double dt);
    void updatePath(Actor& actor, double dt);
    void updateFollowActor(Actor& actor, const Actor& target, double dt);
    bool tryFollowerFreedomStep(Actor& actor, const Actor& target);
    bool tryEvacuateReservedTile(Actor& actor);
    void finishMovement(Actor& actor);
    std::vector<NpcActorDefinition> loadTestingActorDefinitions();
    std::optional<std::size_t> findActor(const std::string& id) const;
    std::optional<std::pair<int, int>> randomValidTile();
    bool validWalkTile(int tx, int ty) const;
    bool canStepBetweenTiles(int from_tx, int from_ty, int to_tx, int to_ty) const;
    bool tileOccupied(int tx, int ty, const Actor* mover = nullptr) const;
    bool tileReserved(int tx, int ty) const;
    bool tileReservedByNonPlayer(int tx, int ty) const;
    Actor* actorAtTile(int tx, int ty);
    bool followerPokemonSettledWithTarget(const Actor& actor) const;
    bool actorCanYieldFromTile(Actor& actor, int player_from_tx, int player_from_ty);
    std::string pokemonCharbinPathForSpecies(const std::string& species) const;
    std::string pokemonCharbinPathForResortSlot(const std::string& species_slug, const std::string& species_name) const;

    std::string project_root_;
    const SceneConfig* scene_ = nullptr;
    std::shared_ptr<characters::CharacterTerrainQuery> terrain_query_;
    characters::CharacterMovementConfig movement_config_{};
    ResortPokemonSpawnConfig resort_pokemon_spawn_config_{};
    PokemonCollisionMode pokemon_collision_mode_ = PokemonCollisionMode::BlockCell;
    std::vector<Actor> actors_;
    std::vector<std::pair<int, int>> reserved_tiles_;
    std::size_t player_reserved_tile_count_ = 1U;
    std::optional<std::size_t> interaction_locked_actor_;
    std::mt19937 rng_{std::random_device{}()};
};

} // namespace pr::gameplay::world3d::npc
