# Pokemon Resort Architecture

This is the central architecture map for the SDL2 app in [`pokemon-resort`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort). It should stay accurate enough that a human or AI agent can decide where a change belongs before editing code.

For test strategy, use [`tests/README.md`](/Users/vanta/Desktop/title_screen_demo/tests/README.md) as the canonical test map. For config ownership, use [`docs/config/README.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/config/README.md). For bridge work, use [`PKHEX_BRIDGE.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/PKHEX_BRIDGE.md).

## Current State

The app currently implements:

- title intro, title hold, main menu, options menu, and placeholder Resort/Trade section flow
- transfer entry from the main menu
- loading screen while external saves are scanned and probed
- transfer-ticket selection built from bridge/cache summaries
- selected-save deep probe into the transfer system screen
- transfer-system box UI with Box Space, tool carousel, lower info banner, Pokemon action menu, item action menu, keyboard/controller focus, pointer gestures, and temporary in-memory Pokemon/item movement
- persisted user options through `SaveDataStore`
- a canonical SQLite-backed Resort backend foundation that is open at runtime but not yet the transfer screen's storage source of truth

The codebase is transfer-heavy now. `TitleScreen.cpp` still matters, but transfer work should start in the transfer flow, transfer ticket, transfer system, bridge, or backend modules named below.

## Source-Of-Truth Rules

- **JSON config** owns authored layout, timing, text, colors, asset paths, icon placement, animation tuning, input bindings, and audio defaults.
- **Runtime code** owns state machines, input semantics, controller behavior, bridge/cache parsing, persistence decisions, and screen transitions.
- **Persisted save/profile data** owns player settings, canonical Resort Pokemon state, and future durable transfer/storage state.
- **Bridge JSON** is an external process contract. Parse it at boundaries such as `SaveLibrary` or `BridgeImportAdapter`; do not re-query raw bridge JSON from UI rendering code.

When changing behavior, name which source of truth you are changing before you edit.

## Runtime Architecture

### Entry And App Orchestration

- [`src/main.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/main.cpp) forwards an optional title config path into `pr::runApplication` and supports `--clear-save-cache`.
- [`src/core/App.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/App.cpp) owns SDL initialization, root discovery, config loading, window/renderer setup, shared assets/fonts, persisted options restore, Resort service startup, input dispatch, active screen/flow selection, audio decisions, and the main loop.
- Keep `App.cpp` orchestration-focused. New screen-specific state should live in a screen, coordinator, controller, service, or config parser.

### Core Modules

- [`ConfigLoader.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/ConfigLoader.cpp) parses app/title config into types from [`Types.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/core/Types.hpp).
- [`Types.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/core/Types.hpp) is the app/title config and persisted settings contract.
- [`Assets.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/Assets.cpp) resolves project-root asset paths, loads textures, renders text, and builds title logo masks.
- [`PokeSpriteAssets.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/PokeSpriteAssets.cpp) owns Pokemon sprite, item icon, misc icon, and SDL texture-cache resolution for transfer UI. Its contract is documented in [`docs/assets/pokesprite_subsystem.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/assets/pokesprite_subsystem.md).
- [`InputBindings.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/InputBindings.cpp) maps human-readable key names from JSON to SDL keycodes.
- [`InputRouter.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/InputRouter.cpp) translates SDL keyboard, mouse, and controller events into the active [`ScreenInput`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/ui/ScreenInput.hpp).
- [`SaveDataStore.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/SaveDataStore.cpp) loads primary/backup user settings and writes atomically.
- [`SaveBridgeClient.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/SaveBridgeClient.cpp) resolves and launches the external .NET bridge process.
- [`SaveLibrary.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/SaveLibrary.cpp) scans the top-level [`saves`](/Users/vanta/Desktop/title_screen_demo/saves) folder, hashes candidates, probes through the bridge, caches summaries, and parses native transfer models for ticket and box UI.
- [`src/resort`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/resort) and [`include/resort`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/resort) own canonical Resort storage, repositories, matching, merge policy, import/export services, mirror sessions, and backend read models.
- [`Audio.mm`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/Audio.mm) is the current macOS audio backend used by `App.cpp`.

## UI Layer

### Shared Screen Surface

- [`ScreenInput.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/ui/ScreenInput.hpp) defines the reusable input vocabulary for screens.
- [`Screen.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/ui/Screen.hpp) extends that vocabulary with `update(dt)` and `render(renderer)`.
- Full-screen UI pages should prefer `Screen` unless they are intentionally only helper/controller code.

### Title, Menu, Options, And Placeholder Sections

- [`TitleScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TitleScreen.cpp) owns title-flow state transitions, high-level title/menu/options/section coordination, and one-shot title events consumed by `App.cpp`.
- [`MainMenuController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/title_screen/MainMenuController.cpp) owns top-level menu selection and selected-row to menu-action mapping.
- [`OptionsMenuController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/title_screen/OptionsMenuController.cpp) owns options selection, value cycling, labels, and mapping to/from persisted `UserSettings`.
- [`SectionScreenController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/title_screen/SectionScreenController.cpp) owns placeholder section identity for menu destinations such as Resort and Trade.
- [`TitleScreenRender.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/title_screen/TitleScreenRender.cpp) owns title/menu/options/section rendering helpers, button geometry, texture caches, and logo shine generation.

