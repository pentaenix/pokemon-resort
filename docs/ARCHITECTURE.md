# Pokemon Resort Architecture

## Current State

This repository currently contains one playable SDL2 application in [`pokemon-resort`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort) plus a top-level [`saves`](/Users/vanta/Desktop/title_screen_demo/saves) folder with external `.sav` files that are not part of the app runtime.

The app is a data-driven title-screen prototype with these implemented flows:

- splash logo fade in, hold, and fade out
- main logo reveal and white flash
- title hold with blinking `PRESS START`
- transition into the main menu
- main menu navigation for `RESORT`, `TRANSFER`, `TRADE`, and `OPTIONS`
- options menu with persisted user settings
- placeholder section screen flow for `RESORT` and `TRADE`
- loading screen while transfer saves are scanned and probed
- transfer save-ticket selection flow into a post-ticket transfer UI shell

At the moment, most gameplay-facing behavior still lives in a single scene controller: [`TitleScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TitleScreen.cpp).

## Runtime Architecture

### Entry and boot

- [`main.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/main.cpp) forwards an optional config path into `pr::runApplication`.
  It also supports `--clear-save-cache` for deleting the transfer probe cache and exiting.
- [`App.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/App.cpp) is the runtime coordinator. It:
  - finds the project root by locating `config/app.json` and `config/title_screen.json`
  - loads shared app config and title-screen config into strongly typed structs
  - initializes SDL, SDL_image, and SDL_ttf
  - creates the window and renderer
  - loads textures, fonts, and text surfaces
  - restores saved user settings if persistence is enabled
  - owns the main event/update/render loop
  - starts and exits the transfer flow
  - bridges scene output to audio playback and save writes

### Core modules

- [`ConfigLoader.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/ConfigLoader.cpp)
  Deserializes `app.json` into shared [`AppConfig`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/core/Types.hpp) and `title_screen.json` into [`TitleScreenConfig`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/core/Types.hpp). `App.cpp` applies the app-level window, input, and audio config over the title config before building the runtime.

- [`Types.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/core/Types.hpp)
  Defines the config schema and persisted settings schema. This file is effectively the contract between JSON authoring, runtime behavior, and save serialization.

- [`Assets.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/Assets.cpp)
  Resolves asset paths relative to the project root, loads textures, renders text into textures, and builds the alpha mask used for the logo shine effect.

- [`PokeSpriteAssets.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/PokeSpriteAssets.cpp)
  Owns pokesprite metadata loading, Pokemon sprite resolution, item icon resolution, and SDL texture caching for the transfer UI. Party-ticket sprites and transfer box sprites should both go through this subsystem rather than building filenames in screen code. The asset-system contract is documented in [`docs/assets/pokesprite_subsystem.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/assets/pokesprite_subsystem.md).

- [`InputBindings.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/InputBindings.cpp)
  Maps human-readable keyboard binding names from JSON to SDL keycodes. Keep keyboard aliases here so config files and future control settings can stay human-readable instead of storing SDL numeric constants.

- [`InputRouter.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/InputRouter.cpp)
  Owns SDL input-event routing into the active [`ScreenInput`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/ui/ScreenInput.hpp): keyboard bindings, mouse forwarding, controller button mapping, and shared hold-to-repeat timing. `App.cpp` owns only app-lifecycle input such as `SDL_QUIT`; screen actions should go through `InputRouter` so keyboard/controller/mouse behavior stays consistent across screens. When adding a new global UI action, first add it to `InputConfig` / `app.json` if it should be remappable, then route it here into the `ScreenInput` vocabulary or a future typed input action. When adding new controller support, prefer extending the controller mapping in `InputRouter` rather than branching in individual screens.
  Tests that protect this contract live in [`input_router_tests.cpp`](/Users/vanta/Desktop/title_screen_demo/tests/native/core/input_router_tests.cpp), [`config_loader_tests.cpp`](/Users/vanta/Desktop/title_screen_demo/tests/native/core/config_loader_tests.cpp), and [`input_config_integration_tests.cpp`](/Users/vanta/Desktop/title_screen_demo/tests/native/core/input_config_integration_tests.cpp). If controls regress, these tests should point future agents toward config loading, binding aliases, or router dispatch rather than screen-specific code.

