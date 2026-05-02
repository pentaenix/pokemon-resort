# Pokemon Resort

Pokemon Resort is a macOS-oriented SDL2 prototype for a Pokemon storage and transfer experience. It now includes a title/menu flow, options persistence, transfer save scanning, transfer-ticket selection, a post-ticket transfer system screen, PokeSprite-backed Pokemon/item rendering, a process-based PKHeX bridge, and a SQLite-backed Resort storage foundation.

The current codebase is no longer just a title-screen demo. Treat this README as the orientation page, then follow the deeper docs for architecture, config, bridge, backend, assets, and testing.

## Start Here

- [`docs/ARCHITECTURE.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/ARCHITECTURE.md) is the central architecture map.
- [`../tests/README.md`](/Users/vanta/Desktop/title_screen_demo/tests/README.md) is the canonical testing map.
- [`docs/config/README.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/config/README.md) explains which JSON files own which UI surfaces.
- [`docs/transfer_system/README.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/transfer_system/README.md) is the practical field guide for transfer-system changes.
- [`docs/PKHEX_BRIDGE.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/PKHEX_BRIDGE.md) is the canonical PKHeX bridge contract.
- [`docs/assets/pokesprite_subsystem.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/assets/pokesprite_subsystem.md) documents Pokemon, item, and misc icon asset resolution.
- [`docs/backend/README.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/backend/README.md) documents canonical Resort storage, import, export, and backend-facing services.

## Current Runtime Flows

- title intro, title hold, main menu, options menu, and Resort/Trade loading-transition test flows
- transfer entry from the main menu
- loading screen while save files are scanned and probed
- transfer-ticket list built from `SaveLibrary` summaries
- selected-save deep probe into the transfer system screen
- transfer-system box/grid UI, Box Space view, tool carousel, info banner, Pokemon action menu, item action menu, and temporary in-memory Pokemon/item movement
- persisted title/options settings through `SaveDataStore`
- canonical Resort backend services opened by the app, with UI replacement still deferred

## Build On macOS

Run commands from the project root:

```bash
cd /Users/vanta/Desktop/title_screen_demo/pokemon-resort
```

Install dependencies:

```bash
brew install sdl2 sdl2_image sdl2_ttf
```

Build and run:

```bash
cmake -S . -B build
cmake --build build
./build/title_screen_demo
```

Run with a custom title config path:

```bash
./build/title_screen_demo /absolute/path/to/title_screen.json
```

Clear the transfer save-probe cache and exit:

```bash
./build/title_screen_demo --clear-save-cache
```

## Subsystem Navigation

Use this map before changing code:

