#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/terrain/ActorTerrainBinding.hpp"
#include "gameplay/world3d/terrain/GridStepMotor.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::characters {

class CharacterTerrainQuery {
public:
    virtual ~CharacterTerrainQuery() = default;

    virtual float tileSize() const = 0;
    virtual bool containsTile(int world_tx, int world_ty) const = 0;
    virtual bool tileBlocked(int world_tx, int world_ty) const = 0;
    virtual int tileBaseHeightUnits(int world_tx, int world_ty) const = 0;
    virtual int tileSpecial(int world_tx, int world_ty) const = 0;
    virtual float tileWorldHeight(int world_tx, int world_ty) const = 0;
    virtual bool canTraverseTerrainEdge(
        int from_world_tx,
        int from_world_ty,
        int to_world_tx,
        int to_world_ty,
        int dx,
        int dy) const = 0;
    virtual terrain::ActorTerrainBinding bindActorStanding(
        int world_tx,
        int world_ty,
        float world_x,
        float world_z) const = 0;
    virtual terrain::GridStepMotor beginStep(
        int from_world_tx,
        int from_world_ty,
        int to_world_tx,
        int to_world_ty,
        int dx,
        int dy,
        int from_height_units,
        int to_height_units) const = 0;
    virtual float actorHeightDuringStep(
        float world_x,
        float world_z,
        const terrain::GridStepMotor& motor,
        float t) const = 0;
};

std::shared_ptr<CharacterTerrainQuery> makeLocalCharacterTerrainQuery(const SceneConfig& scene);

struct LoadedWorldChunk {
    std::string id;
    SceneConfig scene;
    int origin_tile_x = 0;
    int origin_tile_y = 0;
};

std::shared_ptr<CharacterTerrainQuery> makeLoadedWorldCharacterTerrainQuery(
    std::vector<LoadedWorldChunk> chunks);

} // namespace pr::gameplay::world3d::characters
