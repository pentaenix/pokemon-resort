#include "gameplay/world3d/aquarium/construction/AquariumStockingOverlay.hpp"
#include "gameplay/world3d/aquarium/AquariumExhibitPreset.hpp"

#include "ui/transfer_system/GameTransferConfig.hpp"

#include <SDL_image.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <utility>

namespace pr::gameplay::world3d::aquarium::construction {
namespace {

struct Palette {
    Color blue{33, 133, 190, 255};
};

void fill(SDL_Renderer* renderer, const SDL_Rect& rect, Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &rect);
}

void fillRound(SDL_Renderer* renderer, SDL_Rect rect, int radius, Color color) {
    radius = std::clamp(radius, 0, std::min(rect.w, rect.h) / 2);
    fill(renderer, SDL_Rect{rect.x + radius, rect.y, rect.w - radius * 2, rect.h}, color);
    for (int y = 0; y < rect.h; ++y) {
        const int dy = y < radius ? radius - y : y >= rect.h - radius ? y - (rect.h - radius - 1) : 0;
        const int inset = dy > 0
            ? radius - static_cast<int>(std::sqrt(std::max(0, radius * radius - dy * dy))) : 0;
        if (inset > 0) fill(renderer, SDL_Rect{rect.x + inset, rect.y + y, rect.w - inset * 2, 1}, color);
        else if (y >= radius && y < rect.h - radius) fill(renderer, SDL_Rect{rect.x, rect.y + y, rect.w, 1}, color);
    }
}

SDL_Rect closeRect(int width, int height) {
    (void)width;
    (void)height;
    return {50, 12, 76, 76};
}

SDL_Rect catalogueViewportRect(int width, int height) {
    (void)width;
    (void)height;
    return {40, 100, BoxViewport::kViewportWidth, BoxViewport::kViewportHeight};
}

SDL_Rect slotRect(int width, int height, int local_index) {
    const SDL_Rect viewport = catalogueViewportRect(width, height);
    constexpr int kSlotWidth = 78;
    constexpr int kSlotHeight = 72;
    constexpr int kGap = 12;
    constexpr int kGridInsetX = 16;
    constexpr int kGridInsetY = 106;
    return {
        viewport.x + kGridInsetX + (local_index % AquariumStockingController::kColumns) *
            (kSlotWidth + kGap),
        viewport.y + kGridInsetY + (local_index / AquariumStockingController::kColumns) *
            (kSlotHeight + kGap),
        kSlotWidth,
        kSlotHeight};
}

SDL_Rect tabTrackRect(int width, int height) {
    (void)height;
    const int right_x = std::max(0, width - 40 - BoxViewport::kViewportWidth);
    return {right_x + 15, 13, 530, 77};
}

SDL_Rect tabHalfRect(int width, int height, bool exhibit) {
    const SDL_Rect track = tabTrackRect(width, height);
    return {track.x + (exhibit ? track.w / 2 : 0), track.y,
        track.w / 2, track.h};
}

SDL_Rect previousBoxRect(int width, int height) {
    const SDL_Rect viewport = catalogueViewportRect(width, height);
    return {viewport.x + 12, viewport.y + 12, 50, 82};
}

SDL_Rect nextBoxRect(int width, int height) {
    const SDL_Rect viewport = catalogueViewportRect(width, height);
    return {viewport.x + 498, viewport.y + 12, 50, 82};
}

SDL_Rect capacityArea(int width, int height) {
    (void)height;
    return {std::max(0, width - 40 - BoxViewport::kViewportWidth), 100,
        BoxViewport::kViewportWidth, BoxViewport::kViewportHeight};
}

void drawTextureCentered(
    SDL_Renderer* renderer, const TextureHandle& texture, const SDL_Rect& bounds) {
    if (!texture.texture) return;
    const SDL_Rect destination{
        bounds.x + (bounds.w - texture.width) / 2,
        bounds.y + (bounds.h - texture.height) / 2,
        texture.width,
        texture.height};
    SDL_RenderCopy(renderer, texture.texture.get(), nullptr, &destination);
}

void drawFitSymbol(
    SDL_Renderer* renderer,
    SDL_Rect card,
    AquariumHabitatFitReason reason,
    bool capacity_blocked) {
    const int size = std::clamp(card.w / 4, 13, 20);
    const SDL_Rect badge{card.x + 4, card.y + card.h - size - 4, size, size};
    fillRound(renderer, badge, size / 2, {225, 86, 79, 245});
    SDL_SetRenderDrawColor(renderer, 255, 252, 232, 255);
    const int left = badge.x + 4;
    const int right = badge.x + badge.w - 5;
    const int top = badge.y + 4;
    const int bottom = badge.y + badge.h - 5;
    const int middle_x = badge.x + badge.w / 2;
    const int middle_y = badge.y + badge.h / 2;
    if (capacity_blocked || reason == AquariumHabitatFitReason::MissingEnvelope) {
        const SDL_Rect grid{left, top, right - left, bottom - top};
        SDL_RenderDrawRect(renderer, &grid);
        SDL_RenderDrawLine(renderer, left, bottom, right, top);
    } else if (reason == AquariumHabitatFitReason::VerticalClearance) {
        SDL_RenderDrawLine(renderer, middle_x, top, middle_x, bottom);
        SDL_RenderDrawLine(renderer, middle_x, top, middle_x - 3, top + 3);
        SDL_RenderDrawLine(renderer, middle_x, top, middle_x + 3, top + 3);
        SDL_RenderDrawLine(renderer, middle_x, bottom, middle_x - 3, bottom - 3);
        SDL_RenderDrawLine(renderer, middle_x, bottom, middle_x + 3, bottom - 3);
    } else if (reason == AquariumHabitatFitReason::TurningSpace) {
        SDL_RenderDrawLine(renderer, left + 1, middle_y + 2, left + 1, top + 2);
        SDL_RenderDrawLine(renderer, left + 1, top + 2, right - 2, top + 2);
        SDL_RenderDrawLine(renderer, right - 2, top + 2, right - 2, middle_y + 1);
        SDL_RenderDrawLine(renderer, right - 2, middle_y + 1, right - 5, middle_y - 2);
        SDL_RenderDrawLine(renderer, right - 2, middle_y + 1, right - 5, middle_y + 4);
    } else {
        SDL_RenderDrawLine(renderer, left, middle_y, right, middle_y);
        SDL_RenderDrawLine(renderer, left, middle_y, left + 3, middle_y - 3);
        SDL_RenderDrawLine(renderer, left, middle_y, left + 3, middle_y + 3);
        SDL_RenderDrawLine(renderer, right, middle_y, right - 3, middle_y - 3);
        SDL_RenderDrawLine(renderer, right, middle_y, right - 3, middle_y + 3);
    }
}

} // namespace

