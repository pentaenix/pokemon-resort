#include "gameplay/world3d/characters/CharacterTerrainQuery.hpp"

#include "gameplay/world3d/interiors/DefaultRoom.hpp"
#include "gameplay/world3d/terrain/TerrainSurface.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace pr::gameplay::world3d::characters {

namespace {

struct ResolvedTile {
    const LoadedWorldChunk* chunk = nullptr;
    int local_x = 0;
    int local_y = 0;
};

bool containsSceneTile(const SceneConfig& scene, int tile_x, int tile_y) {
    const bool inside = tile_x >= 0 && tile_y >= 0 &&
        tile_x < std::max(1, scene.grid.width) &&
        tile_y < std::max(1, scene.grid.height);
    return inside || interiors::entryExtensionCoversTile(scene, tile_x, tile_y);
}

class LocalCharacterTerrainQuery final : public CharacterTerrainQuery {
public:
    explicit LocalCharacterTerrainQuery(const SceneConfig& scene) : scene_(scene) {}

    float tileSize() const override { return std::max(1.0f, scene_.grid.tile_size); }

    bool containsTile(int world_tx, int world_ty) const override {
        return containsSceneTile(scene_, world_tx, world_ty);
    }

    bool tileBlocked(int world_tx, int world_ty) const override {
        if (interiors::entryExtensionCoversTile(scene_, world_tx, world_ty)) return false;
        if (interiors::boundaryCellBlocked(scene_, world_tx, world_ty)) return true;
        if (scene_.terrain.collision.empty()) return false;
        if (world_ty < 0 || world_ty >= static_cast<int>(scene_.terrain.collision.size())) return false;
        const auto& row = scene_.terrain.collision[static_cast<std::size_t>(world_ty)];
        if (world_tx < 0 || world_tx >= static_cast<int>(row.size())) return false;
        return row[static_cast<std::size_t>(world_tx)] != 0;
    }

    bool tileIsActualWater(int world_tx, int world_ty) const override {
        const auto [sample_x, sample_y] =
            interiors::defaultRoomTerrainSampleTile(scene_, world_tx, world_ty);
        return terrain::isActualWaterTile(scene_, sample_x, sample_y);
    }

    int tileBaseHeightUnits(int world_tx, int world_ty) const override {
        const auto [sample_x, sample_y] =
            interiors::defaultRoomTerrainSampleTile(scene_, world_tx, world_ty);
        if (scene_.terrain.heights.empty()) {
            return static_cast<int>(std::round(scene_.player.spawn_height / std::max(0.001f, tileSize())));
        }
        if (sample_y < 0 || sample_y >= static_cast<int>(scene_.terrain.heights.size())) return 0;
        const auto& row = scene_.terrain.heights[static_cast<std::size_t>(sample_y)];
        if (sample_x < 0 || sample_x >= static_cast<int>(row.size())) return 0;
        return static_cast<int>(row[static_cast<std::size_t>(sample_x)]);
    }

    int tileSpecial(int world_tx, int world_ty) const override {
        const auto [sample_x, sample_y] =
            interiors::defaultRoomTerrainSampleTile(scene_, world_tx, world_ty);
        if (scene_.terrain.specials.empty()) return 0;
        if (sample_y < 0 || sample_y >= static_cast<int>(scene_.terrain.specials.size())) return 0;
        const auto& row = scene_.terrain.specials[static_cast<std::size_t>(sample_y)];
        if (sample_x < 0 || sample_x >= static_cast<int>(row.size())) return 0;
        return static_cast<int>(row[static_cast<std::size_t>(sample_x)]);
    }

    float tileWorldHeight(int world_tx, int world_ty) const override {
        const auto [sample_x, sample_y] =
            interiors::defaultRoomTerrainSampleTile(scene_, world_tx, world_ty);
        return terrain::heightAtActorFeet(
            scene_,
            (static_cast<float>(world_tx) + 0.5f) * tileSize(),
            (static_cast<float>(world_ty) + 0.5f) * tileSize(),
            sample_x,
            sample_y);
    }