Title-side effects should flow through typed `TitleScreenEvent` values. Avoid adding more boolean consume methods.

### Transfer Flow Shell

- [`TransferFlowCoordinator.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TransferFlowCoordinator.cpp) owns the runtime transfer shell after the title menu requests TRANSFER. It starts async ticket scans and selected-save deep probes, owns concrete SDL screen instances, and reports high-level return/audio/SFX signals to `App.cpp`.
- [`TransferFlowController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_flow/TransferFlowController.cpp) owns the pure transfer-flow state machine: active transfer sub-screen, loading purpose, pending deep-probe selection, remembered last-viewed game box, return-to-title requests, and transfer-system entry requests.
- [`TransferSelectionBuilder.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_flow/TransferSelectionBuilder.cpp) converts `SaveLibrary` summaries/deep probes into UI-facing `TransferSaveSelection` data.

If a transfer flow rule can be tested without SDL or PKHeX, prefer `TransferFlowController` or `TransferSelectionBuilder`.

### Loading And Transfer Ticket Selection

- [`LoadingScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/LoadingScreen.cpp) owns the black transfer loading screen and reads [`loading_screen.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/loading_screen.json).
- [`TransferTicketScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TransferTicketScreen.cpp) renders transfer tickets from already parsed transfer summaries. It should not probe saves or parse bridge JSON.
- [`TransferTicketListController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_ticket/TransferTicketListController.cpp) owns ticket list state: selection, scroll target/current offset, pointer drag/click behavior, rip activation timing, fade-to-black handoff, return-to-title requests, and selected-save handoff.

Ticket party sprites resolve through `PokeSpriteAssets` from parsed `party_slots`. Save names, trainer names, dropdown labels, and box titles should stay on the Unicode-preferring font path.

### Transfer System Screen

[`TransferSystemScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TransferSystemScreen.cpp) is the SDL-heavy adapter for the post-ticket transfer UI. It still owns:

- concrete SDL screen lifecycle and asset/font handles
- adaptation from keyboard/controller/pointer input into controllers
- pointer hit-testing that depends on current rendered geometry
- in-memory game/resort slot arrays for the current non-persistent transfer prototype
- box viewport model refresh and texture prewarming
- application of temporary Pokemon/item moves to those in-memory slots
- selected-save/game title context
- some remaining cursor, speech-bubble, and mini-preview drawing helpers

New transfer-system behavior should first look for one of these smaller seams:

- [`GameTransferConfig.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/GameTransferConfig.cpp) parses [`game_transfer.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/game_transfer.json). If a visual/timing/style value is authorable, parse it here.
- [`TransferSystemUiStateController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/TransferSystemUiStateController.cpp) owns pure top-level UI state: first-enter fade, pill target, panel reveal, tool-carousel slide/selection, exit progression, and one-shot requests.
- [`GameBoxBrowserController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/GameBoxBrowserController.cpp) owns active game box index, Box Space mode/row offset, dropdown animation, highlighted row, scroll clamping, and dropdown selection.
- [`TransferInfoBannerPresenter.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/TransferInfoBannerPresenter.cpp) maps active context into lower-banner field values.
- [`TransferInfoBannerRenderer.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/TransferInfoBannerRenderer.cpp) draws the lower info banner, text fitting, generated symbols, PokeSprite item icons, and PokeSprite misc icons.
- [`PokemonActionMenuController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/PokemonActionMenuController.cpp) owns the normal-tool Pokemon action menu state, geometry, hit testing, and row selection.
- [`ItemActionMenuController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/ItemActionMenuController.cpp) owns the item-tool modal pages, labels, geometry, hit testing, and row selection.
- [`PokemonMoveController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/PokemonMoveController.cpp) owns temporary Pokemon-in-hand state for action-menu moves and swap-tool moves.
- [`move/HeldMoveController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/move/HeldMoveController.cpp) owns the generic held-object state for Pokemon, Box Space boxes, and held items.
- [`TransferSystemFocusGraph.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/TransferSystemFocusGraph.cpp) builds the deterministic keyboard/controller navigation topology for transfer-system controls.
- [`FocusManager.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/FocusManager.cpp) applies explicit directional edges, optional spatial fallback, activation callbacks, and current focus bounds.
- [`TransferSystemRenderer.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/TransferSystemRenderer.cpp) owns high-level transfer-system render orchestration and chrome-heavy drawing: animated background, pill toggle, tool carousel, dropdown chrome/list, action menus, held-object drawing, and draw order.
- [`BoxViewport.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/BoxViewport.cpp) renders reusable 6x5 transfer box chrome from `BoxViewportModel`. It is not canonical Resort storage.