AquariumStockingOverlay::~AquariumStockingOverlay() {
    resetUiResources();
    sprite_assets_.reset();
    resetRasterTarget();
}

void AquariumStockingOverlay::resetUiResources() const {
    catalogue_viewport_.reset();
    pill_font_.reset();
    pokemon_label_selected_ = {};
    pokemon_label_unselected_ = {};
    exhibit_label_selected_ = {};
    exhibit_label_unselected_ = {};
    background_texture_ = {};
    basic_tool_texture_ = {};
    exit_texture_ = {};
    tank_label_ = {};
    summary_font_.reset();
    summary_small_font_.reset();
    ui_renderer_ = nullptr;
}

void AquariumStockingOverlay::resetRasterTarget() const {
    if (ui_renderer_ == raster_renderer_) resetUiResources();
    if (raster_renderer_) SDL_DestroyRenderer(raster_renderer_);
    if (raster_surface_) SDL_FreeSurface(raster_surface_);
    raster_renderer_ = nullptr;
    raster_surface_ = nullptr;
    raster_pixels_ = {};
}

void AquariumStockingOverlay::configure(std::string project_root) {
    resetUiResources();
    sprite_assets_.reset();
    resetRasterTarget();
    project_root_ = std::move(project_root);
    const auto transfer_style = transfer_system::loadGameTransfer(project_root_);
    box_style_ = transfer_style.box_viewport;
    pill_style_ = transfer_style.pill_toggle;
    carousel_style_ = transfer_style.tool_carousel;
    info_style_ = transfer_style.info_banner;
    box_style_.arrow_mod_color = {18, 74, 119, 255};
    box_style_.viewport_background_color = {117, 198, 226, 255};
    box_style_.viewport_border_color = {34, 112, 162, 255};
    box_style_.viewport_border_thickness = 4;
    box_style_.name_plate_background_color = {236, 251, 255, 255};
    box_style_.slot_background_color = {229, 248, 253, 255};
    box_style_.disabled_slot_background_enabled = true;
    box_style_.disabled_slot_background_color = {112, 159, 178, 255};
    box_style_.disabled_slot_background_alpha = 225;
    box_style_.disabled_sprite_mod_enabled = true;
    box_style_.disabled_sprite_mod_color = {71, 104, 119, 255};
    box_style_.disabled_sprite_mod_alpha = 105;
    box_style_.box_name_color = {18, 74, 119, 255};
    pill_style_.track_color = {34, 112, 162, 255};
    pill_style_.pill_color = {236, 251, 255, 255};
    pill_style_.label_unselected_color = {228, 248, 253, 255};
    pill_style_.label_selected_color = {18, 74, 119, 255};
    carousel_style_.viewport_color = {117, 198, 226, 255};
    info_style_.separator_color = {34, 112, 162, 255};
    info_style_.info_background_color = {225, 246, 252, 255};
    sprite_assets_ = PokeSpriteAssets::create(project_root_);
    sprite_source_rects_.clear();
}

