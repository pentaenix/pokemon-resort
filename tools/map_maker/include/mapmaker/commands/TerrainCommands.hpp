#pragma once

#include "mapmaker/commands/CommandStack.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pr::mapmaker {

struct TerrainCell {
    std::uint8_t height = 0;
    std::uint8_t special = 0;
    bool collision = false;

    friend bool operator==(const TerrainCell&, const TerrainCell&) = default;
};

class TerrainGrid {
public:
    TerrainGrid(int width, int height, TerrainCell fill = {});

    int width() const { return width_; }
    int height() const { return height_; }
    bool contains(int x, int y) const;
    const TerrainCell& cell(int x, int y) const;
    void setCell(int x, int y, TerrainCell value);

private:
    std::size_t indexOf(int x, int y) const;

    int width_ = 0;
    int height_ = 0;
    std::vector<TerrainCell> cells_;
};

struct TerrainCellPatch {
    int x = 0;
    int y = 0;
    TerrainCell before;
    TerrainCell after;
};

class TerrainPatchCommand final : public EditorCommand {
public:
    TerrainPatchCommand(
        TerrainGrid& grid,
        std::vector<TerrainCellPatch> patches,
        std::string label = "Paint terrain",
        std::string coalesce_key = {});

    const std::string& label() const override { return label_; }
    void apply() override;
    void revert() override;
    bool isNoop() const override;
    bool coalesceWith(const EditorCommand& newer) override;

    const std::vector<TerrainCellPatch>& patches() const { return patches_; }

private:
    TerrainGrid* grid_ = nullptr;
    std::vector<TerrainCellPatch> patches_;
    std::string label_;
    std::string coalesce_key_;
};

std::unique_ptr<EditorCommand> makeSetTerrainCellCommand(
    TerrainGrid& grid,
    int x,
    int y,
    TerrainCell value,
    std::string label = "Set terrain cell",
    std::string coalesce_key = {});

} // namespace pr::mapmaker
