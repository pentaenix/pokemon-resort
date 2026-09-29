# Config Guide

This directory documents JSON authoring surfaces for Pokemon Resort. Config owns values that designers or contributors should tune without changing runtime logic: layout, timing, text, colors, asset paths, icon placement, animation constants, input bindings, and audio defaults.

Runtime code owns state transitions, input semantics, parsing, persistence, bridge/cache behavior, and data mutation. Persisted save/profile data owns player state that must survive app restarts.

## Config Files

- [`app.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/app.json)
  Shared app config: desktop window size, logical/design resolution, app title, input bindings, audio assets (including `aquarium_music`), and default audio volumes. `input.world_build_mode_keys` owns the outdoor build-mode toggle (`N` by default); `N` is intentionally not a Back binding. Aquarium music uses the same persisted Music Volume option as other music. Parsed by `ConfigLoader.cpp` into `AppConfig`.
- [`title_screen.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/title_screen.json)
  Title/menu/options authoring: intro timings, logo/background assets, prompt/menu/options text, skip flags, save identity (`persistence`: SDL organization/application, primary/backup JSON save file names, and **`resort_profile_file_name`** for the SQLite Resort DB next to those files), and title-specific visual tuning. Parsed by `ConfigLoader.cpp` into `TitleScreenConfig`.
- [`loading_screen.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/loading_screen.json)
  Transfer loading screen ball directory, position, scale, text, spin timing, and the Resort transfer loading animation/message catalog. Read by `PokeballLoadingScreen` and `ResortTransferLoadingScreen`; see [`docs/loading/README.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/loading/README.md) for call patterns.
- [`transfer_select_save.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/transfer_select_save.json)
  Transfer-ticket screen header, list viewport, ticket art/text layout, rip animation, transfer lobby audio, background animation, and game color palette. See [`transfer_select_save.md`](transfer_select_save.md).
- [`game_transfer.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/game_transfer.json)
  Post-ticket transfer system layout and tuning. See [`game_transfer.md`](game_transfer.md).
- [`transfer_save.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/transfer_save.json)
  Transfer system save/exit UX tuning (e.g. exit-save modal styling and animation).
- [`pokemon_summary.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/pokemon_summary.json)
  Transfer-system Pokemon Summary panel shell and future Summary content authoring. See [`pokemon_summary.md`](pokemon_summary.md).
- [`gameplay/world3d/aquarium_room_decorations.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/gameplay/world3d/aquarium_room_decorations.json)
  Asset-owned room-decoration categories and collision profiles. Assets are estimated into `structures`, `furniture`, `nature`, and `equipment` tabs from their names; an entry can override that estimate. Unlisted models use their conservative measured bounds. Use `cell-mask` for gates and irregular props that should block only selected local grid cells, or `none` for non-solid visual props. Mask cells are relative to the placement cell at zero rotation and rotate with the decoration; placed-room save documents continue to store only the asset ID and transform.
- [`gameplay/world3d/world_decorations.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/gameplay/world3d/world_decorations.json)
  Outdoor build-mode asset catalog. Map editability remains a map-project policy (`editPolicy: "editable_land"`); authored `.owmap` files are never rewritten by play. Confirmed player placements are profile data under the normal save directory. In build mode, Tab opens or closes this catalog; it is intentionally closed on entry so the map remains visible.

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