void AquariumStockingOverlay::ensureUiResources(
    SDL_Renderer* renderer, int width, int height) const {
    const SDL_Rect viewport = catalogueViewportRect(width, height);
    if (ui_renderer_ != renderer) {
        resetUiResources();
        ui_renderer_ = renderer;
        const std::string font_path = "assets/fonts/power clear bold.ttf";
        catalogue_viewport_ = std::make_unique<pr::BoxViewport>(
            renderer, project_root_, font_path, box_style_,
            BoxViewportRole::ExternalGameSave, viewport.x, viewport.y);
        catalogue_viewport_->setFooterMode(BoxViewport::FooterMode::Hidden);
        pill_font_ = loadFont(font_path, std::max(12, pill_style_.font_pt), project_root_);
        summary_font_ = loadFont(font_path, 32, project_root_);
        summary_small_font_ = loadFont(font_path, 24, project_root_);
        pokemon_label_selected_ = renderTextTexture(
            renderer, pill_font_.get(), "POKEMON", pill_style_.label_selected_color);
        pokemon_label_unselected_ = renderTextTexture(
            renderer, pill_font_.get(), "POKEMON", pill_style_.label_unselected_color);
        exhibit_label_selected_ = renderTextTexture(
            renderer, pill_font_.get(), "EXHIBIT", pill_style_.label_selected_color);
        exhibit_label_unselected_ = renderTextTexture(
            renderer, pill_font_.get(), "EXHIBIT", pill_style_.label_unselected_color);
        tank_label_ = renderTextTexture(
            renderer, pill_font_.get(), "TANK", box_style_.box_name_color);
        const auto load_texture = [&](const char* relative_path) {
            TextureHandle texture;
            const std::string path = project_root_ + "/" + relative_path;
            if (SDL_Texture* raw = IMG_LoadTexture(renderer, path.c_str())) {
                texture.texture.reset(raw, SDL_DestroyTexture);
                SDL_QueryTexture(raw, nullptr, nullptr, &texture.width, &texture.height);
                SDL_SetTextureBlendMode(raw, SDL_BLENDMODE_BLEND);
            }
            return texture;
        };
        background_texture_ = load_texture("assets/transfer_select_save/background.png");
        basic_tool_texture_ = load_texture("assets/game_transfer/icon_basic.png");
        exit_texture_ = load_texture("assets/game_transfer/exit.png");
    }
    if (catalogue_viewport_) catalogue_viewport_->setViewportOrigin(viewport.x, viewport.y);
}

