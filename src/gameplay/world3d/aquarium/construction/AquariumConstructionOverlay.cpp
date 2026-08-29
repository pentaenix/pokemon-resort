#include "gameplay/world3d/aquarium/construction/AquariumConstructionOverlay.hpp"

#include <algorithm>

namespace pr::gameplay::world3d::aquarium::construction {

void AquariumConstructionOverlay::configure(const AquariumConstructionConfig& config) {
    config_ = config;
    if (config_.allowed_cells.empty()) return;
    min_column_ = max_column_ = config_.allowed_cells.front().column;
    min_row_ = max_row_ = config_.allowed_cells.front().row;
    for (const auto cell : config_.allowed_cells) {
        min_column_ = std::min(min_column_, cell.column);
        max_column_ = std::max(max_column_, cell.column);
        min_row_ = std::min(min_row_, cell.row);
        max_row_ = std::max(max_row_, cell.row);
    }
}

SDL_Rect AquariumConstructionOverlay::gridRect(int width, int height) const {
    const int columns = std::max(1, max_column_ - min_column_ + 1);
    const int rows = std::max(1, max_row_ - min_row_ + 1);
    const int available_w = std::max(1, width - 160);
    const int available_h = std::max(1, height - 180);
    const int cell = std::max(8, std::min(available_w / columns, available_h / rows));
    const int grid_w = cell * columns;
    const int grid_h = cell * rows;
    return {(width - grid_w) / 2, (height - grid_h) / 2 + 28, grid_w, grid_h};
}

SDL_Rect AquariumConstructionOverlay::cellRect(
    pr::aquarium::geometry::GridCell cell, int width, int height) const {
    const SDL_Rect grid = gridRect(width, height);
    const int columns = std::max(1, max_column_ - min_column_ + 1);
    const int cell_size = grid.w / columns;
    return {
        grid.x + (cell.column - min_column_) * cell_size,
        grid.y + (cell.row - min_row_) * cell_size,
        cell_size,
        cell_size,
    };
}

std::optional<pr::aquarium::geometry::GridCell> AquariumConstructionOverlay::cellAt(
    int x, int y, int width, int height) const {
    if (config_.allowed_cells.empty()) return std::nullopt;
    const SDL_Rect grid = gridRect(width, height);
    const SDL_Point point{x, y};
    if (!SDL_PointInRect(&point, &grid)) return std::nullopt;
    const int columns = std::max(1, max_column_ - min_column_ + 1);
    const int rows = std::max(1, max_row_ - min_row_ + 1);
    const int column = min_column_ + (x - grid.x) * columns / std::max(1, grid.w);
    const int row = min_row_ + (y - grid.y) * rows / std::max(1, grid.h);
    const pr::aquarium::geometry::GridCell cell{column, row};
    const bool allowed = std::any_of(config_.allowed_cells.begin(), config_.allowed_cells.end(),
        [&](auto item) { return item.column == column && item.row == row; });
    return allowed ? std::optional{cell} : std::nullopt;
}

void AquariumConstructionOverlay::render(
    SDL_Renderer* renderer, int width, int height,
    const AquariumConstructionSession& session) const {
    if (!renderer || !session.active()) return;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 5, 18, 30, 178);
    const SDL_Rect shade{0, 0, width, height};
    SDL_RenderFillRect(renderer, &shade);

    const SDL_Rect grid = gridRect(width, height);
    SDL_SetRenderDrawColor(renderer, 10, 32, 48, 230);
    SDL_Rect frame{grid.x - 10, grid.y - 10, grid.w + 20, grid.h + 20};
    SDL_RenderFillRect(renderer, &frame);

    for (const auto cell : config_.allowed_cells) {
        SDL_Rect rect = cellRect({cell.column, cell.row}, width, height);
        SDL_SetRenderDrawColor(renderer, 35, 64, 77, 220);
        SDL_RenderFillRect(renderer, &rect);
        SDL_SetRenderDrawColor(renderer, 82, 121, 132, 210);
        SDL_RenderDrawRect(renderer, &rect);
    }
    for (const auto cell : session.draftCells()) {
        SDL_Rect rect = cellRect(cell, width, height);
        if (session.draftValid()) SDL_SetRenderDrawColor(renderer, 40, 178, 135, 205);
        else SDL_SetRenderDrawColor(renderer, 212, 76, 72, 205);
        SDL_RenderFillRect(renderer, &rect);
        if (!session.draftValid()) {
            SDL_RenderDrawLine(renderer, rect.x, rect.y, rect.x + rect.w, rect.y + rect.h);
            SDL_RenderDrawLine(renderer, rect.x + rect.w, rect.y, rect.x, rect.y + rect.h);
        }
    }
    SDL_Rect cursor = cellRect(session.cursor(), width, height);
    cursor.x += 2; cursor.y += 2; cursor.w -= 4; cursor.h -= 4;
    SDL_SetRenderDrawColor(renderer, 250, 246, 184, 255);
    SDL_RenderDrawRect(renderer, &cursor);
    cursor.x += 2; cursor.y += 2; cursor.w -= 4; cursor.h -= 4;
    SDL_RenderDrawRect(renderer, &cursor);

    // Shape, fixed-height, confirm, and cancel glyphs communicate the v1 tools
    // without exposing authored numeric values.
    const int palette_y = std::max(20, grid.y - 76);
    SDL_Rect shape{grid.x, palette_y, 52, 42};
    SDL_SetRenderDrawColor(renderer, 22, 48, 65, 245);
    SDL_RenderFillRect(renderer, &shape);
    SDL_SetRenderDrawColor(renderer, 137, 220, 228, 255);
    SDL_Rect rectangle_icon{shape.x + 10, shape.y + 9, 32, 24};
    SDL_RenderDrawRect(renderer, &rectangle_icon);
    for (int tick = 0; tick < 4; ++tick) {
        SDL_Rect pip{shape.x + 70 + tick * 18, shape.y + 25 - tick * 4, 10, 8 + tick * 4};
        SDL_SetRenderDrawColor(renderer, 91, 176, 210, 255);
        SDL_RenderFillRect(renderer, &pip);
    }
    const SDL_Rect confirm{grid.x + grid.w - 94, palette_y, 42, 42};
    const SDL_Rect cancel{grid.x + grid.w - 42, palette_y, 42, 42};
    SDL_SetRenderDrawColor(renderer, 35, 110, 79, 245);
    SDL_RenderFillRect(renderer, &confirm);
    SDL_SetRenderDrawColor(renderer, 235, 255, 220, 255);
    SDL_RenderDrawLine(renderer, confirm.x + 10, confirm.y + 23, confirm.x + 18, confirm.y + 31);
    SDL_RenderDrawLine(renderer, confirm.x + 18, confirm.y + 31, confirm.x + 33, confirm.y + 11);
    SDL_SetRenderDrawColor(renderer, 116, 45, 48, 245);
    SDL_RenderFillRect(renderer, &cancel);
    SDL_SetRenderDrawColor(renderer, 255, 220, 220, 255);
    SDL_RenderDrawLine(renderer, cancel.x + 10, cancel.y + 10, cancel.x + 32, cancel.y + 32);
    SDL_RenderDrawLine(renderer, cancel.x + 32, cancel.y + 10, cancel.x + 10, cancel.y + 32);
}

} // namespace pr::gameplay::world3d::aquarium::construction
