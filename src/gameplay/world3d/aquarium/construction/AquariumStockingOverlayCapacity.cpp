#include "gameplay/world3d/aquarium/construction/AquariumStockingOverlay.hpp"

#include <SDL_image.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace pr::gameplay::world3d::aquarium::construction {
namespace {

void fill(SDL_Renderer* renderer, const SDL_Rect& rect, Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &rect);
}

void fillRound(SDL_Renderer* renderer, SDL_Rect rect, int radius, Color color) {
    radius = std::clamp(radius, 0, std::min(rect.w, rect.h) / 2);
    fill(renderer, SDL_Rect{rect.x + radius, rect.y, rect.w - radius * 2, rect.h}, color);
    for (int y = 0; y < rect.h; ++y) {
        const int dy = y < radius ? radius - y
            : y >= rect.h - radius ? y - (rect.h - radius - 1) : 0;
        const int inset = dy > 0
            ? radius - static_cast<int>(std::sqrt(std::max(0, radius * radius - dy * dy))) : 0;
        const SDL_Rect row{rect.x + inset, rect.y + y, rect.w - inset * 2, 1};
        if (row.w > 0) fill(renderer, row, color);
    }
}

SDL_Rect capacityArea(int width, int height) {
    (void)height;
    return {std::max(0, width - 40 - BoxViewport::kViewportWidth), 100,
        BoxViewport::kViewportWidth, BoxViewport::kViewportHeight};
}

Color residentColor(const std::string& species_id) {
    std::uint32_t hash = 2166136261U;
    for (const unsigned char byte : species_id) hash = (hash ^ byte) * 16777619U;
    return Color{
        static_cast<Uint8>(70U + hash % 90U),
        static_cast<Uint8>(125U + (hash >> 8U) % 90U),
        static_cast<Uint8>(145U + (hash >> 16U) % 85U), 255};
}

} // namespace

SDL_Rect AquariumStockingOverlay::spriteSourceRect(
    const AquariumSpeciesEntry& species,
    const TextureHandle& texture) const {
    const std::string cache_key = species.id + ":" + species.form;
    if (const auto found = sprite_source_rects_.find(cache_key);
        found != sprite_source_rects_.end()) return found->second;
    SDL_Rect bounds{0, 0, std::max(1, texture.width), std::max(1, texture.height)};
    if (sprite_assets_) {
        PokemonSpriteRequest request;
        request.species_id = species.dex;
        request.species_slug = species.species;
        request.form_key = species.form;
        const auto resolved = sprite_assets_->resolvePokemon(request);
        const auto path = std::filesystem::path(project_root_) /
            "assets/pokesprite" / resolved.relative_path;
        SDL_Surface* loaded = IMG_Load(path.string().c_str());
        SDL_Surface* rgba = loaded
            ? SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0) : nullptr;
        if (loaded) SDL_FreeSurface(loaded);
        if (rgba) {
            int min_x = rgba->w;
            int min_y = rgba->h;
            int max_x = -1;
            int max_y = -1;
            const auto* pixels = static_cast<const std::uint8_t*>(rgba->pixels);
            for (int y = 0; y < rgba->h; ++y) {
                const auto* row = pixels + static_cast<std::size_t>(y) * rgba->pitch;
                for (int x = 0; x < rgba->w; ++x) {
                    if (row[static_cast<std::size_t>(x) * 4U + 3U] < 8U) continue;
                    min_x = std::min(min_x, x);
                    min_y = std::min(min_y, y);
                    max_x = std::max(max_x, x);
                    max_y = std::max(max_y, y);
                }
            }
            if (max_x >= min_x && max_y >= min_y) {
                constexpr int padding = 2;
                min_x = std::max(0, min_x - padding);
                min_y = std::max(0, min_y - padding);
                max_x = std::min(rgba->w - 1, max_x + padding);
                max_y = std::min(rgba->h - 1, max_y + padding);
                bounds = {min_x, min_y, max_x - min_x + 1, max_y - min_y + 1};
            }
            SDL_FreeSurface(rgba);
        }
    }
    sprite_source_rects_[cache_key] = bounds;
    return bounds;
}