BoxViewportModel AquariumStockingOverlay::catalogueModel(
    SDL_Renderer* renderer,
    const AquariumSpeciesCatalog& catalog,
    const AquariumStockingController& controller) const {
    BoxViewportModel model;
    model.box_name = controller.tab() == AquariumStockingController::Tab::Pokemon
        ? "AQUARIUM " + std::to_string(controller.boxIndex() + 1)
        : "EXHIBITS";
    model.visible_slot_count = AquariumStockingController::kBoxSize;
    model.slot_columns = AquariumStockingController::kColumns;
    if (controller.tab() == AquariumStockingController::Tab::Exhibit || !sprite_assets_) {
        model.disabled_slots.fill(true);
        return model;
    }
    for (int local = 0; local < AquariumStockingController::kBoxSize; ++local) {
        const int index = controller.pageStart() + local;
        if (index >= static_cast<int>(catalog.approved.size())) {
            model.disabled_slots[static_cast<std::size_t>(local)] = true;
            continue;
        }
        const auto& species = catalog.approved[static_cast<std::size_t>(index)];
        PokemonSpriteRequest request;
        request.species_id = species.dex;
        request.species_slug = species.species;
        request.form_key = species.form;
        model.slot_sprites[static_cast<std::size_t>(local)] =
            sprite_assets_->loadPokemonTexture(renderer, request);
        model.disabled_slots[static_cast<std::size_t>(local)] =
            !controller.speciesCanBeAdded(static_cast<std::size_t>(index));
    }
    return model;
}

std::optional<std::size_t> AquariumStockingOverlay::speciesAt(
    int width, int height, int point_x, int point_y,
    const AquariumStockingController& controller) const {
    if (controller.tab() != AquariumStockingController::Tab::Pokemon) {
        return std::nullopt;
    }
    const int start = controller.pageStart();
    for (int local = 0; local < AquariumStockingController::kPageSize; ++local) {
        const SDL_Rect rect = slotRect(width, height, local);
        const SDL_Point point{point_x, point_y};
        if (SDL_PointInRect(&point, &rect)) return static_cast<std::size_t>(start + local);
    }
    return std::nullopt;
}

bool AquariumStockingOverlay::closeAt(
    int width, int height, int point_x, int point_y) const {
    const SDL_Rect rect = closeRect(width, height);
    const SDL_Point point{point_x, point_y};
    return SDL_PointInRect(&point, &rect);
}

bool AquariumStockingOverlay::capacityAt(
    int width, int height, int point_x, int point_y) const {
    const SDL_Rect rect = capacityArea(width, height);
    const SDL_Point point{point_x, point_y};
    return SDL_PointInRect(&point, &rect);
}

bool AquariumStockingOverlay::previousBoxAt(
    int width, int height, int point_x, int point_y) const {
    const SDL_Rect rect = previousBoxRect(width, height);
    const SDL_Point point{point_x, point_y};
    return SDL_PointInRect(&point, &rect);
}

bool AquariumStockingOverlay::nextBoxAt(
    int width, int height, int point_x, int point_y) const {
    const SDL_Rect rect = nextBoxRect(width, height);
    const SDL_Point point{point_x, point_y};
    return SDL_PointInRect(&point, &rect);
}

std::optional<AquariumStockingController::Tab> AquariumStockingOverlay::tabAt(
    int width, int height, int point_x, int point_y) const {
    const SDL_Point point{point_x, point_y};
    const SDL_Rect pokemon = tabHalfRect(width, height, false);
    if (SDL_PointInRect(&point, &pokemon)) return AquariumStockingController::Tab::Pokemon;
    const SDL_Rect exhibit = tabHalfRect(width, height, true);
    if (SDL_PointInRect(&point, &exhibit)) return AquariumStockingController::Tab::Exhibit;
    return std::nullopt;
}

