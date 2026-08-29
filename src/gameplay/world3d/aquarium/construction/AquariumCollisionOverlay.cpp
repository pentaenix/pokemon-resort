#include "gameplay/world3d/aquarium/construction/AquariumCollisionOverlay.hpp"

#include <algorithm>
#include <stdexcept>

namespace pr::gameplay::world3d::aquarium::construction {

AquariumCollisionOverlay::AquariumCollisionOverlay(
    std::shared_ptr<gameplay::world3d::characters::CharacterTerrainQuery> base)
    : base_(std::move(base)) {
    if (!base_) throw std::invalid_argument("Aquarium collision overlay requires a base query");
}

void AquariumCollisionOverlay::setBlockedCells(
    std::vector<pr::aquarium::geometry::GridCell> cells) {
    std::sort(cells.begin(), cells.end(), [](auto lhs, auto rhs) {
        return lhs.row == rhs.row ? lhs.column < rhs.column : lhs.row < rhs.row;
    });
    cells.erase(std::unique(cells.begin(), cells.end(), [](auto lhs, auto rhs) {
        return lhs.column == rhs.column && lhs.row == rhs.row;
    }), cells.end());
    blocked_cells_ = std::move(cells);
}

float AquariumCollisionOverlay::tileSize() const { return base_->tileSize(); }
bool AquariumCollisionOverlay::containsTile(int x, int y) const { return base_->containsTile(x, y); }
bool AquariumCollisionOverlay::tileBlocked(int x, int y) const {
    return base_->tileBlocked(x, y) || std::any_of(blocked_cells_.begin(), blocked_cells_.end(),
        [&](auto cell) { return cell.column == x && cell.row == y; });
}
bool AquariumCollisionOverlay::tileIsActualWater(int x, int y) const { return base_->tileIsActualWater(x, y); }
int AquariumCollisionOverlay::tileBaseHeightUnits(int x, int y) const { return base_->tileBaseHeightUnits(x, y); }
int AquariumCollisionOverlay::tileSpecial(int x, int y) const { return base_->tileSpecial(x, y); }
float AquariumCollisionOverlay::tileWorldHeight(int x, int y) const { return base_->tileWorldHeight(x, y); }
bool AquariumCollisionOverlay::canTraverseTerrainEdge(
    int fx, int fy, int tx, int ty, int dx, int dy) const {
    return !tileBlocked(tx, ty) && base_->canTraverseTerrainEdge(fx, fy, tx, ty, dx, dy);
}
gameplay::world3d::terrain::ActorTerrainBinding AquariumCollisionOverlay::bindActorStanding(
    int tx, int ty, float x, float z) const {
    return base_->bindActorStanding(tx, ty, x, z);
}
gameplay::world3d::terrain::GridStepMotor AquariumCollisionOverlay::beginStep(
    int fx, int fy, int tx, int ty, int dx, int dy, int fh, int th) const {
    return base_->beginStep(fx, fy, tx, ty, dx, dy, fh, th);
}
float AquariumCollisionOverlay::actorHeightDuringStep(
    float x, float z, const gameplay::world3d::terrain::GridStepMotor& motor, float t) const {
    return base_->actorHeightDuringStep(x, z, motor, t);
}

} // namespace pr::gameplay::world3d::aquarium::construction
