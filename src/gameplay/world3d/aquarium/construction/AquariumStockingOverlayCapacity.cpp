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

struct CapacityGridLayout {
    SDL_Rect area{};
    int columns = 1;
    int first_row = 0;
    int visible_rows = 1;
    int cell = 18;
    int grid_x = 0;
    int grid_y = 0;
};

CapacityGridLayout capacityGridLayout(
    int width, int height, const AquariumStockingController& controller) {
    const SDL_Rect panel = capacityArea(width, height);
    CapacityGridLayout layout;
    layout.area = {panel.x + 12, panel.y + 98, panel.w - 24, panel.h - 110};
    layout.columns = std::max(1, controller.capacityColumns());
    const int total_rows = std::max(1, controller.capacityRows());
    layout.visible_rows = std::min(
        total_rows, AquariumStockingController::kCapacityVisibleRows);
    layout.first_row = controller.capacityFirstVisibleRow();
    layout.cell = std::clamp(std::min(
        layout.area.w / layout.columns - 3,
        layout.area.h / layout.visible_rows - 3), 18, 62);
    layout.grid_x = layout.area.x + std::max(0,
        (layout.area.w - layout.columns * (layout.cell + 3) + 3) / 2);
    layout.grid_y = layout.area.y + std::max(0,
        (layout.area.h - layout.visible_rows * (layout.cell + 3) + 3) / 2);
    return layout;
}

Color residentColor(const std::string& species_id, std::size_t placement_index) {
    std::uint32_t hash = 2166136261U;
    for (const unsigned char byte : species_id) hash = (hash ^ byte) * 16777619U;
    hash = (hash ^ static_cast<std::uint32_t>(placement_index + 1U)) * 16777619U;
    return Color{
        static_cast<Uint8>(70U + hash % 90U),
        static_cast<Uint8>(125U + (hash >> 8U) % 90U),
        static_cast<Uint8>(145U + (hash >> 16U) % 85U), 255};
}

Color lightened(Color color) {
    constexpr int kWhiteWeight = 72;
    color.r = static_cast<Uint8>((color.r * (255 - kWhiteWeight) + 255 * kWhiteWeight) / 255);
    color.g = static_cast<Uint8>((color.g * (255 - kWhiteWeight) + 255 * kWhiteWeight) / 255);
    color.b = static_cast<Uint8>((color.b * (255 - kWhiteWeight) + 255 * kWhiteWeight) / 255);
    color.a = 245;
    return color;
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
    const CapacityGridLayout layout = capacityGridLayout(width, height, controller);
    const SDL_Rect grid_area = layout.area;
    const int columns = layout.columns;
    const int total_rows = std::max(1, controller.capacityRows());
    const int visible_rows = layout.visible_rows;
    const int first_row = layout.first_row;
    const int cell = layout.cell;
    const int grid_x = layout.grid_x;
    const int grid_y = layout.grid_y;
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
        const Color owner_color = owner >= 0
            ? residentColor(placements[static_cast<std::size_t>(owner)].species_id,
                static_cast<std::size_t>(owner))
            : Color{189, 226, 237, 255};
        fillRound(renderer, rect, 5, owner_color);
        fillRound(renderer, {rect.x + 3, rect.y + 3, rect.w - 6, rect.h - 6}, 3,
            owner >= 0 ? lightened(owner_color) : Color{229, 248, 253, 255});
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
        // Capacity belongs to the colored footprint, not the icon. A Kyogre
        // therefore occupies more cells without turning its menu sprite into
        // a giant image that obscures the stocking board.
        const float scale = 0.88f * std::min(
            static_cast<float>(std::max(1, cell - 4)) / std::max(1, source.w),
            static_cast<float>(std::max(1, cell - 4)) / std::max(1, source.h));
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
}

std::optional<AquariumResidentCapacityPlacement>
AquariumStockingOverlay::capacityPlacementAt(
    int width, int height, int point_x, int point_y,
    const AquariumStockingController& controller) const {
    const CapacityGridLayout layout = capacityGridLayout(width, height, controller);
    const int stride = layout.cell + 3;
    const int local_x = point_x - layout.grid_x;
    const int local_y = point_y - layout.grid_y;
    if (local_x < 0 || local_y < 0) return std::nullopt;
    const int column = local_x / stride;
    const int visible_row = local_y / stride;
    if (column < 0 || column >= layout.columns || visible_row < 0 ||
        visible_row >= layout.visible_rows || local_x % stride >= layout.cell ||
        local_y % stride >= layout.cell) return std::nullopt;
    const int cell_index = (layout.first_row + visible_row) * layout.columns + column;
    if (cell_index < 0 || cell_index >= controller.capacityCells()) return std::nullopt;
    for (const auto& placement : controller.capacityPlacements()) {
        if (std::find(placement.cell_indices.begin(), placement.cell_indices.end(),
                cell_index) != placement.cell_indices.end()) return placement;
    }
    return std::nullopt;
}

bool AquariumStockingOverlay::capacityGridAt(
    int width, int height, int point_x, int point_y,
    const AquariumStockingController& controller) const {
    const CapacityGridLayout layout = capacityGridLayout(width, height, controller);
    const int stride = layout.cell + 3;
    const int local_x = point_x - layout.grid_x;
    const int local_y = point_y - layout.grid_y;
    return local_x >= 0 && local_y >= 0 &&
        local_x < layout.columns * stride - 3 &&
        local_y < layout.visible_rows * stride - 3;
}

} // namespace pr::gameplay::world3d::aquarium::construction
