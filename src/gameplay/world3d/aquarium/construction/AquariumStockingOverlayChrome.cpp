#include "gameplay/world3d/aquarium/construction/AquariumStockingOverlay.hpp"
#include "gameplay/world3d/aquarium/AquariumExhibitPreset.hpp"
#include "gameplay/world3d/aquarium/AquariumSubstratePreset.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace pr::gameplay::world3d::aquarium::construction {
namespace {

void fill(SDL_Renderer* renderer, const SDL_Rect& rect, Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &rect);
}

void fillRound(SDL_Renderer* renderer, SDL_Rect rect, int radius, Color color) {
    radius = std::clamp(radius, 0, std::min(rect.w, rect.h) / 2);
    for (int y = 0; y < rect.h; ++y) {
        const int dy = y < radius ? radius - y
            : y >= rect.h - radius ? y - (rect.h - radius - 1) : 0;
        const int inset = dy > 0
            ? radius - static_cast<int>(std::sqrt(std::max(0, radius * radius - dy * dy))) : 0;
        const SDL_Rect row{rect.x + inset, rect.y + y, rect.w - inset * 2, 1};
        if (row.w > 0) fill(renderer, row, color);
    }
}

void drawCentered(SDL_Renderer* renderer, const TextureHandle& texture, const SDL_Rect& bounds) {
    if (!texture.texture) return;
    const SDL_Rect destination{bounds.x + (bounds.w - texture.width) / 2,
        bounds.y + (bounds.h - texture.height) / 2, texture.width, texture.height};
    SDL_RenderCopy(renderer, texture.texture.get(), nullptr, &destination);
}

SDL_Rect catalogueSlotRect(int local_index) {
    constexpr int slot_width = 78;
    constexpr int slot_height = 72;
    constexpr int gap = 12;
    return {40 + 16 + (local_index % AquariumStockingController::kColumns) *
            (slot_width + gap),
        100 + 106 + (local_index / AquariumStockingController::kColumns) *
            (slot_height + gap),
        slot_width, slot_height};
}

TextureHandle textTexture(
    SDL_Renderer* renderer, TTF_Font* font, const std::string& text, Color color) {
    return renderTextTexture(renderer, font, text, color);
}

} // namespace

void AquariumStockingOverlay::renderTransferChrome(
    SDL_Renderer* renderer,
    int width,
    int height,
    const AquariumSpeciesCatalog&,
    const AquariumStockingController&) const {
    fill(renderer, {0, 0, width, height}, {35, 119, 166, 255});
    if (background_texture_.texture) {
        SDL_SetTextureBlendMode(background_texture_.texture.get(), SDL_BLENDMODE_BLEND);
        SDL_SetTextureColorMod(background_texture_.texture.get(), 145, 210, 235);
        SDL_SetTextureAlphaMod(background_texture_.texture.get(), 220);
        const int tile_w = std::max(1, background_texture_.width / 2);
        const int tile_h = std::max(1, background_texture_.height / 2);
        for (int y = 0; y < height; y += tile_h) {
            for (int x = 0; x < width; x += tile_w) {
                const SDL_Rect destination{x, y, tile_w, tile_h};
                SDL_RenderCopy(renderer, background_texture_.texture.get(), nullptr, &destination);
            }
        }
    }

    const SDL_Rect exit_button{50, 12, 76, 76};
    fillRound(renderer, exit_button, 12, carousel_style_.viewport_color);
    if (exit_texture_.texture) {
        SDL_SetTextureColorMod(exit_texture_.texture.get(), 18, 74, 119);
        SDL_SetTextureAlphaMod(exit_texture_.texture.get(), 255);
        const SDL_Rect icon{exit_button.x + 10, exit_button.y + 10,
            exit_button.w - 20, exit_button.h - 20};
        SDL_RenderCopy(renderer, exit_texture_.texture.get(), nullptr, &icon);
    }

    const SDL_Rect tool{142, 12, 240, 76};
    fillRound(renderer, tool, 12, carousel_style_.viewport_color);
    const int frame_size = 70;
    const SDL_Rect frame{tool.x + (tool.w - frame_size) / 2,
        tool.y + (tool.h - frame_size) / 2, frame_size, frame_size};
    fillRound(renderer, frame, 12, carousel_style_.frame_basic);
    fillRound(renderer, {frame.x + 7, frame.y + 7, frame.w - 14, frame.h - 14},
        7, carousel_style_.viewport_color);
    if (basic_tool_texture_.texture) {
        SDL_SetTextureColorMod(basic_tool_texture_.texture.get(), 18, 74, 119);
        SDL_SetTextureAlphaMod(basic_tool_texture_.texture.get(), 255);
        const SDL_Rect icon{tool.x + (tool.w - 62) / 2, tool.y + (tool.h - 62) / 2, 62, 62};
        SDL_RenderCopy(renderer, basic_tool_texture_.texture.get(), nullptr, &icon);
    }
}

