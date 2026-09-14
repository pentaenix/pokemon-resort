#pragma once

#include "core/assets/Assets.hpp"
#include "core/assets/Font.hpp"
#include "core/assets/PokeSpriteAssets.hpp"
#include "gameplay/world3d/aquarium/AquariumSpeciesCatalog.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumStockingController.hpp"
#include "ui/BoxViewport.hpp"

#include <SDL.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace pr::gameplay::world3d::aquarium::construction {

struct AquariumStockingOverlayPixels {
    int width = 0;
    int height = 0;
    std::string content_key;
    std::vector<std::uint8_t> rgba;
};

class AquariumStockingOverlay {
public:
    AquariumStockingOverlay() = default;
    ~AquariumStockingOverlay();
    AquariumStockingOverlay(const AquariumStockingOverlay&) = delete;
    AquariumStockingOverlay& operator=(const AquariumStockingOverlay&) = delete;

    void configure(std::string project_root);
    void render(
        SDL_Renderer* renderer,
        int width,
        int height,
        const AquariumSpeciesCatalog& catalog,
        const AquariumStockingController& controller) const;
    std::optional<std::size_t> speciesAt(
        int width, int height, int point_x, int point_y,
        const AquariumStockingController& controller) const;
    bool closeAt(int width, int height, int point_x, int point_y) const;
    bool capacityAt(int width, int height, int point_x, int point_y) const;
    bool previousBoxAt(int width, int height, int point_x, int point_y) const;
    bool nextBoxAt(int width, int height, int point_x, int point_y) const;
    std::optional<AquariumStockingController::Tab> tabAt(
        int width, int height, int point_x, int point_y) const;
    std::optional<std::size_t> exhibitPresetAt(
        int width, int height, int point_x, int point_y) const;
    std::optional<int> exhibitBrightnessAt(
        int width, int height, int point_x, int point_y) const;
    std::optional<int> exhibitMurkinessAt(
        int width, int height, int point_x, int point_y) const;
    int exhibitBrightnessLevelAtX(int width, int point_x) const;
    int exhibitMurkinessLevelAtX(int width, int point_x) const;
    std::optional<std::size_t> exhibitSubstrateAt(
        int width, int height, int point_x, int point_y) const;
    const AquariumStockingOverlayPixels& rasterize(
        int width,
        int height,
        const AquariumSpeciesCatalog& catalog,
        const AquariumStockingController& controller) const;

private:
    void resetRasterTarget() const;
    void resetUiResources() const;
    void ensureUiResources(SDL_Renderer* renderer, int width, int height) const;
    BoxViewportModel catalogueModel(
        SDL_Renderer* renderer,
        const AquariumSpeciesCatalog& catalog,
        const AquariumStockingController& controller) const;
    void renderCapacity(
        SDL_Renderer* renderer,
        int width,
        int height,
        const AquariumSpeciesCatalog& catalog,
        const AquariumStockingController& controller) const;
    void renderExhibit(
        SDL_Renderer* renderer,
        int width,
        int height,
        const AquariumStockingController& controller) const;
    SDL_Rect exhibitPresetRect(int width, int height, std::size_t index) const;
    void renderTransferChrome(
        SDL_Renderer* renderer,
        int width,
        int height,
        const AquariumSpeciesCatalog& catalog,
        const AquariumStockingController& controller) const;
    void renderInfoBanner(
        SDL_Renderer* renderer,
        int width,
        int height,
        const AquariumSpeciesCatalog& catalog,
        const AquariumStockingController& controller) const;
    void renderHeldSpecies(
        SDL_Renderer* renderer,
        int width,
        int height,
        const AquariumSpeciesCatalog& catalog,
        const AquariumStockingController& controller) const;
    SDL_Rect spriteSourceRect(
        const AquariumSpeciesEntry& species,
        const TextureHandle& texture) const;

    std::string project_root_;
    GameTransferBoxViewportStyle box_style_{};
    GameTransferPillToggleStyle pill_style_{};
    GameTransferToolCarouselStyle carousel_style_{};
    GameTransferInfoBannerStyle info_style_{};
    mutable std::shared_ptr<PokeSpriteAssets> sprite_assets_;
    mutable SDL_Renderer* ui_renderer_ = nullptr;
    mutable std::unique_ptr<pr::BoxViewport> catalogue_viewport_;
    mutable FontHandle pill_font_;
    mutable FontHandle summary_font_;
    mutable FontHandle summary_small_font_;
    mutable TextureHandle pokemon_label_selected_;
    mutable TextureHandle pokemon_label_unselected_;
    mutable TextureHandle exhibit_label_selected_;
    mutable TextureHandle exhibit_label_unselected_;
    mutable TextureHandle background_texture_;
    mutable TextureHandle basic_tool_texture_;
    mutable TextureHandle exit_texture_;
    mutable TextureHandle tank_label_;
    mutable SDL_Surface* raster_surface_ = nullptr;
    mutable SDL_Renderer* raster_renderer_ = nullptr;
    mutable AquariumStockingOverlayPixels raster_pixels_;
    mutable std::unordered_map<std::string, SDL_Rect> sprite_source_rects_;
};

} // namespace pr::gameplay::world3d::aquarium::construction