- [`ScreenInput.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/ui/ScreenInput.hpp)
  Defines the small reusable input surface for screens. It is the input vocabulary used by `InputRouter`: `canNavigate()`, `onNavigate(delta)`, `onAdvancePressed()`, `onBackPressed()`, 2D navigation, and pointer handlers. This keeps shared keyboard/controller repeat and pointer dispatch in one place instead of duplicating it per screen.

- [`Screen.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/ui/Screen.hpp)
  Extends `ScreenInput` with the common runtime surface for active UI screens: `update(dt)` and `render(renderer)`. Current concrete screens (`TitleScreen`, `LoadingScreen`, `TransferTicketScreen`, and `TransferSystemScreen`) implement this interface. `App.cpp` still owns explicit flow transitions and screen-specific side effects, but it can use `Screen` for active-screen input/render dispatch. New full-screen UI/gameplay pages should prefer `Screen` unless they are intentionally only an input helper.

- [`SaveDataStore.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/SaveDataStore.cpp)
  Loads from primary then backup save files, tolerates older flat save shapes, and writes atomically through a temporary file plus backup refresh.

- [`SaveBridgeClient.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/SaveBridgeClient.cpp)
  Launches the external .NET helper under [`tools/pkhex_bridge`](/Users/vanta/Desktop/title_screen_demo/tools/pkhex_bridge), captures stdout and stderr, and keeps the `PKHeX.Core` integration outside the native binary. It now prefers a bundled or published helper executable and falls back to development-time `dotnet run` only when needed.
  The bridge contract, save reader models, extension rules, and testing guidance are documented in [`PKHEX_BRIDGE.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/PKHEX_BRIDGE.md).

- [`SaveLibrary.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/SaveLibrary.cpp)
  Scans the top-level workspace [`saves`](/Users/vanta/Desktop/title_screen_demo/saves) folder without recursion, records file metadata, and probes each candidate through the bridge during startup. The ticket-list path still consumes a light transfer summary, but the deeper transfer probe now parses richer `PcSlotSpecies` models for both party and boxes from bridge `all_pokemon` / `boxes` data, including `form_key`, shiny, gender, moves, held item, and location. New box, bag, trainer, Pokedex, and Pokemon UI work should extend those parsed native models rather than re-reading raw bridge JSON.
  Transfer-select cache behavior is covered by [`save_library_cache_tests.cpp`](/Users/vanta/Desktop/title_screen_demo/tests/native/core/save_library_cache_tests.cpp). The tests use a fake bridge via `PKHEX_BRIDGE_EXECUTABLE` and protect cache generation, cache hits, hash misses, stale ticket summaries, known-bad cached game IDs, and the rule that menu cache does not supply transfer-system box detail payloads.

- [`include/resort`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/resort) and [`src/resort`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/resort)
  Define the native Pokemon Resort backend subsystem. This subsystem is storage-first and separate from UI controllers: domain models live under `domain`, SQLite connection/migrations/repositories under `persistence`, and orchestration/query/import/export boundaries under `services`. It introduces canonical Pokemon rows, independent box placement, raw snapshot storage, history events, mirror sessions, conservative matching/merge, and export projection. UI screens should consume service read models such as `PokemonSlotView` rather than owning SQL, canonical state, or import/export rules. Backend consumer docs live under [`docs/backend`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/backend/README.md).

- [`Audio.mm`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/Audio.mm)
  Provides the audio controller used by `App.cpp` for looped menu music and button sound effects.

### UI / scene layer

- [`TitleScreen.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/ui/TitleScreen.hpp)
  Defines the `TitleState` enum and the public scene API used by the app loop.
  One-shot title outputs are reported through `TitleScreenEvent` and drained with `consumeEvents()`. Keep new title-side effects in that event vocabulary instead of adding new boolean consume methods.
  Test-only read accessors are guarded by `PR_ENABLE_TEST_HOOKS` and are compiled only into harness targets such as `title_screen_flow_harness_tests`; do not enable that macro for the player executable.

- [`TitleScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TitleScreen.cpp)
  Contains the current state machine and almost all player-facing behavior:
  - timed state transitions
  - skip behavior for authoring and iteration
  - title-level input dispatch into menu/options/section actions
  - options interactions and pending save requests
  - section-screen entry/exit
  - high-level title/menu/options/section flow coordination