void AquariumStockingOverlay::renderInfoBanner(
    SDL_Renderer* renderer,
    int width,
    int height,
    const AquariumSpeciesCatalog&,
    const AquariumStockingController& controller) const {
    const int separator_height = std::max(4, info_style_.separator_height);
    const int banner_height = std::max(110, info_style_.info_height);
    const int y = height - banner_height - separator_height;
    fill(renderer, {0, y, width, separator_height}, info_style_.separator_color);
    fill(renderer, {0, y + separator_height, width, banner_height},
        info_style_.info_background_color);

    if (controller.tab() == AquariumStockingController::Tab::Exhibit) {
        std::string title;
        std::string description;
        std::array<float, 4> chip_color{0.16f, 0.58f, 0.86f, 1.0f};
        switch (controller.focusedExhibitControl()) {
        case AquariumStockingController::ExhibitControl::Color: {
            const auto& preset = kAquariumExhibitPresets[
                std::min(controller.focusedExhibitPresetIndex(),
                    kAquariumExhibitPresets.size() - 1U)];
            title = std::string(preset.display_name);
            description = "WATER COLOR";
            chip_color = preset.water_surface;
            break;
        }
        case AquariumStockingController::ExhibitControl::Brightness:
            title = "TANK BRIGHTNESS";
            description = "DIM                         BRIGHT";
            break;
        case AquariumStockingController::ExhibitControl::Murkiness:
            title = "WATER MURKINESS";
            description = "CLEAR                         DENSE";
            chip_color = {0.10f, 0.24f, 0.30f, 1.0f};
            break;
        case AquariumStockingController::ExhibitControl::Substrate: {
            const auto& substrate = kAquariumSubstratePresets[
                std::min(controller.focusedSubstrateIndex(),
                    kAquariumSubstratePresets.size() - 1U)];
            title = std::string(substrate.display_name);
            description = "BLACK 2 FLOOR";
            chip_color = {
                substrate.swatch_base[0] / 255.0f,
                substrate.swatch_base[1] / 255.0f,
                substrate.swatch_base[2] / 255.0f, 1.0f};
            break;
        }
        }
        const TextureHandle name = textTexture(renderer, summary_font_.get(),
            title, {18, 74, 119, 255});
        const TextureHandle detail = textTexture(renderer, summary_small_font_.get(),
            description, {47, 91, 118, 255});
        if (name.texture) {
            const SDL_Rect destination{
                78, y + separator_height + 8, name.width, name.height};
            SDL_RenderCopy(renderer, name.texture.get(), nullptr, &destination);
        }
        if (detail.texture) {
            const SDL_Rect destination{
                78, y + separator_height + 58, detail.width, detail.height};
            SDL_RenderCopy(renderer, detail.texture.get(), nullptr, &destination);
        }
        const SDL_Rect water_chip{18, y + separator_height + 18, 42, 72};
        fillRound(renderer, water_chip, 10, {
            static_cast<Uint8>(chip_color[0] * 255.0f),
            static_cast<Uint8>(chip_color[1] * 255.0f),
            static_cast<Uint8>(chip_color[2] * 255.0f), 255});
        return;
    }

    const AquariumSpeciesEntry* species = controller.heldSpecies();
    if (!species) species = controller.focusedSpecies();
    if (!species || controller.tab() == AquariumStockingController::Tab::Exhibit) return;

    if (sprite_assets_) {
        PokemonSpriteRequest request;
        request.species_id = species->dex;
        request.species_slug = species->species;
        request.form_key = species->form;
        const TextureHandle sprite = sprite_assets_->loadPokemonTexture(renderer, request);
        if (sprite.texture) {
            SDL_SetTextureColorMod(sprite.texture.get(), 255, 255, 255);
            SDL_SetTextureAlphaMod(sprite.texture.get(), 255);
            const SDL_Rect destination{18, y + separator_height + 8, 88, 88};
            SDL_RenderCopy(renderer, sprite.texture.get(), nullptr, &destination);
        }
    }

    const TextureHandle name = textTexture(renderer, summary_font_.get(),
        species->display_name.empty() ? species->species : species->display_name,
        {18, 74, 119, 255});
    const TextureHandle profile = textTexture(renderer, summary_small_font_.get(),
        species->movement_profile + "  /  " + species->vertical_zone,
        {47, 91, 118, 255});
    if (name.texture) {
        const SDL_Rect destination{122, y + separator_height + 8, name.width, name.height};
        SDL_RenderCopy(renderer, name.texture.get(), nullptr, &destination);
    }
    if (profile.texture) {
        const SDL_Rect destination{122, y + separator_height + 58,
            profile.width, profile.height};
        SDL_RenderCopy(renderer, profile.texture.get(), nullptr, &destination);
    }

    int mask_width = 0;
    for (const auto& row : species->capacity_mask) {
        mask_width = std::max(mask_width, static_cast<int>(row.size()));
    }
    const int mask_height = static_cast<int>(species->capacity_mask.size());
    constexpr int cell = 18;
    const int origin_x = width - 38 - mask_width * (cell + 3);
    const int origin_y = y + separator_height +
        std::max(8, (banner_height - mask_height * (cell + 3)) / 2);
    for (int row = 0; row < mask_height; ++row) {
        for (int column = 0; column < static_cast<int>(species->capacity_mask[row].size()); ++column) {
            if (species->capacity_mask[row][column] != '1') continue;
            fillRound(renderer,
                {origin_x + column * (cell + 3), origin_y + row * (cell + 3), cell, cell},
                4, {33, 133, 190, 255});
        }
    }
}