    bool canTraverseTerrainEdge(
        int from_world_tx,
        int from_world_ty,
        int to_world_tx,
        int to_world_ty,
        int dx,
        int dy) const override {
        const bool uses_extension =
            interiors::entryExtensionCoversTile(scene_, from_world_tx, from_world_ty) ||
            interiors::entryExtensionCoversTile(scene_, to_world_tx, to_world_ty);
        if (uses_extension) {
            return containsTile(from_world_tx, from_world_ty) &&
                containsTile(to_world_tx, to_world_ty) &&
                tileBaseHeightUnits(from_world_tx, from_world_ty) ==
                    tileBaseHeightUnits(to_world_tx, to_world_ty);
        }
        return terrain::canTraverseTerrainEdge(
            scene_, from_world_tx, from_world_ty, to_world_tx, to_world_ty, dx, dy);
    }

    terrain::ActorTerrainBinding bindActorStanding(
        int world_tx,
        int world_ty,
        float world_x,
        float world_z) const override {
        const auto [sample_x, sample_y] =
            interiors::defaultRoomTerrainSampleTile(scene_, world_tx, world_ty);
        terrain::ActorTerrainBinding binding =
            terrain::bindActorStanding(scene_, sample_x, sample_y, world_x, world_z);
        binding.logical_tx = world_tx;
        binding.logical_ty = world_ty;
        return binding;
    }

    terrain::GridStepMotor beginStep(
        int from_world_tx,
        int from_world_ty,
        int to_world_tx,
        int to_world_ty,
        int dx,
        int dy,
        int from_height_units,
        int to_height_units) const override {
        const auto [sample_from_x, sample_from_y] =
            interiors::defaultRoomTerrainSampleTile(scene_, from_world_tx, from_world_ty);
        const auto [sample_to_x, sample_to_y] =
            interiors::defaultRoomTerrainSampleTile(scene_, to_world_tx, to_world_ty);
        return terrain::GridStepMotor::beginStep(
            scene_,
            sample_from_x,
            sample_from_y,
            sample_to_x,
            sample_to_y,
            dx,
            dy,
            from_height_units,
            to_height_units);
    }

    float actorHeightDuringStep(
        float world_x,
        float world_z,
        const terrain::GridStepMotor& motor,
        float t) const override {
        return terrain::actorHeightDuringStep(scene_, world_x, world_z, motor, t);
    }

private:
    SceneConfig scene_;
};

class LoadedWorldCharacterTerrainQuery final : public CharacterTerrainQuery {
public:
    explicit LoadedWorldCharacterTerrainQuery(std::vector<LoadedWorldChunk> chunks)
        : chunks_(std::move(chunks)) {
        for (LoadedWorldChunk& chunk : chunks_) {
            chunk.scene.grid.tile_size = std::max(1.0f, chunk.scene.grid.tile_size);
        }
    }

    float tileSize() const override {
        return chunks_.empty() ? 16.0f : std::max(1.0f, chunks_.front().scene.grid.tile_size);
    }

    bool containsTile(int world_tx, int world_ty) const override {
        return resolve(world_tx, world_ty).has_value();
    }

    bool tileBlocked(int world_tx, int world_ty) const override {
        const auto resolved = resolve(world_tx, world_ty);
        if (!resolved) return true;
        const SceneConfig& scene = resolved->chunk->scene;
        if (interiors::entryExtensionCoversTile(
                scene, resolved->local_x, resolved->local_y)) {
            return false;
        }
        if (interiors::boundaryCellBlocked(
                scene, resolved->local_x, resolved->local_y)) {
            return true;
        }
        if (scene.terrain.collision.empty()) return false;
        if (resolved->local_y < 0 || resolved->local_y >= static_cast<int>(scene.terrain.collision.size())) return false;
        const auto& row = scene.terrain.collision[static_cast<std::size_t>(resolved->local_y)];
        if (resolved->local_x < 0 || resolved->local_x >= static_cast<int>(row.size())) return false;
        return row[static_cast<std::size_t>(resolved->local_x)] != 0;
    }