- [`src/ui/title_screen/MainMenuController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/title_screen/MainMenuController.cpp)
  Owns main-menu selection state, wraparound navigation, and the mapping from selected row to explicit menu action (`OpenResort`, `OpenTransfer`, `OpenTrade`, `OpenOptions`). `TitleScreen` consumes those actions, performs the scene transition, and emits one-shot output events for app-level effects such as button SFX or transfer entry. Keep future top-level menu behavior in this controller rather than reintroducing raw index routing in `TitleScreen`.

- [`src/ui/title_screen/OptionsMenuController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/title_screen/OptionsMenuController.cpp)
  Owns options-menu selection state, text-speed/music/SFX cycling, label generation, and conversion to/from persisted [`UserSettings`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/core/Types.hpp). `TitleScreen` consumes option actions (`ChangedSettings`, `CloseOptions`) and remains responsible for requesting save/audio side effects. Keep future options behavior here when it can be expressed without SDL rendering.

- [`src/ui/title_screen/SectionScreenController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/title_screen/SectionScreenController.cpp)
  Owns placeholder section identity for menu destinations such as `RESORT` and `TRADE`: queued/pending section, committed current section after fade, and current section title. `TitleScreen` still owns the surrounding transition state and rendering, but future placeholder section identity should grow here instead of adding more raw section fields to `TitleScreen`.

- [`src/ui/title_screen/TitleScreenRender.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/title_screen/TitleScreenRender.cpp)
  Owns title-screen rendering, button geometry, hit-test rectangles, cached option/section text textures, and logo shine generation. Rendering helpers still operate on `TitleScreen` state, but they are separated from the state-transition methods so rendering changes do not have to share one implementation file with flow logic.

- [`TransferFlowCoordinator.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TransferFlowCoordinator.cpp)
  Owns the transfer runtime shell after the title menu requests TRANSFER: it starts async save-ticket scanning and selected-save deep probes, owns the concrete SDL screen instances, and applies the pure flow/controller decisions to those runtime objects. `App.cpp` starts the flow and consumes high-level return/audio/SFX signals, but it should not own transfer futures or ticket/system transition details.

- [`src/ui/transfer_flow/TransferFlowController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_flow/TransferFlowController.cpp)
  Owns the pure transfer-flow state machine independent of SDL rendering and futures: active transfer sub-screen, current loading purpose, pending deep-probe selection, remembered last-viewed game box per game, return-to-title requests, and one-shot transfer-system entry requests. If a transfer-flow rule can be tested without SDL or PKHeX, it should usually live here rather than in `TransferFlowCoordinator`.

- [`src/ui/transfer_flow/TransferSelectionBuilder.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_flow/TransferSelectionBuilder.cpp)
  Owns conversion between `SaveLibrary` transfer summaries and the UI-facing [`TransferSaveSelection`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/ui/TransferSaveSelection.hpp) model, including game-title normalization and merging fresh deep-probe payloads into an existing selection. When a fresh probe refines a save’s `game_id`, this builder should also refresh the UI-facing game key/title so ticket text, footer icon callouts, and transfer-screen labels stay in sync. Keep `SaveFileRecord`/`TransferSaveSummary` to UI mapping here instead of spreading it through the coordinator or screens.

- [`TransferTicketScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TransferTicketScreen.cpp)
  Owns the transfer save-ticket rendering layer, texture/font caches, config-driven layout, and pointer-to-view adaptation. It renders directly in the shared `1280x800` logical/design coordinate system; there is no internal 512-to-1280 translation layer. It receives already parsed transfer summaries from `TransferFlowCoordinator`; bridge probing, hashing, and cache reads stay inside `SaveLibrary`. Ticket party sprites now resolve through `PokeSpriteAssets` from parsed `party_slots`, not from ad hoc sprite filenames. Save-derived ticket text such as game title and trainer name should use the Unicode-preferring font path so Japanese names and full-width punctuation render the same way as speech-bubble text.

