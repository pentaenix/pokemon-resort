# Runtime Structure

- `src/core/App.cpp`
  Owns the SDL application loop, renderer setup, audio sync, and event dispatch.

- `src/core/InputBindings.cpp`
  Converts config-defined key names into SDL keycodes and matches runtime input against configured bindings.

- `src/core/SaveDataStore.cpp`
  Loads and saves the versioned `.sav` container, with primary and backup files for recovery.

- `src/ui/TitleScreen.cpp`
  Acts as the current front-end scene controller. It owns title flow, menu/options navigation, and placeholder section screens.

# Config Strategy

- `config/title_screen.json`
  Holds authoring defaults for transitions, input bindings, audio, save naming, menu layout, and visual effects.

- `pokemon_resort.sav`
  Lives in the platform's writable app-data directory and stores player data such as options.

- `pokemon_resort.sav.bak`
  Mirrors the primary save so the app can recover if the main save is interrupted or corrupted.

- `include/core/Types.hpp`
  Mirrors the JSON structure in strongly typed C++ config structs.

# Future Direction

- If section screens keep growing, split them out of `TitleScreen.cpp` into separate scene/controller files.
- Keep `App.cpp` as a thin coordinator and move input/render/state logic into scene-local modules.