    bool tileIsActualWater(int world_tx, int world_ty) const override {
        const auto resolved = resolve(world_tx, world_ty);
        if (!resolved) return false;
        const auto [sample_x, sample_y] = interiors::defaultRoomTerrainSampleTile(
            resolved->chunk->scene, resolved->local_x, resolved->local_y);
        return terrain::isActualWaterTile(resolved->chunk->scene, sample_x, sample_y);
    }

    int tileBaseHeightUnits(int world_tx, int world_ty) const override {
        const auto resolved = resolve(world_tx, world_ty);
        if (!resolved) return 0;
        const SceneConfig& scene = resolved->chunk->scene;
        const auto [sample_x, sample_y] = interiors::defaultRoomTerrainSampleTile(
            scene, resolved->local_x, resolved->local_y);
        if (scene.terrain.heights.empty()) {
            return static_cast<int>(std::round(scene.player.spawn_height / std::max(0.001f, tileSize())));
        }
        if (sample_y < 0 || sample_y >= static_cast<int>(scene.terrain.heights.size())) return 0;
        const auto& row = scene.terrain.heights[static_cast<std::size_t>(sample_y)];
        if (sample_x < 0 || sample_x >= static_cast<int>(row.size())) return 0;
        return static_cast<int>(row[static_cast<std::size_t>(sample_x)]);
    }

    int tileSpecial(int world_tx, int world_ty) const override {
        const auto resolved = resolve(world_tx, world_ty);
        if (!resolved) return 0;
        const SceneConfig& scene = resolved->chunk->scene;
        const auto [sample_x, sample_y] = interiors::defaultRoomTerrainSampleTile(
            scene, resolved->local_x, resolved->local_y);
        if (scene.terrain.specials.empty()) return 0;
        if (sample_y < 0 || sample_y >= static_cast<int>(scene.terrain.specials.size())) return 0;
        const auto& row = scene.terrain.specials[static_cast<std::size_t>(sample_y)];
        if (sample_x < 0 || sample_x >= static_cast<int>(row.size())) return 0;
        return static_cast<int>(row[static_cast<std::size_t>(sample_x)]);
    }

    float tileWorldHeight(int world_tx, int world_ty) const override {
        const auto resolved = resolve(world_tx, world_ty);
        if (!resolved) return 0.0f;
        const SceneConfig& scene = resolved->chunk->scene;
        const auto [sample_x, sample_y] = interiors::defaultRoomTerrainSampleTile(
            scene, resolved->local_x, resolved->local_y);
        return terrain::heightAtActorFeet(
            scene,
            (static_cast<float>(resolved->local_x) + 0.5f) * tileSize(),
            (static_cast<float>(resolved->local_y) + 0.5f) * tileSize(),
            sample_x,
            sample_y);
    }

    bool canTraverseTerrainEdge(
        int from_world_tx,
        int from_world_ty,
        int to_world_tx,
        int to_world_ty,
        int dx,
        int dy) const override {
        const auto from = resolve(from_world_tx, from_world_ty);
        const auto to = resolve(to_world_tx, to_world_ty);
        if (from && to && from->chunk == to->chunk) {
            const SceneConfig& scene = from->chunk->scene;
            const bool uses_extension =
                interiors::entryExtensionCoversTile(scene, from->local_x, from->local_y) ||
                interiors::entryExtensionCoversTile(scene, to->local_x, to->local_y);
            if (uses_extension) {
                return tileBaseHeightUnits(from_world_tx, from_world_ty) ==
                    tileBaseHeightUnits(to_world_tx, to_world_ty);
            }
            return terrain::canTraverseTerrainEdge(
                scene,
                from->local_x,
                from->local_y,
                to->local_x,
                to->local_y,
                dx,
                dy);
        }
        return false;
    }

