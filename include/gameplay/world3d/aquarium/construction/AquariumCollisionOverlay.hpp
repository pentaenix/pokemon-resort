#pragma once

#include "aquarium_geometry/Types.hpp"
#include "gameplay/world3d/characters/CharacterTerrainQuery.hpp"

#include <memory>
#include <vector>

namespace pr::gameplay::world3d::aquarium::construction {

class AquariumCollisionOverlay final
    : public gameplay::world3d::characters::CharacterTerrainQuery {
public:
    explicit AquariumCollisionOverlay(
        std::shared_ptr<gameplay::world3d::characters::CharacterTerrainQuery> base);

    void setBlockedCells(std::vector<pr::aquarium::geometry::GridCell> cells);
    const std::vector<pr::aquarium::geometry::GridCell>& blockedCells() const { return blocked_cells_; }

    float tileSize() const override;
    bool containsTile(int world_tx, int world_ty) const override;
    bool tileBlocked(int world_tx, int world_ty) const override;
    bool tileIsActualWater(int world_tx, int world_ty) const override;
    int tileBaseHeightUnits(int world_tx, int world_ty) const override;
    int tileSpecial(int world_tx, int world_ty) const override;
    float tileWorldHeight(int world_tx, int world_ty) const override;
    bool canTraverseTerrainEdge(int, int, int, int, int, int) const override;
    gameplay::world3d::terrain::ActorTerrainBinding bindActorStanding(
        int, int, float, float) const override;
    gameplay::world3d::terrain::GridStepMotor beginStep(
        int, int, int, int, int, int, int, int) const override;
    float actorHeightDuringStep(
        float, float, const gameplay::world3d::terrain::GridStepMotor&, float) const override;

private:
    std::shared_ptr<gameplay::world3d::characters::CharacterTerrainQuery> base_;
    std::vector<pr::aquarium::geometry::GridCell> blocked_cells_;
};

} // namespace pr::gameplay::world3d::aquarium::construction
