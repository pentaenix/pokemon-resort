#pragma once

#include "core/Types.hpp"
#include "core/assets/Font.hpp"

#include <SDL.h>

#include <string>

namespace pr {
struct PcSlotSpecies;
}

namespace pr::transfer_system::summary {

struct PokemonSummaryContentModel {
    bool has_data = false;
    std::string pokemon_name;
};

PokemonSummaryContentModel buildPokemonSummaryContentModel(const PcSlotSpecies* pokemon);

void drawTemporaryPokemonSummaryContent(
    SDL_Renderer* renderer,
    TTF_Font* font,
    const SDL_Rect& panel_rect,
    const PokemonSummaryContentModel& model,
    const Color& text_color);

} // namespace pr::transfer_system::summary
