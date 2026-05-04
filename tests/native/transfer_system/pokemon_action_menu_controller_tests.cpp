#include "ui/transfer_system/PokemonActionMenuController.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct TestFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure(message);
    }
}

void testMenuPlacesAgainstSourceSideAndClampsToScreen() {
    pr::GameTransferPokemonActionMenuStyle style;
    style.width = 260;
    style.row_height = 52;
    style.padding_y = 12;
    style.gap_from_slot = 14;

    pr::transfer_system::PokemonActionMenuController menu;
    menu.open(true, 0, SDL_Rect{980, 120, 80, 80});
    const SDL_Rect game_rect = menu.finalRect(style, 1280, 800);
    expect(game_rect.x + game_rect.w <= 980, "game-side menu should be placed to the left of the source slot");

    menu.open(false, 0, SDL_Rect{20, 120, 80, 80});
    const SDL_Rect resort_rect = menu.finalRect(style, 1280, 800);
    expect(resort_rect.x >= 100, "resort-side menu should be placed to the right of the source slot");

    menu.open(true, 0, SDL_Rect{5, 5, 20, 20});
    const SDL_Rect clamped_rect = menu.finalRect(style, 320, 220);
    expect(clamped_rect.x >= 8 && clamped_rect.y >= 8, "menu should clamp to the visible screen margin");
}

void testMenuSelectionWrapsAndRowsHitOnlyWhenInteractive() {
    pr::GameTransferPokemonActionMenuStyle style;
    style.width = 260;
    style.row_height = 52;
    style.padding_y = 12;

    pr::transfer_system::PokemonActionMenuController menu;
    menu.open(true, 0, SDL_Rect{980, 120, 80, 80});
    const SDL_Rect rect = menu.finalRect(style, 1280, 800);
    expect(!menu.rowAtPoint(rect.x + 20, rect.y + 20, style, 1280, 800).has_value(),
           "menu rows should not accept hits before the opening animation is interactive");

    menu.update(1.0, style);
    menu.stepSelection(-1);
    expect(menu.selectedRow() == 4, "up from the first action should wrap to Cancel");
    menu.stepSelection(1);
    expect(menu.selectedRow() == 0, "down from Cancel should wrap to Move");

    const auto row = menu.rowAtPoint(rect.x + 20, rect.y + style.padding_y + style.row_height * 3 + 4, style, 1280, 800);
    expect(row.has_value() && *row == 3, "pointer hit testing should map to the correct action row");
    expect(menu.actionForRow(*row) == pr::transfer_system::PokemonActionMenuController::Action::Release,
           "row 3 should map to Release");
}

} // namespace

int main() {
    try {
        testMenuPlacesAgainstSourceSideAndClampsToScreen();
        testMenuSelectionWrapsAndRowsHitOnlyWhenInteractive();
        return EXIT_SUCCESS;
    } catch (const TestFailure& failure) {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return EXIT_FAILURE;
    } catch (const std::exception& ex) {
        std::cerr << "ERROR: unexpected exception: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
}