    terrain::ActorTerrainBinding bindActorStanding(
        int world_tx,
        int world_ty,
        float world_x,
        float world_z) const override {
        const auto resolved = resolve(world_tx, world_ty);
        if (!resolved) {
            terrain::ActorTerrainBinding binding{};
            binding.logical_tx = world_tx;
            binding.logical_ty = world_ty;
            binding.height_sample_tx = world_tx;
            binding.height_sample_ty = world_ty;
            binding.simulation_y = 0.0f;
            return binding;
        }

        const float local_x =
            world_x - (static_cast<float>(resolved->chunk->origin_tile_x) * tileSize());
        const float local_z =
            world_z - (static_cast<float>(resolved->chunk->origin_tile_y) * tileSize());
        const auto [sample_x, sample_y] = interiors::defaultRoomTerrainSampleTile(
            resolved->chunk->scene, resolved->local_x, resolved->local_y);
        terrain::ActorTerrainBinding binding = terrain::bindActorStanding(
            resolved->chunk->scene,
            sample_x,
            sample_y,
            local_x,
            local_z);
        binding.logical_tx = world_tx;
        binding.logical_ty = world_ty;
        binding.height_sample_tx += resolved->chunk->origin_tile_x;
        binding.height_sample_ty += resolved->chunk->origin_tile_y;
        return binding;
    }

    terrain::GridStepMotor beginStep(
        int from_world_tx,
        int from_world_ty,
        int to_world_tx,
        int to_world_ty,
        int dx,
        int dy,
        int from_height_units,
        int to_height_units) const override {
        const auto from = resolve(from_world_tx, from_world_ty);
        const auto to = resolve(to_world_tx, to_world_ty);
        if (from && to && from->chunk == to->chunk) {
            const auto [sample_from_x, sample_from_y] = interiors::defaultRoomTerrainSampleTile(
                from->chunk->scene, from->local_x, from->local_y);
            const auto [sample_to_x, sample_to_y] = interiors::defaultRoomTerrainSampleTile(
                from->chunk->scene, to->local_x, to->local_y);
            terrain::GridStepMotor motor = terrain::GridStepMotor::beginStep(
                from->chunk->scene,
                sample_from_x,
                sample_from_y,
                sample_to_x,
                sample_to_y,
                dx,
                dy,
                from_height_units,
                to_height_units);
            motor.sample_x += from->chunk->origin_tile_x;
            motor.sample_y += from->chunk->origin_tile_y;
            motor.sample_end_x += from->chunk->origin_tile_x;
            motor.sample_end_y += from->chunk->origin_tile_y;
            return motor;
        }

        terrain::GridStepMotor motor{};
        motor.sample_x = to_world_tx;
        motor.sample_y = to_world_ty;
        motor.sample_end_x = to_world_tx;
        motor.sample_end_y = to_world_ty;
        motor.interpolate_y = from_height_units != to_height_units;
        motor.center_lerp_y = motor.interpolate_y;
        motor.lerp_start_y = static_cast<float>(from_height_units) * verticalUnitsPerFloor(from);
        motor.lerp_end_y = static_cast<float>(to_height_units) * verticalUnitsPerFloor(to);
        return motor;
    }