- [`src/ui/transfer_ticket/TransferTicketListController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_ticket/TransferTicketListController.cpp)
  Owns transfer-ticket list state and behavior independent of SDL rendering: selected ticket, scroll target/current offset, pointer drag vs click behavior, rip activation timing, fade-to-black handoff timing, return-to-title requests, and the selected-save handoff into the transfer system. Future transfer-ticket rules should grow here instead of adding more runtime booleans to `TransferTicketScreen`. Focus tests for this contract live in [`transfer_ticket_list_controller_tests.cpp`](/Users/vanta/Desktop/title_screen_demo/tests/native/transfer_ticket/transfer_ticket_list_controller_tests.cpp).

- [`LoadingScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/LoadingScreen.cpp)
  Owns the black loading screen shown while transfer save probing runs in the background. It reads [`loading_screen.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/loading_screen.json), picks random ball PNGs from the configured loading asset directory, and swaps balls after each animation lap.

- [`TransferSystemScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TransferSystemScreen.cpp)
  Owns the post-ticket game transfer UI shell (box grid, animated background). Layout and tuning live in [`game_transfer.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/game_transfer.json), separate from the ticket selector’s [`transfer_select_save.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/transfer_select_save.json). External-save box rendering reads the already parsed `PcSlotSpecies` payload carried on `TransferSaveSelection` and resolves textures through `PokeSpriteAssets`; it should not parse bridge JSON or build sprite paths itself. The file is still large, but config parsing, top-level UI animation/tool/pill/exit state, right-panel box-browser state, and Pokemon action-menu state no longer belong directly to the screen. The remaining screen-side input code should stay as thin adaptation helpers that translate keyboard/controller/pointer intent into controller calls instead of reintroducing long-lived UI state bags. In particular, focused speech-bubble visibility is a focus/input contract now: keyboard/controller focus on a valid Pokemon slot or game icon should surface the bubble even when the previous focused UI control did not use one, while mouse-only bubble visibility remains hover-driven.

- [`src/ui/transfer_system/TransferInfoBannerPresenter.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/TransferInfoBannerPresenter.cpp)
  Owns the pure mapping from the active lower-banner context into rendered field values. That context can be an occupied Pokemon slot, the game icon summary, the current tool-carousel selection, the pill toggle mode, or the empty placeholder state. Keep new banner data rules here: icon keys, secondary-type visibility, OT fallback behavior, origin-region text, tooltip copy, and optional-status visibility. Origin should prefer per-Pokemon metadata carried on `PcSlotSpecies` (`origin_game`, `met_location_name`) over save-title guesses, because titles like HG/SS are not enough to distinguish Johto-vs-Kanto catches.

- [`src/ui/transfer_system/TransferInfoBannerRenderer.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/TransferInfoBannerRenderer.cpp)
  Owns the SDL rendering side of the lower info banner: active-context selection, banner texture caches, text fitting, generated marking/gender symbols, PokeSprite misc icon loading, and drawing the configured `game_transfer.json -> info_banner` layout. Banner visual changes should stay here, while non-visual field rules stay in `TransferInfoBannerPresenter.cpp`.

- [`src/ui/transfer_system/GameTransferConfig.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/GameTransferConfig.cpp)
  Is now the single source of truth for `game_transfer.json` parsing. If a transfer-system layout/style/fade/pill/carousel/dropdown/cursor field is authorable in JSON, parse it here rather than duplicating config readers inside `TransferSystemScreen.cpp`.

- [`src/ui/transfer_system/TransferSystemUiStateController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/TransferSystemUiStateController.cpp)
  Owns the pure top-level transfer-system UI state that does not require SDL rendering: first-enter fade values, pill target state, panel reveal state, tool-carousel slide/selection state, exit progression, and one-shot UI/button/return requests. If a rule can be tested without instantiating the full screen or renderer, prefer keeping it here.

- [`src/ui/transfer_system/GameBoxBrowserController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/GameBoxBrowserController.cpp)
  Owns the pure right-panel box-browser state that does not require SDL rendering: active game box index, Box Space mode + row offset, dropdown open/close animation state, highlighted dropdown row, and dropdown scroll clamping/selection rules. Future game-box browsing behavior should extend this controller first rather than adding more mutable dropdown or box-space fields back into `TransferSystemScreen.cpp`.

- [`src/ui/transfer_system/PokemonActionMenuController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/PokemonActionMenuController.cpp)
  Owns the pure temporary Pokemon action menu state for the normal transfer tool: open/close animation progression, source slot identity, side-aware placement geometry, row hit testing, and keyboard/controller row selection. The screen adapts accept/back/pointer input into this controller, keeps the lower info banner anchored to the selected Pokemon while the menu is open, clamps the menu above the info banner, and optionally dims non-selected box sprites through `game_transfer.json -> pokemon_action_menu.dim_background_sprites`. Future action behavior such as Summary, Mark, or Release should add explicit actions around this controller instead of storing more menu flags in `TransferSystemScreen.cpp`.

