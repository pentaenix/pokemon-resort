#include "gameplay/world3d/aquarium/construction/AquariumConstructionSession.hpp"

#include <algorithm>

namespace pr::gameplay::world3d::aquarium::construction {
namespace geo = pr::aquarium::geometry;

void AquariumConstructionSession::rebuildCellIndex() {
    allowed_cell_index_.clear();
    occupied_cell_index_.clear();
    for (auto cell : allowed_cells_) allowed_cell_index_.emplace(cell.column, cell.row);
    // Empty owner is an authored obstacle, which can never be ignored by an edit.
    for (auto cell : authored_obstacles_) occupied_cell_index_[{cell.column, cell.row}].push_back({});
    for (const auto& tank : committed_.tanks)
        for (auto cell : tankFootprintCells(tank))
            occupied_cell_index_[{cell.column, cell.row}].push_back(tank.id);
}

bool AquariumConstructionSession::cellAllowed(geo::GridCell cell) const {
    return allowed_cell_index_.find({cell.column, cell.row}) != allowed_cell_index_.end();
}

bool AquariumConstructionSession::occupiedByCommitted(
    geo::GridCell cell, const std::string& ignored_tank_id) const {
    const auto found = occupied_cell_index_.find({cell.column, cell.row});
    if (found == occupied_cell_index_.end()) return false;
    return std::any_of(found->second.begin(), found->second.end(), [&](const auto& owner) {
        return owner.empty() || ignored_tank_id.empty() || owner != ignored_tank_id;
    });
}

bool AquariumConstructionSession::cellBlocked(geo::GridCell cell) const {
    return occupiedByCommitted(cell);
}
} // namespace pr::gameplay::world3d::aquarium::construction