std::optional<std::size_t> AquariumStockingOverlay::exhibitPresetAt(
    int width, int height, int point_x, int point_y) const {
    const SDL_Point point{point_x, point_y};
    for (std::size_t index = 0; index < kAquariumExhibitPresets.size(); ++index) {
        const SDL_Rect rect = exhibitPresetRect(width, height, index);
        if (SDL_PointInRect(&point, &rect)) return index;
    }
    return std::nullopt;
}

const AquariumStockingOverlayPixels& AquariumStockingOverlay::rasterize(
    int width,
    int height,
    const AquariumSpeciesCatalog& catalog,
    const AquariumStockingController& controller) const {
    width = std::max(1, width);
    height = std::max(1, height);
    std::ostringstream signature;
    signature << width << 'x' << height << ':' << catalog.revision << ':'
              << controller.tankId() << ':' << controller.focusedIndex() << ':'
              << controller.pageStart() << ':' << static_cast<int>(controller.tab()) << ':'
              << static_cast<int>(controller.focusArea()) << ':'
              << controller.holdingSpecies() << ':' << controller.pointerActive() << ':'
              << controller.pointerX() << ',' << controller.pointerY() << ':'
              << controller.focusedExhibitPresetIndex() << ':'
              << controller.currentExhibitPresetId() << ':'
              << static_cast<int>(controller.focusedExhibitControl()) << ':'
              << controller.focusedBrightnessLevel() << ':'
              << controller.focusedMurkinessLevel() << ':'
              << controller.focusedSubstrateIndex() << ':'
              << controller.capacityCells() << ':' << controller.capacityColumns() << ':'
              << controller.capacityFirstVisibleRow() << ':'
              << controller.usedCapacityCells();
    for (const auto& resident : controller.residents()) {
        signature << ':' << resident.species_id << '=' << resident.count;
    }
    const std::string content_key = signature.str();
    if (raster_renderer_ && raster_surface_ && raster_pixels_.content_key == content_key &&
        !raster_pixels_.rgba.empty()) {
        return raster_pixels_;
    }

    if (!raster_surface_ || raster_surface_->w != width || raster_surface_->h != height) {
        sprite_assets_.reset();
        resetRasterTarget();
        raster_surface_ = SDL_CreateRGBSurfaceWithFormat(
            0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
        if (raster_surface_) raster_renderer_ = SDL_CreateSoftwareRenderer(raster_surface_);
        sprite_assets_ = PokeSpriteAssets::create(project_root_);
    }
    if (!raster_renderer_ || !raster_surface_) return raster_pixels_;

    SDL_SetRenderDrawBlendMode(raster_renderer_, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(raster_renderer_, 0, 0, 0, 0);
    SDL_RenderClear(raster_renderer_);
    render(raster_renderer_, width, height, catalog, controller);
    SDL_RenderPresent(raster_renderer_);

    const bool locked = SDL_MUSTLOCK(raster_surface_) && SDL_LockSurface(raster_surface_) == 0;
    if (SDL_MUSTLOCK(raster_surface_) && !locked) return raster_pixels_;
    raster_pixels_.width = width;
    raster_pixels_.height = height;
    raster_pixels_.content_key = content_key;
    raster_pixels_.rgba.resize(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
    const auto* source = static_cast<const std::uint8_t*>(raster_surface_->pixels);
    const std::size_t row_bytes = static_cast<std::size_t>(width) * 4U;
    for (int row = 0; row < height; ++row) {
        std::memcpy(
            raster_pixels_.rgba.data() + static_cast<std::size_t>(row) * row_bytes,
            source + static_cast<std::size_t>(row) *
                static_cast<std::size_t>(raster_surface_->pitch),
            row_bytes);
    }
    if (locked) SDL_UnlockSurface(raster_surface_);
    return raster_pixels_;
}

void AquariumStockingOverlay::render(
    SDL_Renderer* renderer,
    int width,
    int height,
    const AquariumSpeciesCatalog& catalog,
    const AquariumStockingController& controller) const {
    if (!renderer || !controller.active()) return;
    const Palette palette;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    ensureUiResources(renderer, width, height);
    renderTransferChrome(renderer, width, height, catalog, controller);
    if (catalogue_viewport_ &&
        controller.tab() == AquariumStockingController::Tab::Pokemon) {
        catalogue_viewport_->setHeaderMode(
            controller.tab() == AquariumStockingController::Tab::Pokemon
                ? BoxViewport::HeaderMode::Normal
                : BoxViewport::HeaderMode::BoxSpace,
            false);
        catalogue_viewport_->snapContentToModel(catalogueModel(renderer, catalog, controller));
        catalogue_viewport_->render(renderer);
    }

    if (controller.tab() == AquariumStockingController::Tab::Pokemon) {
        const int start = controller.pageStart();
        for (int local = 0; local < AquariumStockingController::kBoxSize; ++local) {
            const int index = start + local;
            if (index >= static_cast<int>(catalog.approved.size())) continue;
            const SDL_Rect rect = slotRect(width, height, local);
            const bool selected = index == controller.focusedIndex() &&
                controller.focusArea() == AquariumStockingController::FocusArea::Catalogue;
            const auto& fit = controller.habitatFit(static_cast<std::size_t>(index));
            const bool can_add = controller.speciesCanBeAdded(static_cast<std::size_t>(index));
            if (selected) {
                const Color focus = carousel_style_.frame_basic;
                SDL_SetRenderDrawColor(renderer, focus.r, focus.g, focus.b, focus.a);
                for (int inset = 0; inset < 4; ++inset) {
                    const SDL_Rect ring{rect.x - inset, rect.y - inset,
                        rect.w + inset * 2, rect.h + inset * 2};
                    SDL_RenderDrawRect(renderer, &ring);
                }
            }
            if (!can_add) drawFitSymbol(renderer, rect, fit.reason, fit.fits());
            const auto& species = catalog.approved[static_cast<std::size_t>(index)];
            const int count = controller.residentCount(species.id);
            if (count <= 0) continue;
            const int badge = 22;
            fillRound(renderer,
                SDL_Rect{rect.x + rect.w - badge + 3, rect.y - 3, badge, badge},
                badge / 2, palette.blue);
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            for (int dot = 0; dot < std::min(4, count); ++dot) {
                const SDL_Rect dot_rect{rect.x + rect.w - badge + 7 + (dot % 2) * 6,
                    rect.y + 3 + (dot / 2) * 6, 4, 4};
                SDL_RenderFillRect(renderer, &dot_rect);
            }
        }
    }

    const SDL_Rect track = tabTrackRect(width, height);
    fillRound(renderer, track, track.h / 2, pill_style_.track_color);
    const bool exhibit = controller.tab() == AquariumStockingController::Tab::Exhibit;
    const int pad = std::max(0, pill_style_.pill_inset);
    const int inner_width = std::max(1, track.w - pad * 2);
    const int inner_height = std::max(1, track.h - pad * 2);
    const int pill_width = std::min(pill_style_.pill_width, inner_width);
    const int pill_height = std::min(pill_style_.pill_height, inner_height);
    const SDL_Rect pill{track.x + pad + (exhibit ? inner_width - pill_width : 0),
        track.y + pad + (inner_height - pill_height) / 2,
        pill_width, pill_height};
    fillRound(renderer, pill, pill.h / 2, pill_style_.pill_color);
    drawTextureCentered(renderer,
        exhibit ? pokemon_label_unselected_ : pokemon_label_selected_,
        tabHalfRect(width, height, false));
    drawTextureCentered(renderer,
        exhibit ? exhibit_label_selected_ : exhibit_label_unselected_,
        tabHalfRect(width, height, true));

    if (controller.tab() == AquariumStockingController::Tab::Pokemon) {
        renderCapacity(renderer, width, height, catalog, controller);
    } else {
        renderExhibit(renderer, width, height, controller);
    }
    renderInfoBanner(renderer, width, height, catalog, controller);
    renderHeldSpecies(renderer, width, height, catalog, controller);

}

} // namespace pr::gameplay::world3d::aquarium::construction