- [`src/ui/transfer_system/PokemonMoveController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/PokemonMoveController.cpp)
  Owns the temporary in-memory Pokemon move state used by the action-menu Move row and the swap tool: held Pokemon payload, return slot, pointer-vs-keyboard mode, and pickup source. Pointer drops are always click-to-place (never pointer-up on a slot) so the swap tool can use the same press-based navigation as the rest of the UI while a Pokemon is in hand. `TransferSystemScreen.cpp` snapshots the held sprite texture at pickup and on hand-swaps so the in-hand draw stays stable, draws a same-silhouette shadow (tinted duplicate of that texture), applies state transitions to the game/resort slot arrays, refreshes box models, and requests pickup/putdown SFX. Occupied-slot behavior is data-driven in `game_transfer.json -> pokemon_action_menu`: modal moves can send the target back to the held Pokemon's return slot, while swap-tool moves can keep the target in hand. This is still intentionally non-persistent; save mutation belongs in a later write-back layer.

- [`src/ui/transfer_system/TransferSystemFocusGraph.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/TransferSystemFocusGraph.cpp)
  Owns the deterministic keyboard/controller navigation topology for the transfer screen: grid wrap rules, chrome-to-grid transitions, footer/top-control links, and single-panel fallback behavior. If a new interactive control changes directional navigation, update this graph module and its tests instead of editing ad hoc neighbor wiring inside `TransferSystemScreen.cpp`.

- [`src/ui/transfer_system/TransferSystemRenderer.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/TransferSystemRenderer.cpp)
  Owns the high-level transfer-system rendering pass and the chrome-heavy drawing methods: animated background, pill toggle, tool carousel, dropdown chrome/list, Pokemon action menu drawing, and the top-level `render()` orchestration. If visual work changes transfer-system chrome or draw order, prefer updating this renderer unit instead of pushing more rendering code back into `TransferSystemScreen.cpp`.

- [`BoxViewport.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/BoxViewport.cpp)
  Renders reusable 6x5 transfer-box chrome for the current transfer system shell. It is a UI widget fed by `BoxViewportModel`; it is not canonical Resort storage and should not own persistence or import/export decisions. Box-name title rendering should stay on the Unicode-preferring font path so external save box names match the character coverage of transfer speech bubbles.

## Portability Goals

This project should stay easy to move across operating systems over time. The preferred shape is:

- shared gameplay and UI logic in portable C++ where possible
- OS-specific behavior isolated behind small adapter modules
- data and authoring in JSON or files rather than hard-coded platform branches
- build and packaging differences handled in the build system or platform entry points, not scattered through scene code

When adding or changing behavior, assume future support for Linux, Windows, and Android is a real goal unless the change is explicitly platform-only.

### What Should Stay Shared

- scene state and transitions
- menu flow and interaction rules
- layout and timing values that can be expressed in config
- save schema and persistence rules
- asset lookup policy, as long as the paths can be resolved through a platform-aware helper

### What Should Be Isolated

- audio backends
- app lifecycle and window creation details
- filesystem roots and save locations
- controller, keyboard, and touch input translation
- external helper processes or native bridges
- packaging-specific logic, such as bundle/resource lookup

### Working Rule For New Code

If a change can be written as an intent, config value, or scene event, prefer that over a direct OS call inside UI code. If a change genuinely needs platform APIs, keep the boundary thin and document it in this file.

## State Machine

The title screen currently moves through these runtime states:

1. `SplashFadeIn`
2. `SplashHold`
3. `SplashFadeOut`
4. `MainLogoOnBlack`
5. `WhiteFlash`
6. `TitleHold`
7. `WaitingForStart`
8. `StartTransition`
9. `MainMenuIntro`
10. `MainMenuIdle`
11. `MainMenuToSection`
12. `MainMenuSectionFade`
13. `OptionsIntro`
14. `OptionsIdle`
15. `OptionsOutro`
16. `SectionScreen`