- **App orchestration:** `src/core/App.cpp` owns SDL setup, app loop, active flow selection, persistence wiring, audio wiring, and high-level transfer lifecycle.
- **Title/menu/options:** `src/ui/TitleScreen.cpp` owns title flow coordination. Extracted title collaborators live under `src/ui/title_screen/`.
- **Loading screens:** `src/ui/loading/` owns reusable loading screens, including the black Pokeball loader and the Resort transfer boat loader. See `docs/loading/README.md` for call patterns and message keys.
- **Transfer flow shell:** `src/ui/TransferFlowCoordinator.cpp` owns async save scanning/deep probing and concrete screen transitions. Pure flow decisions live under `src/ui/transfer_flow/`.
- **Transfer ticket list:** `src/ui/TransferTicketScreen.cpp` renders tickets. List behavior lives in `src/ui/transfer_ticket/TransferTicketListController.cpp`.
- **Transfer system screen:** `src/ui/TransferSystemScreen.cpp` adapts real SDL input/render state to smaller controllers. Pure controllers and render helpers live under `src/ui/transfer_system/`; read [`docs/transfer_system/README.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/transfer_system/README.md) before changing this area.
- **Input:** `src/core/InputRouter.cpp`, `src/core/InputBindings.cpp`, and `include/ui/ScreenInput.hpp` keep keyboard/controller/mouse routing centralized.
- **Save scanning and bridge summaries:** `src/core/SaveLibrary.cpp` owns save discovery, bridge probing, cache behavior, and parsed transfer models.
- **PKHeX bridge boundary:** `src/core/SaveBridgeClient.cpp` launches the .NET helper in `../tools/pkhex_bridge`; native C++ should not link `PKHeX.Core`.
- **PokeSprite assets:** `src/core/PokeSpriteAssets.cpp` owns Pokemon, item, and misc icon path resolution plus texture caching.
- **Resort backend:** `src/resort/` and `include/resort/` own canonical Pokemon storage, import/export services, repositories, and SQLite persistence.

## Config Sources Of Truth

Prefer JSON when changing authored layout, timing, text, colors, asset paths, or tuneable UI behavior. Prefer runtime code when changing state transitions, input semantics, persistence, parsing, or controller behavior. Prefer persisted data only when the player/profile state itself must survive app restarts.

Important config files:

- [`config/app.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/app.json): window/logical size, app title, shared input bindings, shared audio assets/default volume.
- [`config/title_screen.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/title_screen.json): title/menu/options authoring, intro timings, prompt/menu labels, save identity, skip flags, logo shine.
- [`config/loading_screen.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/loading_screen.json): transfer loading animation.
- [`config/transfer_select_save.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/transfer_select_save.json): transfer-ticket screen layout, ticket art/text, rip animation, palette.
- [`config/game_transfer.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/game_transfer.json): transfer-system layout, box viewport tuning, Box Space timing, tool carousel, action menus, item tool visuals, info banner, mini preview, dropdown, selection cursor, and speech bubbles.

See [`docs/config/README.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/config/README.md) and [`docs/config/game_transfer.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/config/game_transfer.md) before adding new transfer UI config.

## PKHeX Bridge

The PKHeX integration is intentionally process-based. The native app calls a .NET helper under [`../tools/pkhex_bridge`](/Users/vanta/Desktop/title_screen_demo/tools/pkhex_bridge) through `SaveBridgeClient`.

For development:

```bash
cd /Users/vanta/Desktop/title_screen_demo/tools/pkhex_bridge
dotnet restore
dotnet build
dotnet run --project PKHeXBridge.csproj -- "/absolute/path/to/save.sav"
```

For shipping-style bundling, publish a self-contained helper:

```bash
cd /Users/vanta/Desktop/title_screen_demo/pokemon-resort
cmake --build build --target pkhex_bridge_publish
```

The full launch order, JSON contract, import-grade model, and guarded write-projection behavior are documented in [`docs/PKHEX_BRIDGE.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/PKHEX_BRIDGE.md). Keep that file canonical instead of copying bridge details into new docs.

## Testing

The canonical test guide is [`../tests/README.md`](/Users/vanta/Desktop/title_screen_demo/tests/README.md).

Before finishing behavior or architecture changes, run the relevant focused tests and then the native suite:

```bash
cd /Users/vanta/Desktop/title_screen_demo/pokemon-resort
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Run bridge unit/integration/e2e tests when touching `tools/pkhex_bridge`, save probing, import-grade JSON, write-projection validation, bridge-backed native import, or bridge output consumed by `SaveLibrary`.

## Contributor Rules Of Thumb

- Keep `App.cpp` orchestration-focused.
- Keep PKHeX behind the external bridge.
- Keep bridge parsing and cache behavior in `SaveLibrary`, not UI screens.
- Keep transfer-system navigation in focus/flow/controller seams instead of adding long-lived state bags to `TransferSystemScreen.cpp`.
- Keep visual tuning in config and parsing in the relevant config loader.
- Keep UI screens consuming read models; do not put SQL, bridge JSON parsing, or Resort import/export rules in render code.
- Update docs when module boundaries, config contracts, test coverage, or bridge output change.