void AquariumStockingOverlay::renderCapacity(
    SDL_Renderer* renderer,
    int width,
    int height,
    const AquariumSpeciesCatalog& catalog,
    const AquariumStockingController& controller) const {
    const SDL_Rect area = capacityArea(width, height);
    fillRound(renderer, area, 16, {34, 112, 162, 255});
    fillRound(renderer, {area.x + 4, area.y + 4, area.w - 8, area.h - 8},
        12, {117, 198, 226, 255});
    const SDL_Rect name_plate{area.x + 100, area.y + 18, 360, 70};
    fillRound(renderer, name_plate, 35, {236, 251, 255, 255});
    if (tank_label_.texture) {
        const SDL_Rect label{name_plate.x + (name_plate.w - tank_label_.width) / 2,
            name_plate.y + (name_plate.h - tank_label_.height) / 2,
            tank_label_.width, tank_label_.height};
        SDL_RenderCopy(renderer, tank_label_.texture.get(), nullptr, &label);
    }
    if (controller.tab() == AquariumStockingController::Tab::Exhibit) return;

    // Treat the complete destination panel below its title as a scrollable
    // viewport. Large cells make each resident footprint readable; deep
    // stocking boards pan vertically instead of shrinking into a thumbnail.
    const SDL_Rect grid_area{area.x + 12, area.y + 98,
        area.w - 24, area.h - 110};
    const int columns = std::max(1, controller.capacityColumns());
    const int total_rows = std::max(1, controller.capacityRows());
    const int visible_rows = std::min(total_rows, AquariumStockingController::kCapacityVisibleRows);
    const int first_row = controller.capacityFirstVisibleRow();
    const int cell = std::clamp(std::min(
        grid_area.w / columns - 3,
        grid_area.h / visible_rows - 3), 18, 62);
    const int grid_x = grid_area.x + std::max(0,
        (grid_area.w - columns * (cell + 3) + 3) / 2);
    const int grid_y = grid_area.y + std::max(0,
        (grid_area.h - visible_rows * (cell + 3) + 3) / 2);
    const int first_cell = first_row * columns;
    const int final_cell = std::min(
        controller.capacityCells(), first_cell + columns * visible_rows);
    const auto placements = controller.capacityPlacements();
    std::vector<int> owners(static_cast<std::size_t>(controller.capacityCells()), -1);
    for (std::size_t owner = 0; owner < placements.size(); ++owner) {
        for (const int index : placements[owner].cell_indices) {
            if (index >= 0 && index < controller.capacityCells()) {
                owners[static_cast<std::size_t>(index)] = static_cast<int>(owner);
            }
        }
    }
    for (int index = first_cell; index < final_cell; ++index) {
        const SDL_Rect rect{grid_x + (index % columns) * (cell + 3),
            grid_y + (index / columns - first_row) * (cell + 3), cell, cell};
        const int owner = owners[static_cast<std::size_t>(index)];
        fillRound(renderer, rect, 5,
            owner >= 0 ? residentColor(placements[static_cast<std::size_t>(owner)].species_id)
                       : Color{189, 226, 237, 255});
        fillRound(renderer, {rect.x + 3, rect.y + 3, rect.w - 6, rect.h - 6}, 3,
            owner >= 0 ? Color{226, 244, 241, 210} : Color{229, 248, 253, 255});
    }

    for (const auto& placement : placements) {
        if (placement.cell_indices.empty() || !sprite_assets_) continue;
        int min_column = columns;
        int max_column = 0;
        int min_row = total_rows;
        int max_row = 0;
        bool visible = false;
        for (const int index : placement.cell_indices) {
            if (index < first_cell || index >= final_cell) continue;
            visible = true;
            min_column = std::min(min_column, index % columns);
            max_column = std::max(max_column, index % columns);
            min_row = std::min(min_row, index / columns - first_row);
            max_row = std::max(max_row, index / columns - first_row);
        }
        const AquariumSpeciesEntry* species = catalog.findApproved(placement.species_id);
        if (!visible || !species) continue;
        PokemonSpriteRequest request;
        request.species_id = species->dex;
        request.species_slug = species->species;
        request.form_key = species->form;
        const TextureHandle sprite = sprite_assets_->loadPokemonTexture(renderer, request);
        if (!sprite.texture) continue;
        const SDL_Rect source = spriteSourceRect(*species, sprite);
        const SDL_Rect bounds{grid_x + min_column * (cell + 3),
            grid_y + min_row * (cell + 3),
            (max_column - min_column + 1) * (cell + 3) - 3,
            (max_row - min_row + 1) * (cell + 3) - 3};
        const float scale = 0.92f * std::min(
            static_cast<float>(std::max(1, bounds.w - 4)) / std::max(1, source.w),
            static_cast<float>(std::max(1, bounds.h - 4)) / std::max(1, source.h));
        SDL_Rect destination{0, 0, std::max(1, static_cast<int>(source.w * scale)),
            std::max(1, static_cast<int>(source.h * scale))};
        destination.x = bounds.x + (bounds.w - destination.w) / 2;
        destination.y = bounds.y + (bounds.h - destination.h) / 2;
        SDL_RenderSetClipRect(renderer, &bounds);
        SDL_RenderCopy(renderer, sprite.texture.get(), &source, &destination);
        SDL_RenderSetClipRect(renderer, nullptr);
    }

    if (const auto preview = controller.focusedPreviewPlacement()) {
        SDL_SetRenderDrawColor(renderer, 255, 205, 61, 255);
        for (const int index : preview->cell_indices) {
            if (index < first_cell || index >= final_cell) continue;
            SDL_Rect rect{grid_x + (index % columns) * (cell + 3),
                grid_y + (index / columns - first_row) * (cell + 3), cell, cell};
            SDL_RenderDrawRect(renderer, &rect);
            rect = {rect.x + 2, rect.y + 2, rect.w - 4, rect.h - 4};
            SDL_RenderDrawRect(renderer, &rect);
        }
    }
    if (total_rows > visible_rows) {
        const SDL_Rect track{grid_area.x + grid_area.w - 6, grid_y, 6,
            visible_rows * (cell + 3) - 3};
        fillRound(renderer, track, 3, {88, 158, 187, 255});
        const int thumb_height = std::max(16, track.h * visible_rows / total_rows);
        const int travel = std::max(0, track.h - thumb_height);
        const int thumb_y = track.y + (controller.maxCapacityFirstVisibleRow() > 0
            ? travel * first_row / controller.maxCapacityFirstVisibleRow() : 0);
        fillRound(renderer, {track.x - 1, thumb_y, 8, thumb_height}, 4,
            {33, 133, 190, 255});
    }
    if (controller.focusArea() == AquariumStockingController::FocusArea::Tank) {
        SDL_SetRenderDrawColor(renderer, 218, 54, 54, 255);
        for (int inset = 0; inset < 4; ++inset) {
            const SDL_Rect ring{grid_area.x - inset, grid_area.y - inset,
                grid_area.w + inset * 2, grid_area.h + inset * 2};
            SDL_RenderDrawRect(renderer, &ring);
        }
    }
}

} // namespace pr::gameplay::world3d::aquarium::construction