#### Focus And Navigation Ownership

Directional keyboard/controller navigation belongs in `TransferSystemFocusGraph` and `FocusManager`.

- Use explicit edges for predictable multi-panel behavior.
- Update focus-graph tests when adding or moving interactive controls.
- Keep raw neighbor wiring out of `TransferSystemScreen.cpp` unless it is only adapting current runtime geometry into focus nodes.
- Focused valid Pokemon slots and game icons should surface speech bubbles. Mouse-only bubble visibility remains hover-driven.

#### Held Movement And Pointer Gestures

Temporary held-object behavior is split deliberately:

- `PokemonMoveController` owns Pokemon move semantics for the normal action menu and swap tool.
- `HeldMoveController` owns generic held state for Pokemon, Box Space boxes, and held items.
- `move/Gestures.hpp` provides reusable hold/drag gesture helpers for Box Space and quick-drop style behavior.
- `TransferSystemScreen` applies accepted moves to current in-memory slots, refreshes viewport models, and requests pickup/putdown SFX.

Current movement is non-persistent. Save mutation and durable Resort storage integration belong in a later write-back/backend layer.

#### Item Tool Behavior

The yellow item tool currently supports temporary in-memory held-item movement between occupied Pokemon slots.

- `ItemActionMenuController` owns modal pages and actions such as Move Item, Swap Item, Put Away, Back, and Cancel.
- `TransferSystemScreen` applies item pickup, place, swap, cancel, and current modal consequences to `PcSlotSpecies.held_item_id/name`.
- Bag/deposit/storage persistence is not implemented. Put-away style actions should remain explicit placeholders until backend and bag rules exist.
- Item icons should go through `PokeSpriteAssets`, not manually built file paths.

#### Renderer Boundaries

Prefer visual work in render helpers:

- lower info banner visuals: `TransferInfoBannerRenderer`
- transfer-system chrome/draw order/action-menu drawing: `TransferSystemRenderer`
- reusable box chrome: `BoxViewport`
- title/menu/options rendering: `TitleScreenRender`

`TransferSystemScreen.cpp` still contains some geometry-dependent cursor, speech-bubble, mini-preview, and adaptation drawing. When touching those areas, consider whether the next useful step is extraction to a renderer helper with focused harness coverage.

## Save, Bridge, And Backend Data Flow

### Transfer Probe Flow

1. `SaveLibrary` scans the top-level `saves` directory without recursion.
2. It hashes candidate files and checks the transfer probe cache.
3. On a cache miss or stale cache, `SaveBridgeClient` launches the .NET bridge.
4. The bridge emits one JSON object.
5. `SaveLibrary` parses light ticket summaries plus richer `PcSlotSpecies` party/box models.
6. `TransferSelectionBuilder` converts those models into `TransferSaveSelection`.
7. Ticket and transfer-system screens render from parsed native models.

Do not build new UI directly against raw bridge JSON or legacy `box_1` strings. Use parsed native models and extend them when needed.

### Options Persistence Flow

1. `OptionsMenuController` changes settings.
2. `TitleScreen` raises a save request event.
3. `App.cpp` converts scene settings into `SaveData`.
4. `SaveDataStore` writes primary and backup saves atomically.

### Canonical Resort Storage Flow

The Resort backend is separate from the options save file and from transfer-ticket preview data.

- `App.cpp` opens `PokemonResortService` under SDL's preference directory as `profile.resort.db`.
- Repositories own SQL and migrations.
- `BridgeImportAdapter` parses import-grade bridge JSON into `ImportedPokemon`.
- `PokemonMatcher` owns identity lookup.
- `PokemonMergeService` owns canonical merge policy.
- `PokemonResortService` orchestrates import, placement, history, profile boxes, and read models.
- `PokemonExportService` and `MirrorSessionService` own export projection snapshots and managed mirror sessions.

The transfer screen still uses in-memory UI slot state. See [`docs/backend/frontend_integration.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/backend/frontend_integration.md) before replacing that with backend-backed storage.

## Configuration Surface

Shared application config lives in [`app.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/app.json):

