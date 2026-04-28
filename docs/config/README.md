# Config Guide

This directory documents JSON authoring surfaces for Pokemon Resort. Config owns values that designers or contributors should tune without changing runtime logic: layout, timing, text, colors, asset paths, icon placement, animation constants, input bindings, and audio defaults.

Runtime code owns state transitions, input semantics, parsing, persistence, bridge/cache behavior, and data mutation. Persisted save/profile data owns player state that must survive app restarts.

## Config Files

- [`app.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/app.json)
  Shared app config: desktop window size, logical/design resolution, app title, input bindings, audio assets, and default audio volumes. Parsed by `ConfigLoader.cpp` into `AppConfig`.
- [`title_screen.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/title_screen.json)
  Title/menu/options authoring: intro timings, logo/background assets, prompt/menu/options text, skip flags, save identity (`persistence`: SDL organization/application, primary/backup JSON save file names, and **`resort_profile_file_name`** for the SQLite Resort DB next to those files), and title-specific visual tuning. Parsed by `ConfigLoader.cpp` into `TitleScreenConfig`.
- [`loading_screen.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/loading_screen.json)
  Transfer loading screen ball directory, position, scale, text, and spin timing. Read by `LoadingScreen`.
- [`transfer_select_save.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/transfer_select_save.json)
  Transfer-ticket screen header, list viewport, ticket art/text layout, rip animation, transfer lobby audio, background animation, and game color palette. See [`transfer_select_save.md`](transfer_select_save.md).
- [`game_transfer.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/game_transfer.json)
  Post-ticket transfer system layout and tuning. See [`game_transfer.md`](game_transfer.md).

## Adding Or Moving Config

Use this checklist:

1. Confirm the value is authored/tuneable rather than a state-machine rule.
2. Add the JSON field with a clear default.
3. Parse it in the owning config loader, not ad hoc inside render/input code.
4. Add or update a focused config parsing test.
5. If the field affects player-visible flow, add or update the relevant controller or harness test.
6. Update this guide or the specific config reference when the field creates a new authoring concept.

## Naming

- Use `Pokemon` in file names and code-facing docs unless quoting player-facing text that intentionally uses `Pokémon`.
- Use `PokeSprite` for the C++ subsystem and `pokesprite` for the asset directory/vendor-style data.
- Use `transfer ticket` for the save-selection ticket UI.
- Use `Transfer Select Save` for the whole ticket-selection screen/config surface when a proper title is needed.
- Use `Box Space` for the transfer-system multi-box overview.
