#include "ui/transfer_system/summary/PokemonSummaryContent.hpp"

#include "core/assets/Assets.hpp"
#include "core/domain/PcSlotSpecies.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>

namespace pr::transfer_system::summary {

PokemonSummaryContentModel buildPokemonSummaryContentModel(const PcSlotSpecies* pokemon) {
    PokemonSummaryContentModel model;
    if (!pokemon || !pokemon->occupied()) {
        return model;
    }
    model.has_data = true;
    model.pokemon_name = !pokemon->nickname.empty() ? pokemon->nickname : pokemon->species_name;
    if (model.pokemon_name.empty()) {
        model.pokemon_name = pokemon->slug;
    }
    return model;
}

void drawTemporaryPokemonSummaryContent(
    SDL_Renderer* renderer,
    TTF_Font* font,
    const SDL_Rect& panel_rect,
    const PokemonSummaryContentModel& model,
    const Color& text_color) {
    if (!renderer || !font || !model.has_data || model.pokemon_name.empty()) {
        return;
    }

    // TEMPORARY SUMMARY CONTENT: this placeholder proves selection updates correctly.
    // Replace this file's draw path with the real Summary feature modules as they land.
    TextureHandle name = renderTextTexture(renderer, font, model.pokemon_name, text_color);
    if (!name.texture) {
        return;
    }
    int tw = 0;
    int th = 0;
    SDL_QueryTexture(name.texture.get(), nullptr, nullptr, &tw, &th);
    const int max_w = std::max(1, panel_rect.w - 80);
    const double scale = tw > max_w ? static_cast<double>(max_w) / static_cast<double>(tw) : 1.0;
    SDL_Rect dst{
        panel_rect.x + 40,
        panel_rect.y + 38,
        static_cast<int>(std::round(static_cast<double>(tw) * scale)),
        static_cast<int>(std::round(static_cast<double>(th) * scale))};
    SDL_RenderCopy(renderer, name.texture.get(), nullptr, &dst);
}

} // namespace pr::transfer_system::summary
