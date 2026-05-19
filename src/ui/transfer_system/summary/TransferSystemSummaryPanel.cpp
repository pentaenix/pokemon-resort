#include "ui/TransferSystemScreen.hpp"

#include "ui/transfer_system/summary/PokemonSummaryContent.hpp"

#include <algorithm>
#include <cmath>

namespace pr {
namespace {

double easeOutCubic(double t) {
    t = std::clamp(t, 0.0, 1.0);
    const double inv = 1.0 - t;
    return 1.0 - inv * inv * inv;
}

bool pointInRect(int x, int y, const SDL_Rect& r) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void approachExponential(double& value, double target, double dt, double lambda) {
    if (lambda <= 1e-9) {
        value = target;
        return;
    }
    const double alpha = 1.0 - std::exp(-lambda * std::max(0.0, dt));
    value += (target - value) * std::clamp(alpha, 0.0, 1.0);
    if (std::fabs(target - value) < 0.0005) {
        value = target;
    }
}

void fillOneSidedRoundedRect(
    SDL_Renderer* renderer,
    const SDL_Rect& rect,
    int radius,
    const Color& color,
    bool panel_on_left) {
    if (!renderer || rect.w <= 0 || rect.h <= 0) {
        return;
    }
    radius = std::clamp(radius, 0, std::min(rect.w, rect.h) / 2);
    SDL_SetRenderDrawColor(
        renderer,
        static_cast<Uint8>(color.r),
        static_cast<Uint8>(color.g),
        static_cast<Uint8>(color.b),
        static_cast<Uint8>(color.a));

    for (int yy = 0; yy < rect.h; ++yy) {
        int x0 = rect.x;
        int x1 = rect.x + rect.w - 1;
        if (radius > 0 && (yy < radius || yy >= rect.h - radius)) {
            const int cy = yy < radius ? radius : rect.h - radius - 1;
            const int dy = yy - cy;
            const int inset = radius - static_cast<int>(std::sqrt(std::max(0, radius * radius - dy * dy)));
            if (panel_on_left) {
                x1 -= inset;
            } else {
                x0 += inset;
            }
        }
        SDL_RenderDrawLine(renderer, x0, rect.y + yy, x1, rect.y + yy);
    }
}

} // namespace

void TransferSystemScreen::openPokemonSummaryPanel(
    transfer_system::PokemonMoveController::Panel source_panel,
    int box_index,
    int slot_index) {
    if (!pokemon_summary_style_.enabled) {
        return;
    }
    const PokemonSummarySide next_side =
        source_panel == transfer_system::PokemonMoveController::Panel::Game
            ? PokemonSummarySide::Left
            : PokemonSummarySide::Right;
    if (pokemon_summary_target_open_ && next_side != pokemon_summary_side_) {
        pokemon_summary_reveal_ = 0.0;
    }
    pokemon_summary_source_panel_ = source_panel;
    pokemon_summary_box_index_ = box_index;
    pokemon_summary_slot_index_ = slot_index;
    pokemon_summary_side_ = next_side;
    pokemon_summary_target_open_ = true;
    closePokemonActionMenu();
    closeGameBoxDropdown();
    closeResortBoxDropdown();
    ui_state_.requestButtonSfx();
}

void TransferSystemScreen::closePokemonSummaryPanel(bool play_sfx) {
    if (!pokemon_summary_target_open_ && pokemon_summary_reveal_ <= 1e-3) {
        return;
    }
    pokemon_summary_target_open_ = false;
    if (play_sfx) {
        ui_state_.requestButtonSfx();
    }
}

void TransferSystemScreen::updatePokemonSummaryPanel(double dt) {
    const double target = pokemon_summary_target_open_ ? 1.0 : 0.0;
    const bool retracting_box =
        (pokemon_summary_target_open_ && pokemon_summary_reveal_ < 0.995) ||
        (!pokemon_summary_target_open_ && pokemon_summary_reveal_ > 0.005);
    const double smoothing = retracting_box
        ? pokemon_summary_style_.retracted_box_smoothing
        : (pokemon_summary_target_open_ ? pokemon_summary_style_.enter_smoothing : pokemon_summary_style_.exit_smoothing);
    approachExponential(pokemon_summary_reveal_, target, dt, std::max(1.0, smoothing));
    if (!pokemon_summary_target_open_ && pokemon_summary_reveal_ <= 1e-3) {
        pokemon_summary_reveal_ = 0.0;
        pokemon_summary_box_index_ = -1;
        pokemon_summary_slot_index_ = -1;
    }
}

bool TransferSystemScreen::pokemonSummaryPanelVisible() const {
    return pokemon_summary_style_.enabled && pokemon_summary_reveal_ > 1e-3;
}

bool TransferSystemScreen::pokemonSummaryPanelOpenOnLeft() const {
    return pokemon_summary_side_ == PokemonSummarySide::Left;
}

SDL_Rect TransferSystemScreen::pokemonSummaryPanelRect() const {
    const int w = std::max(1, pokemon_summary_style_.width);
    const int h = std::max(1, pokemon_summary_style_.height);
    const int y = pokemon_summary_style_.top_y;
    const int screen_w = window_config_.virtual_width;
    int hidden_x = -w;
    int shown_x = 0;
    if (pokemon_summary_side_ == PokemonSummarySide::Right) {
        hidden_x = screen_w;
        shown_x = screen_w - w;
    }
    const double eased = easeOutCubic(pokemon_summary_reveal_);
    const int x = static_cast<int>(std::lround(
        static_cast<double>(hidden_x) +
        (static_cast<double>(shown_x - hidden_x) * eased)));
    return SDL_Rect{x, y, w, h};
}

void TransferSystemScreen::drawPokemonSummaryPanel(SDL_Renderer* renderer) const {
    if (!pokemonSummaryPanelVisible()) {
        return;
    }
    const SDL_Rect r = pokemonSummaryPanelRect();
    const int border = std::clamp(pokemon_summary_style_.border_thickness, 0, 40);
    const int radius = std::max(0, pokemon_summary_style_.corner_radius);
    const bool panel_on_left = pokemon_summary_side_ == PokemonSummarySide::Left;
    if (border > 0) {
        fillOneSidedRoundedRect(renderer, r, radius, pokemon_summary_style_.border_color, panel_on_left);
        fillOneSidedRoundedRect(
            renderer,
            SDL_Rect{r.x + border, r.y + border, std::max(0, r.w - 2 * border), std::max(0, r.h - 2 * border)},
            std::max(0, radius - border),
            pokemon_summary_style_.fill_color,
            panel_on_left);
    } else {
        fillOneSidedRoundedRect(renderer, r, radius, pokemon_summary_style_.fill_color, panel_on_left);
    }
    const auto model = transfer_system::summary::buildPokemonSummaryContentModel(pokemonSummarySelectedPokemon());
    transfer_system::summary::drawTemporaryPokemonSummaryContent(
        renderer,
        pokemon_summary_font_.get(),
        r,
        model,
        pokemon_summary_style_.temporary_name_color);
}

bool TransferSystemScreen::handlePokemonSummaryPointerPressed(int logical_x, int logical_y) {
    if (!pokemonSummaryPanelVisible() && !pokemon_summary_target_open_) {
        return false;
    }

    if (pointInRect(logical_x, logical_y, pokemonSummaryPanelRect())) {
        // Reserved for the Summary screen's own buttons/controls as its content grows.
        return true;
    }

    if (const auto ref = slotRefAtPointer(logical_x, logical_y)) {
        if (const auto picked = focusNodeAtPointer(logical_x, logical_y)) {
            focus_.setCurrent(*picked);
            selection_cursor_hidden_after_mouse_ = true;
            speech_hover_active_ = true;
        }
        setPokemonSummarySelection(*ref);
        return true;
    }

    if (focusNodeAtPointer(logical_x, logical_y)) {
        closePokemonSummaryPanel(false);
        return false;
    }

    closePokemonSummaryPanel();
    return true;
}

void TransferSystemScreen::setPokemonSummarySelection(const transfer_system::PokemonMoveController::SlotRef& ref) {
    const PokemonSummarySide next_side =
        ref.panel == transfer_system::PokemonMoveController::Panel::Game
            ? PokemonSummarySide::Left
            : PokemonSummarySide::Right;
    if (pokemon_summary_target_open_ && next_side != pokemon_summary_side_) {
        pokemon_summary_reveal_ = 0.0;
    }
    pokemon_summary_source_panel_ = ref.panel;
    pokemon_summary_box_index_ = ref.box_index;
    pokemon_summary_slot_index_ = ref.slot_index;
    pokemon_summary_side_ = next_side;
    pokemon_summary_target_open_ = true;
}

void TransferSystemScreen::syncPokemonSummaryToFocus() {
    if (!pokemon_summary_target_open_ && !pokemonSummaryPanelVisible()) {
        return;
    }
    if (const auto ref = slotRefForFocus(focus_.current())) {
        setPokemonSummarySelection(*ref);
    }
}

const PcSlotSpecies* TransferSystemScreen::pokemonSummarySelectedPokemon() const {
    if (pokemon_summary_box_index_ < 0 || pokemon_summary_slot_index_ < 0) {
        return nullptr;
    }
    return pokemonAt(transfer_system::PokemonMoveController::SlotRef{
        pokemon_summary_source_panel_,
        pokemon_summary_box_index_,
        pokemon_summary_slot_index_});
}

} // namespace pr