- desktop window size
- renderer logical size and design coordinates
- app title
- input bindings
- shared audio assets and default volumes

The target/design resolution is `1280x800`. The desktop preview window is smaller by default; changing the preview window is not the same as changing logical/design coordinates.

Screen config lives in:

- [`title_screen.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/title_screen.json)
- [`loading_screen.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/loading_screen.json)
- [`transfer_select_save.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/transfer_select_save.json)
- [`game_transfer.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/game_transfer.json)

See [`docs/config/README.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/config/README.md) for ownership guidance and [`docs/config/game_transfer.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/config/game_transfer.md) for the transfer-system config reference.

## Testing Map

Use [`tests/README.md`](/Users/vanta/Desktop/title_screen_demo/tests/README.md) as the canonical test source. As of the current build, the native CTest suite includes storage/backend, title controllers, transfer ticket, transfer flow, transfer-system config/state/browser/action-menu/focus, input/config, PokeSprite assets, save-library cache, headless boot, title flow harness, transfer-system harness, and transfer-ticket Unicode harness coverage.

Run:

```bash
cd /Users/vanta/Desktop/title_screen_demo/pokemon-resort
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Run .NET bridge tests sequentially when touching the bridge, save probing, import-grade JSON, write-projection validation, or bridge-backed native import.

## Build And Platform Notes

- Build system: [`CMakeLists.txt`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/CMakeLists.txt)
- Primary target: `title_screen_demo`
- Backend/test utility target: `resort_backend_tool`
- Native tests are registered with CTest; do not rely on a single executable name.
- Language mode: C++17 plus Objective-C++ for the current macOS audio backend.
- Current platform assumptions: macOS with `SDL2`, `SDL2_image`, `SDL2_ttf`, `AVFoundation`, and `Foundation`.
- Save parsing bridge: external .NET helper under [`tools/pkhex_bridge`](/Users/vanta/Desktop/title_screen_demo/tools/pkhex_bridge).
- Shipping bridge path: publish a self-contained helper and bundle it next to the executable or inside app resources.

For cross-platform work, isolate OS-specific behavior behind small adapters. Avoid adding platform checks inside scene logic.

## Architectural Strengths

- Clear process boundary around PKHeX.
- Strong typed config boundary for app/title settings and dedicated parser for transfer-system config.
- Many high-risk transfer behaviors now have pure controller seams.
- Save writes are defensive and backup-aware.
- Resort backend separates canonical storage from preview/ticket UI models.
- Tests cover both narrow controller contracts and SDL harness flows.

## Current Architectural Constraints

- `TransferSystemScreen.cpp` is still large and owns too much geometry-dependent adaptation.
- Transfer-system Pokemon/item movement is temporary and in-memory, not persisted to Resort storage or external saves.
- Bridge write-projection validates inputs but intentionally refuses real save mutation.
- Audio is still macOS-specific.
- Some render responsibilities remain split between screen methods and renderer helpers.

## Recommended Next Refactors

Near-term:

- Continue extracting geometry-dependent transfer cursor, speech-bubble, and mini-preview drawing from `TransferSystemScreen` when tests can protect the move.
- Add focused tests around held item behavior before expanding bag/deposit semantics.
- Keep adding transfer config fields through `GameTransferConfig` rather than one-off JSON reads.
- Add small platform/service adapters for audio, save-path resolution, and helper-process spawning before serious cross-platform work.

Mid-term:

- Replace transfer-system in-memory slot state with canonical Resort storage read models and explicit persistence/write-back flows.
- Complete safe bridge write-back only through the existing `write-projection` operation.
- Add platform-specific build/package notes when Windows, Linux, or Android become active targets.
- Consider a broader screen/router abstraction only when gameplay requirements justify it; do not introduce one just to hide current flow complexity.

## Handoff Notes For Future Agents

Read task-specific files rather than always starting from `TitleScreen.cpp`:

- Use `AGENTS.md` for the task-oriented first-read list.
- Use this architecture doc for module boundaries.
- Use `docs/config/README.md` before adding JSON fields.
- Use `tests/README.md` before deciding what to run.
- Use `PKHEX_BRIDGE.md` before touching save probing, bridge JSON, import-grade reads, or write-back validation.
- Use backend docs before building UI against canonical Resort storage.

When in doubt, preserve existing seams: config parser, pure controller, presenter, renderer, bridge boundary, save-library parser/cache, backend service. New behavior should usually extend one of those before expanding `App.cpp`, `TitleScreen.cpp`, or `TransferSystemScreen.cpp`.