    float actorHeightDuringStep(
        float world_x,
        float world_z,
        const terrain::GridStepMotor& motor,
        float t) const override {
        const terrain::TileCoord sample = [&]() {
            if (motor.surface_follow) {
                return terrain::TileCoord{
                    static_cast<int>(std::floor(world_x / tileSize())),
                    static_cast<int>(std::floor(world_z / tileSize()))};
            }
            return motor.activeSampleTile(t);
        }();
        const auto resolved = resolve(sample.x, sample.y);
        if (!resolved) {
            return motor.lerp_start_y + ((motor.lerp_end_y - motor.lerp_start_y) * std::clamp(t, 0.0f, 1.0f));
        }
        const float local_x =
            world_x - (static_cast<float>(resolved->chunk->origin_tile_x) * tileSize());
        const float local_z =
            world_z - (static_cast<float>(resolved->chunk->origin_tile_y) * tileSize());
        terrain::GridStepMotor local_motor = motor;
        local_motor.sample_x -= resolved->chunk->origin_tile_x;
        local_motor.sample_y -= resolved->chunk->origin_tile_y;
        local_motor.sample_end_x -= resolved->chunk->origin_tile_x;
        local_motor.sample_end_y -= resolved->chunk->origin_tile_y;
        if (local_motor.surface_follow) {
            const int local_tx = static_cast<int>(std::floor(local_x / tileSize()));
            const int local_ty = static_cast<int>(std::floor(local_z / tileSize()));
            local_motor.sample_x = local_tx;
            local_motor.sample_y = local_ty;
            local_motor.sample_end_x = local_tx;
            local_motor.sample_end_y = local_ty;
            local_motor.handoff = false;
        }
        return terrain::actorHeightDuringStep(resolved->chunk->scene, local_x, local_z, local_motor, t);
    }

private:
    std::vector<LoadedWorldChunk> chunks_;

    std::optional<ResolvedTile> resolve(int world_tx, int world_ty) const {
        // Authored cells take precedence when an extension overlaps a chunk in
        // the same world space.
        for (const LoadedWorldChunk& chunk : chunks_) {
            const int local_x = world_tx - chunk.origin_tile_x;
            const int local_y = world_ty - chunk.origin_tile_y;
            if (local_x >= 0 && local_y >= 0 &&
                local_x < std::max(1, chunk.scene.grid.width) &&
                local_y < std::max(1, chunk.scene.grid.height)) {
                return ResolvedTile{&chunk, local_x, local_y};
            }
        }
        for (const LoadedWorldChunk& chunk : chunks_) {
            const int local_x = world_tx - chunk.origin_tile_x;
            const int local_y = world_ty - chunk.origin_tile_y;
            if (interiors::entryExtensionCoversTile(chunk.scene, local_x, local_y)) {
                return ResolvedTile{&chunk, local_x, local_y};
            }
        }
        return std::nullopt;
    }

    float verticalUnitsPerFloor(const std::optional<ResolvedTile>& resolved) const {
        if (!resolved) return tileSize();
        const SceneConfig& scene = resolved->chunk->scene;
        return scene.terrain.height_per_floor > 0.0f
            ? scene.terrain.height_per_floor
            : std::max(1.0f, scene.grid.tile_size);
    }
};

} // namespace

std::vector<LoadedWorldChunk> selectActiveWorldChunks(
    const std::vector<LoadedWorldChunk>& catalog,
    const std::string& active_map_id) {
    const auto active = std::find_if(catalog.begin(), catalog.end(), [&](const LoadedWorldChunk& chunk) {
        return chunk.id == active_map_id || chunk.scene.id == active_map_id;
    });
    if (active == catalog.end()) return {};

    std::vector<LoadedWorldChunk> selected;
    for (const LoadedWorldChunk& chunk : catalog) {
        const bool is_active = &chunk == &*active;
        const bool shares_space = chunk.scene.environment.space == active->scene.environment.space;
        if (!is_active && (!active->scene.environment.render_other_spaces || !shares_space)) continue;
        LoadedWorldChunk rebased = chunk;
        rebased.origin_tile_x -= active->origin_tile_x;
        rebased.origin_tile_y -= active->origin_tile_y;
        selected.push_back(std::move(rebased));
    }
    return selected;
}

std::shared_ptr<CharacterTerrainQuery> makeLocalCharacterTerrainQuery(const SceneConfig& scene) {
    return std::make_shared<LocalCharacterTerrainQuery>(scene);
}

std::shared_ptr<CharacterTerrainQuery> makeLoadedWorldCharacterTerrainQuery(
    std::vector<LoadedWorldChunk> chunks) {
    return std::make_shared<LoadedWorldCharacterTerrainQuery>(std::move(chunks));
}

} // namespace pr::gameplay::world3d::characters
