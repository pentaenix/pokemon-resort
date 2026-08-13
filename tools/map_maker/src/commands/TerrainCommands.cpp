#include "mapmaker/commands/TerrainCommands.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace pr::mapmaker {

TerrainGrid::TerrainGrid(int width, int height, TerrainCell fill)
    : width_(width), height_(height) {
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("Terrain grid dimensions must be positive");
    }
    const std::size_t safe_width = static_cast<std::size_t>(width);
    const std::size_t safe_height = static_cast<std::size_t>(height);
    if (safe_height > std::numeric_limits<std::size_t>::max() / safe_width) {
        throw std::overflow_error("Terrain grid dimensions overflow addressable memory");
    }
    cells_.assign(safe_width * safe_height, fill);
}

bool TerrainGrid::contains(int x, int y) const {
    return x >= 0 && y >= 0 && x < width_ && y < height_;
}

std::size_t TerrainGrid::indexOf(int x, int y) const {
    if (!contains(x, y)) {
        throw std::out_of_range(
            "Terrain cell is outside " + std::to_string(width_) + "x" + std::to_string(height_));
    }
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
        static_cast<std::size_t>(x);
}

const TerrainCell& TerrainGrid::cell(int x, int y) const { return cells_[indexOf(x, y)]; }

void TerrainGrid::setCell(int x, int y, TerrainCell value) {
    cells_[indexOf(x, y)] = value;
}

TerrainPatchCommand::TerrainPatchCommand(
    TerrainGrid& grid,
    std::vector<TerrainCellPatch> patches,
    std::string label,
    std::string coalesce_key)
    : grid_(&grid), label_(std::move(label)), coalesce_key_(std::move(coalesce_key)) {
    std::unordered_map<std::size_t, std::size_t> by_cell;
    for (TerrainCellPatch& patch : patches) {
        if (!grid.contains(patch.x, patch.y)) {
            throw std::out_of_range("Terrain patch contains an out-of-bounds cell");
        }
        const std::size_t key = static_cast<std::size_t>(patch.y) *
            static_cast<std::size_t>(grid.width()) + static_cast<std::size_t>(patch.x);
        const auto [iterator, inserted] = by_cell.emplace(key, patches_.size());
        if (inserted) {
            patches_.push_back(std::move(patch));
        } else {
            patches_[iterator->second].after = patch.after;
        }
    }
}

void TerrainPatchCommand::apply() {
    for (const TerrainCellPatch& patch : patches_) {
        grid_->setCell(patch.x, patch.y, patch.after);
    }
}

void TerrainPatchCommand::revert() {
    for (auto patch = patches_.rbegin(); patch != patches_.rend(); ++patch) {
        grid_->setCell(patch->x, patch->y, patch->before);
    }
}

bool TerrainPatchCommand::isNoop() const {
    return patches_.empty() || std::all_of(
        patches_.begin(), patches_.end(), [](const TerrainCellPatch& patch) {
            return patch.before == patch.after;
        });
}

bool TerrainPatchCommand::coalesceWith(const EditorCommand& newer_command) {
    const auto* newer = dynamic_cast<const TerrainPatchCommand*>(&newer_command);
    if (!newer || grid_ != newer->grid_ || coalesce_key_.empty() ||
        coalesce_key_ != newer->coalesce_key_) {
        return false;
    }

    for (const TerrainCellPatch& incoming : newer->patches_) {
        const auto existing = std::find_if(
            patches_.begin(), patches_.end(), [&](const TerrainCellPatch& patch) {
                return patch.x == incoming.x && patch.y == incoming.y;
            });
        if (existing == patches_.end()) {
            patches_.push_back(incoming);
        } else {
            existing->after = incoming.after;
        }
    }
    return true;
}

std::unique_ptr<EditorCommand> makeSetTerrainCellCommand(
    TerrainGrid& grid,
    int x,
    int y,
    TerrainCell value,
    std::string label,
    std::string coalesce_key) {
    return std::make_unique<TerrainPatchCommand>(
        grid,
        std::vector<TerrainCellPatch>{{x, y, grid.cell(x, y), value}},
        std::move(label),
        std::move(coalesce_key));
}

} // namespace pr::mapmaker