This means the current architecture is scene-centric rather than screen-per-flow. The `TitleScreen` object acts as both controller and renderer for the entire front-end experience.

## Data Flow

### Startup pipeline

1. [`main.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/main.cpp) calls `runApplication`.
2. [`App.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/App.cpp) finds the root and loads [`config/app.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/app.json) plus [`config/title_screen.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/title_screen.json).
3. [`ConfigLoader.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/ConfigLoader.cpp) maps JSON into [`Types.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/core/Types.hpp) structs.
4. SDL subsystems are initialized and the renderer logical size is configured.
5. [`Assets.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/Assets.cpp) loads textures and text assets.
6. [`SaveDataStore.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/SaveDataStore.cpp) restores persisted options into `TitleScreen`.
7. The app loop starts. When the player opens TRANSFER, `App.cpp` starts `TransferFlowCoordinator`, which switches through loading, ticket selection, selected-save deep probing, and transfer-system screens.

The transfer probe path goes through the PKHeX bridge rather than native C++ PKHeX bindings. Read [`PKHEX_BRIDGE.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/PKHEX_BRIDGE.md) before changing save probing, bridge JSON, box data, bag data, trainer data, Pokedex data, or future save write/edit behavior.

### Frame pipeline

1. `App.cpp` polls SDL events.
2. Keyboard, mouse, and controller events are translated into `ScreenInput` actions for the active screen. Vertical menu navigation uses a short app-level hold delay before repeating, instead of relying on OS key-repeat.
3. The active `Screen` updates. For transfer entry, `TransferFlowCoordinator` updates the loading screen while the save-probe task runs.
4. `App.cpp` reads scene side effects:
   - whether menu music should be playing
   - current music and SFX volume
   - whether a button SFX should fire
   - whether user settings should be persisted
5. The active screen renders.
6. SDL presents the frame.

### Persistence pipeline

1. Options state changes inside `TitleScreen`.
2. `TitleScreen` raises a save request flag.
3. `App.cpp` converts current scene settings into `SaveData`.
4. [`SaveDataStore.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/SaveDataStore.cpp) writes the primary save and refreshes the backup.

### Resort storage pipeline

The canonical Pokemon storage foundation is separate from the options save file. [`App.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/App.cpp) opens [`PokemonResortService`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/resort/services/PokemonResortService.hpp) under the existing SDL preference directory as `profile.resort.db` and seeds the default profile boxes. The service runs migrations, owns repositories, and exposes methods for profile seeding, parsed import, export projection, mirror sessions, Pokemon lookup, placement lookup, and lightweight box views.

The current backend vertical slice supports:

1. creating default boxes and empty box slots for a profile
2. matching import-grade Pokemon by stable identifiers when available (`HOME` tracker, then PID/encryption constant/TID/SID/OT, then PID/TID/SID/OT)
3. creating a new canonical `ResortPokemon` hot/warm/cold record when no stable match exists
4. merging into an existing canonical record when a stable match is found
5. storing an imported raw snapshot with a caller-provided SHA-256 hash before canonical create/update work inside a deferred-FK transaction
6. preserving warm/cold JSON during merge, including union-style array merging for modeled collections such as ribbons/marks
7. placing that Pokemon into `box_slots` with explicit placement policy (`RejectIfOccupied` by default, `ReplaceOccupied` for controlled replacement flows)
8. writing history events for creation, merge, and placement
9. querying `PokemonSlotView` rows through an indexed box-slot join
10. creating export projection snapshots and active mirror sessions
11. recognizing managed beacon returns before generic identity matching

The native import boundary is [`ImportedPokemon`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/resort/domain/ImportedPokemon.hpp), which requires exact raw Pokemon bytes and a SHA-256 hash. [`BridgeImportAdapter`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/resort/integration/BridgeImportAdapter.hpp) parses the import-grade JSON emitted by the process-based PKHeX bridge and intentionally rejects transfer-ticket summaries.

[`PokemonMatcher`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/resort/services/PokemonMatcher.hpp) owns canonical identity lookup policy. [`PokemonMergeService`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/resort/services/PokemonMergeService.hpp) owns canonical merge policy. Repositories own SQL only; UI screens and `App.cpp` do not own Resort import, matching, merge, or storage decisions.

[`MirrorSessionService`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/resort/services/MirrorSessionService.hpp) owns managed mirror lifecycle. [`PokemonExportService`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/resort/services/PokemonExportService.hpp) owns export projection snapshots and mirror opening. The current bridge write-back command validates projection inputs but intentionally refuses unsafe save mutation until real PKM conversion and slot-write rules are implemented per format.

Still deferred:

1. native Gen 1/2 best-effort matching beyond managed beacon safety
2. real bridge PKM conversion and save slot write-back
3. replacing the current transfer UI mock storage

See [`docs/backend/frontend_integration.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/backend/frontend_integration.md) before building UI against this subsystem.

