# Title Screen Demo

A small SDL2 title-screen project for macOS that is driven by `config/title_screen.json`.

## Expected asset layout

```text
assets/
  title/
    background_a.png
    background_b.png
    logo_splash.png
    logo_main.png
  fonts/
    Arial.ttf    # optional but recommended
config/
  title_screen.json
```

## Sequence implemented

- black background + `logo_splash.png` fades in and out
- `logo_main.png` appears over black
- quick white flash
- `logo_main.png` over `background_a.png` with `background_b.png` underneath
- after a delay, blinking `PRESS START`
- on input, `logo_main.png` and `background_a.png` move upward, with the logo moving slightly faster
- `background_b.png` is revealed underneath

## Build on macOS

Install dependencies:

```bash
brew install sdl2 sdl2_image sdl2_ttf
```

Build:

```bash
cmake -S . -B build
cmake --build build
./build/title_screen_demo
```

Run with a custom config path:

```bash
./build/title_screen_demo /absolute/path/to/title_screen.json
```

## Changing how far the main logo moves

The main control is in `config/title_screen.json`:

```json
"transition": {
  "main_logo_end_y": -260,
  "background_a_end_y": -768,
  "logo_speed_scale": 1.20
}
```

If your logo should move fully off-screen, make `main_logo_end_y` more negative.

Examples:

- `-260` = partly off-screen for a medium logo
- `-400` = higher
- `-600` = almost certainly fully gone

A good rule is:

```text
main_logo_end_y < -(logo_height / 2)
```

So if the logo is 300 px tall, any value less than `-150` places its center high enough for the whole image to leave the window.

## What is data-driven

Editable in JSON:

- window size and title
- asset paths
- all major timings
- prompt text, color, size, position
- splash and main logo positions
- background positions
- logo/background transition destinations
- logo speed factor during transition
- input toggles

## Code layout

```text
src/
  main.cpp
  core/
    App.cpp
    Assets.cpp
    Font.cpp
    Json.cpp
    ConfigLoader.cpp
  ui/
    TitleScreen.cpp
include/
  core/
    App.hpp
    Assets.hpp
    ConfigLoader.hpp
    Font.hpp
    Json.hpp
    Types.hpp
  ui/
    TitleScreen.hpp
```

## Notes

- Logos are rendered at their real texture size and centered.
- Backgrounds are rendered at their real texture size using top-left placement.
- For shipping, bundling a font is better than relying on macOS system fonts.