void AquariumStockingOverlay::renderHeldSpecies(
    SDL_Renderer* renderer,
    int width,
    int height,
    const AquariumSpeciesCatalog&,
    const AquariumStockingController& controller) const {
    const AquariumSpeciesEntry* species = controller.heldSpecies();
    if (!species || !sprite_assets_) return;
    PokemonSpriteRequest request;
    request.species_id = species->dex;
    request.species_slug = species->species;
    request.form_key = species->form;
    const TextureHandle sprite = sprite_assets_->loadPokemonTexture(renderer, request);
    if (!sprite.texture) return;

    int center_x = controller.pointerX();
    int center_y = controller.pointerY() + box_style_.sprite_offset_y;
    if (!controller.pointerActive()) {
        if (controller.focusArea() == AquariumStockingController::FocusArea::Tank) {
            center_x = width - 40 - BoxViewport::kViewportWidth / 2;
            center_y = 385;
        } else {
            const SDL_Rect slot = catalogueSlotRect(controller.focusedIndex() - controller.pageStart());
            center_x = slot.x + slot.w / 2;
            center_y = slot.y + slot.h / 2 + box_style_.sprite_offset_y;
        }
    }
    center_x = std::clamp(center_x, 32, width - 32);
    center_y = std::clamp(center_y, 32, height - 128);
    const double scale = std::clamp(box_style_.sprite_scale, 0.5, 4.0);
    const int sprite_width = std::max(1, static_cast<int>(std::lround(sprite.width * scale)));
    const int sprite_height = std::max(1, static_cast<int>(std::lround(sprite.height * scale)));
    const SDL_Rect destination{center_x - sprite_width / 2,
        center_y - sprite_height / 2, sprite_width, sprite_height};
    SDL_SetTextureColorMod(sprite.texture.get(), 0, 0, 0);
    SDL_SetTextureAlphaMod(sprite.texture.get(), 85);
    const SDL_Rect shadow{destination.x, destination.y + 18,
        destination.w, destination.h};
    SDL_RenderCopy(renderer, sprite.texture.get(), nullptr, &shadow);
    SDL_SetTextureColorMod(sprite.texture.get(), 255, 255, 255);
    SDL_SetTextureAlphaMod(sprite.texture.get(), 255);
    SDL_RenderCopy(renderer, sprite.texture.get(), nullptr, &destination);
}

} // namespace pr::gameplay::world3d::aquarium::construction