## Configuration Surface

Shared application settings live in [`app.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/app.json). It currently controls:

- physical dev window size
- renderer logical size / virtual resolution
- design coordinate size
- app window title
- shared input bindings
- shared audio asset paths and default volumes

The current target/design resolution is `1280x800`. The default desktop preview window now opens at `640x400` for a compact desktop footprint, while `virtual_width`, `virtual_height`, `design_width`, and `design_height` remain `1280x800`; only change `window.width` and `window.height` when resizing the desktop preview window.

Keyboard bindings are data-driven through `app.json` → `input` and parsed by `InputBindings.cpp`. To add a remappable keyboard action, add a named vector of binding strings to `InputConfig`, load it in `ConfigLoader.cpp`, add defaults to `app.json`, and route it in `InputRouter.cpp`. Controller buttons are currently mapped in `InputRouter.cpp`; keep that mapping centralized until controller remapping is persisted in user settings. Screens should not inspect raw SDL keycodes/buttons unless they intentionally implement a special text-entry or gameplay-control mode.

Any new input action should include tests at two levels: a config-loader contract test proving the JSON field is parsed, and an input-router test proving the loaded binding reaches the active `ScreenInput`. This keeps future keyboard/controller settings work data-driven instead of relying on hard-coded screen branches.

The main title-screen authoring file is [`title_screen.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/title_screen.json). It currently controls:

- asset paths
- timing values for intro phases and transitions
- prompt text, color, and font size
- layout positions
- transition endpoints and speed scales
- menu labels, animation, and selection behavior
- save naming and app identity for `SDL_GetPrefPath`
- skip flags used to shorten iteration loops
- logo shine tuning

If a behavior is timing/layout/content related, JSON is the first place to look. If a behavior changes title flow, start in `TitleScreen.cpp`. If it changes main-menu selection/action behavior, start in `MainMenuController.cpp`. If it changes options values, labels, or persisted user settings mapping, start in `OptionsMenuController.cpp`. If it changes rendering rules or button geometry, start in `TitleScreenRender.cpp`.

Menu labels are data-driven in `title_screen.json`, while menu selection/action mapping now lives in [`MainMenuController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/title_screen/MainMenuController.cpp). Adding a new top-level item still requires updating both `menu.items` and the controller action mapping, but raw row-index branching should stay out of `TitleScreen`.

The transfer save-ticket page is authored in [`transfer_select_save.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/transfer_select_save.json). Its screen/list coordinates are native `1280x800` logical pixels. Ticket text offsets are local to the ticket art, so they can stay relative to `main_left.png` / `main_right.png` even when full-screen background or banner assets change. The game-specific transfer page after selecting a ticket is configured in [`game_transfer.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/game_transfer.json) (`TransferSystemScreen`).

## Build and Platform Notes

- Build system: [`CMakeLists.txt`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/CMakeLists.txt)
- Primary target: `title_screen_demo`
- Backend/test utility target: `resort_backend_tool`
- Native test target: `resort_storage_tests`
- Native e2e harness target: `title_screen_flow_harness_tests` (separate from the player executable; enables `PR_ENABLE_TEST_HOOKS`)
- Language mode: C++17 plus Objective-C++ for audio today; treat that as an implementation detail, not a requirement for future platforms
- Current platform assumptions: macOS with `SDL2`, `SDL2_image`, and `SDL2_ttf`; `AVFoundation` and `Foundation` are linked on Apple
- Save parsing bridge: external .NET helper under [`tools/pkhex_bridge`](/Users/vanta/Desktop/title_screen_demo/tools/pkhex_bridge), intended to reference `PKHeX.Core`
- Export path: publish the bridge as a self-contained macOS helper and bundle it next to the native executable or inside app resources

For cross-platform work, avoid introducing new macOS-only dependencies in core modules unless there is a clear adapter layer or a fallback path for the other target platforms.

## Architectural Strengths

- Clear separation between app loop, config loading, assets, input, persistence, and scene logic
- Strong typed config boundary through [`Types.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/core/Types.hpp)
- Data-driven tuning for many visual and interaction parameters
- Save writes are defensive and backup-aware

