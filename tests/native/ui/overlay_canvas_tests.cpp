#include "ui/overlay/OverlayCanvas.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

} // namespace

int main() {
    pr::OverlayCanvas canvas(1280, 800);
    pr::OverlayButton weather;
    weather.id = "weather";
    weather.label = "WEATHER: Clear";
    weather.anchor = pr::OverlayAnchor::TopRight;
    weather.style.width = 190;
    weather.style.height = 40;
    weather.style.margin_x = 24;
    weather.style.margin_y = 22;

    const SDL_Rect top_right = canvas.buttonRect(weather);
    expect(top_right.x == 1066, "top-right overlay button should use logical canvas width");
    expect(top_right.y == 22, "top-right overlay button should use top margin");
    expect(canvas.hitButton(weather, 1100, 40), "top-right overlay button should hit inside logical rect");
    expect(!canvas.hitButton(weather, 900, 40), "top-right overlay button should reject outside logical rect");

    canvas.setLogicalSize(640, 400);
    const SDL_Rect compact = canvas.buttonRect(weather);
    expect(compact.x == 426, "top-right overlay button should adapt to new logical width");
    expect(compact.y == 22, "top-right overlay button y should stay stable after resize");
    expect(canvas.hitButton(weather, 500, 40), "resized overlay button should hit inside compact rect");
    expect(!canvas.hitButton(weather, 300, 40), "resized overlay button should reject outside compact rect");

    pr::OverlayButton view = weather;
    view.id = "view";
    view.label = "VIEW: FULL";
    view.style.margin_y = 74;
    const SDL_Rect view_rect = canvas.buttonRect(view);
    expect(view_rect.x == compact.x, "stacked top-right overlay button should align with weather button");
    expect(view_rect.y > compact.y + compact.h, "stacked top-right overlay button should sit below weather button");
    expect(canvas.hitButton(view, view_rect.x + 10, view_rect.y + 10),
           "stacked top-right overlay button should hit inside its own rect");

    pr::OverlayButton pokemon = weather;
    pokemon.id = "pokemon";
    pokemon.label = "POKEMON: Reshiram";
    pokemon.style.width = 232;
    pokemon.style.margin_y = 126;
    const SDL_Rect pokemon_rect = canvas.buttonRect(pokemon);
    expect(pokemon_rect.x < compact.x, "wider stacked top-right overlay button should remain right-aligned");
    expect(pokemon_rect.y > view_rect.y + view_rect.h,
           "third stacked top-right overlay button should sit below view button");

    pr::OverlayButton bottom_left = weather;
    bottom_left.anchor = pr::OverlayAnchor::BottomLeft;
    const SDL_Rect bottom = canvas.buttonRect(bottom_left);
    expect(bottom.x == 24, "bottom-left overlay button should use left margin");
    expect(bottom.y == 338, "bottom-left overlay button should use logical height and bottom margin");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