## Current Architectural Constraints

- [`TitleScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TitleScreen.cpp) is smaller after extracting rendering plus main-menu, options, and placeholder section identity behavior, but it still owns the title flow state machine and surrounding section transitions. Title one-shot side effects now flow through typed `TitleScreenEvent` values instead of several boolean consume-style methods.
- Active UI pages now share the `Screen` update/render/input surface, transfer-specific switching/futures live in `TransferFlowCoordinator`, and SDL input routing lives in `InputRouter`. `App.cpp` still owns top-level screen mode, audio decisions, persistence writes, and per-flow side-effect polling.
- Native automated coverage currently exists for the Resort backend via `resort_storage_tests`, for extracted title-screen menu behavior via `title_screen_menu_tests`, for extracted options behavior via `title_screen_options_tests`, for shared input dispatch via `input_router_tests`, and for scripted title-flow e2e behavior via `title_screen_flow_harness_tests`. PKHeX bridge unit and integration tests live under [`tests`](/Users/vanta/Desktop/title_screen_demo/tests); see [`tests/README.md`](/Users/vanta/Desktop/title_screen_demo/tests/README.md).
- Audio is coordinated externally by `App.cpp` through scene flags rather than through a more explicit event/effect system.
- `PKHeX.Core` is not linked into the native app directly; save probing currently depends on an external helper process and local .NET SDK/runtime availability.

## Recommended Next Refactors

### Near-term

- Split `TitleScreen` into smaller collaborators such as:
  - intro/title sequence controller
  - section screen controller
- Continue moving new full-screen pages behind the `Screen` surface and small flow coordinators where a feature owns multiple screens.
- Extend typed screen events where one-shot app-level effects appear; avoid a generic scene stack/router until gameplay requirements justify it.
- Add thin platform/service interfaces for audio, save-path resolution, and helper-process spawning so those concerns can be swapped without touching scene code.
- Prefer config-driven layout and asset selection over hard-coded coordinates or platform checks inside render logic.

### Mid-term

- Add tests for:
  - save load fallback behavior
  - save atomic-write error handling
- Continue expanding config/input/harness coverage whenever new controls, screens, or scene transitions are introduced.
- Add focused native tests whenever extracting more title-screen logic from SDL-bound rendering code.
- Extend native tests when adding Resort backend behavior; the existing executable is `resort_storage_tests`.
- Separate render concerns from state transitions if the UI grows beyond the current title/menu scope.
- Add platform-specific build notes for Windows, Linux, and Android once those targets become active, including any required runtime assets or packaging steps.

## LLM Handoff Notes

If another model is asked to work in this repo, the best initial reading order is:

1. [`pokemon-resort/docs/ARCHITECTURE.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/ARCHITECTURE.md)
2. [`pokemon-resort/config/title_screen.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/title_screen.json)
3. [`pokemon-resort/include/core/Types.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/core/Types.hpp)
4. [`pokemon-resort/src/core/App.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/App.cpp)
5. [`pokemon-resort/src/ui/TitleScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TitleScreen.cpp)

That sequence gives the fastest path to understanding current behavior, extension points, and where complexity currently lives.

## Guidance For Future Agents

When making changes here, keep these habits in mind:

- State whether the source of truth is JSON config, runtime code, or persisted save data before editing behavior.
- Prefer adding a boundary rather than teaching `App.cpp` or `TitleScreen.cpp` about a new operating system.
- If a feature is likely to vary by platform, start with a shared interface and a single implementation, then add the alternate backend only when needed.
- Keep tests current with every refactor. Prefer useful behavior coverage over shallow line coverage, keep changed/high-risk areas above 80% meaningful coverage, and use mutation checks for major new test scaffolds.
- Keep docs updated when module boundaries or portability assumptions change, especially if a new dependency makes the project harder to move between operating systems.
- Be wary of one-off platform checks in scene logic; they tend to age badly and make the app harder to port later.
