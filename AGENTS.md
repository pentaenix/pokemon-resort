# Repository Guidance For Coding Agents

## Scope

This repository is the active Pokemon Resort code project. Treat the sibling [`saves`](/Users/vanta/Desktop/title_screen_demo/saves) directory as reference/sample data, not source code.

## First Files To Read

Always start with:

1. [`README.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/README.md)
2. [`docs/ARCHITECTURE.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/ARCHITECTURE.md)
3. [`tests/README.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tests/README.md)
4. [`docs/architecture/system-map.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/architecture/system-map.md)
5. [`docs/architecture/module-rules.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/architecture/module-rules.md)
6. [`docs/agents/agent-playbook.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/agents/agent-playbook.md)
7. [`docs/agents/token-budget-policy.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/agents/token-budget-policy.md)

Then follow the task-specific path:

- **Title/menu/options work:** [`config/title_screen.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/title_screen.json), [`include/core/Types.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/core/Types.hpp), [`src/ui/TitleScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TitleScreen.cpp), and [`src/ui/title_screen`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/title_screen).
- **Transfer entry, loading, or ticket selection:** [`src/ui/TransferFlowCoordinator.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TransferFlowCoordinator.cpp), [`src/ui/transfer_flow`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_flow), [`src/ui/TransferTicketScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TransferTicketScreen.cpp), [`src/ui/transfer_ticket`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_ticket), [`config/transfer_select_save.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/transfer_select_save.json), and [`config/loading_screen.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/loading_screen.json).
- **Transfer system UI work:** [`docs/transfer_system/README.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/transfer_system/README.md), [`docs/config/game_transfer.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/config/game_transfer.md), [`config/game_transfer.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/game_transfer.json), [`src/ui/TransferSystemScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TransferSystemScreen.cpp), [`src/ui/transfer_system`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system), [`include/ui/FocusManager.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/ui/FocusManager.hpp), and [`include/ui/transfer_system/move/HeldMoveController.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/ui/transfer_system/move/HeldMoveController.hpp).
- **Config or input work:** [`docs/config/README.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/config/README.md), [`config/app.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/app.json), [`include/core/Types.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/core/Types.hpp), [`src/core/config/ConfigLoader.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/config/ConfigLoader.cpp), [`src/core/input/InputBindings.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/input/InputBindings.cpp), and [`src/core/input/InputRouter.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/input/InputRouter.cpp).
- **Save scanning, transfer summaries, or cache work:** [`src/core/save/SaveLibrary.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/save/SaveLibrary.cpp), [`include/core/domain/PcSlotSpecies.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/core/domain/PcSlotSpecies.hpp), [`src/core/bridge/SaveBridgeClient.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/bridge/SaveBridgeClient.cpp), and [`docs/PKHEX_BRIDGE.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/PKHEX_BRIDGE.md).
- **PKHeX bridge work:** [`docs/PKHEX_BRIDGE.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/PKHEX_BRIDGE.md), [`tools/pkhex_bridge/README.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tools/pkhex_bridge/README.md), [`tools/pkhex_bridge/BridgeConsole.cs`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tools/pkhex_bridge/BridgeConsole.cs), [`tools/pkhex_bridge/BridgeProbe.cs`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tools/pkhex_bridge/BridgeProbe.cs), [`tools/pkhex_bridge/BridgeImport.cs`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tools/pkhex_bridge/BridgeImport.cs), and [`tools/pkhex_bridge/BridgeWriteBack.cs`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tools/pkhex_bridge/BridgeWriteBack.cs).
- **PokeSprite or icon work:** [`docs/assets/pokesprite_subsystem.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/assets/pokesprite_subsystem.md), [`include/core/assets/PokeSpriteAssets.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/core/assets/PokeSpriteAssets.hpp), [`src/core/assets/PokeSpriteAssets.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/assets/PokeSpriteAssets.cpp), and the metadata under [`assets/pokesprite/data`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/assets/pokesprite/data).
- **TEST ATTEND / Pokemon interaction scene work:** [`docs/gameplay/test_attend_agent_guide.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/gameplay/test_attend_agent_guide.md), [`docs/gameplay/test_attend.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/gameplay/test_attend.md), [`docs/gameplay/test_attend_animation_semantics.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/gameplay/test_attend_animation_semantics.md), [`include/gameplay/attend/AttendSceneConfig.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/gameplay/attend/AttendSceneConfig.hpp), [`src/gameplay/attend/AttendSceneConfig.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/gameplay/attend/AttendSceneConfig.cpp), [`src/ui/AttendTestScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/AttendTestScreen.cpp), and [`src/gameplay/attend/rendering`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/gameplay/attend/rendering). If the admin config editor changes too, also read [`pokemon-resort-page/docs/AGENTS.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort-page/docs/AGENTS.md).
- **Resort backend work:** [`docs/backend/README.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/backend/README.md), then the specific backend docs for storage, API, import, export, frontend integration, and testing.

## Current Architecture

- `App.cpp` owns SDL setup, the main loop, composition of app-level services/screens, and final rendering. Top-level app routing, loading, audio direction, transitions, frame requests, and user-settings persistence live under `include/core/app` and `src/core/app`.
- `TitleScreen.cpp` owns the title/menu/options/placeholder-section flow, with extracted menu, options, section, and render collaborators under `src/ui/title_screen`.
- `TransferFlowCoordinator.cpp` owns the runtime transfer shell after TRANSFER is selected. Pure transfer-flow decisions live under `src/ui/transfer_flow`.
- `TransferTicketScreen.cpp` owns transfer-ticket rendering. Ticket list behavior lives in `TransferTicketListController`.
- `TransferSystemScreen.cpp` is still the SDL-heavy transfer-system adapter, but config parsing, top-level UI state, box browsing, focus topology, action menus, held movement, banner presentation, and much of rendering now live in smaller transfer-system modules.
- `src/core/config/ConfigLoader.cpp`, `GameTransferConfig.cpp`, and `Types.hpp` form the config contract for app/title/transfer authoring surfaces.
- `src/core/save/SaveDataStore.cpp` owns options save compatibility and atomic writes.
- `src/core/save/SaveLibrary.cpp` owns external save discovery, bridge probing, parsed transfer models, and cache decisions.
- `src/core/bridge/SaveBridgeClient.cpp` is the native process boundary for PKHeX-related work and should stay process-based unless the architecture intentionally changes.
- `src/core/assets/PokeSpriteAssets.cpp` owns Pokemon sprite, item icon, misc icon, and texture-cache resolution for UI consumers.
- `src/resort` owns canonical Resort storage, import/export services, matching/merge policy, and repositories.

## Working Rules

- Prefer config changes for timing, layout, labels, colors, asset paths, icon field placement, animation tuning, input bindings, and audio defaults.
- Prefer code changes for state transitions, controller rules, input semantics, parsing, persistence, bridge behavior, and save/cache decisions.
- Treat persisted save/profile data as source of truth only for player/profile state that must survive app restarts.
- Keep `App.cpp` as the composition/main-loop layer. Put app-level routing changes in `src/core/app/screen`, loading-screen selection in `src/core/app/loading`, app transition timing in `src/core/app/transition`, audio policy in `src/core/app/audio`, and one-frame app requests in `src/core/app/frame`.
- Keep `TitleScreen.cpp` and `TransferSystemScreen.cpp` from growing new long-lived state bags when a smaller controller, presenter, renderer, or config parser can own the rule.
- Preserve compatibility with existing save files unless a migration is intentionally introduced.
- Keep `PKHeX.Core` behind the external bridge in [`tools/pkhex_bridge`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tools/pkhex_bridge) instead of linking it into the native target.
- For shipping work, prefer the published self-contained bridge executable over `dotnet run` or raw DLL execution.

## Testing Requirements

- [`tests/README.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tests/README.md) is the canonical testing map.
- Every refactor or behavior change must keep the test suite updated. Add, adjust, or remove tests in the same change when contracts move.
- Aim for over 80% meaningful coverage of changed and high-risk behavior, but do not chase vanity line coverage.
- Follow the test pyramid:
  - unit/contract tests for pure controllers, config parsing, input binding aliases, bridge validation, and backend services
  - integration tests for JSON-to-runtime behavior, storage, bridge parsing, save fixtures, and import/export boundaries
  - e2e/harness tests for important SDL player flows using separate test executables, not player-facing test APIs
- Tests should fail for the right reason. Failure messages must name the broken contract and point future agents toward the likely source files.
- When adding controls or screens, update the narrow seam test first and the native harness when behavior is player-visible.
- Keep test-only hooks behind compile definitions such as `PR_ENABLE_TEST_HOOKS` and only enable them on test targets. Do not define test hooks for `title_screen_demo` or any shipping player binary.
- Before finishing behavior work, run the relevant focused tests plus the full native suite:

```bash
cmake --build /Users/vanta/Desktop/title_screen_demo/pokemon-resort/build
ctest --test-dir /Users/vanta/Desktop/title_screen_demo/pokemon-resort/build --output-on-failure
```

- **`ctest` alone does not run the PKHeX bridge .NET tests.** After substantive work on saves, the bridge, transfer import/write-back, or `SaveLibrary` / `SaveBridgeClient`, run the **combined** script from the repo root (bridge unit → bridge integration → native `ctest`, including SDL harnesses):

```bash
tests/run_all_tests.sh
```

  See [`tests/README.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tests/README.md) (“After a large change”) for optional bridge CLI e2e.

- Run bridge tests sequentially when touching `tools/pkhex_bridge`, save probing, import-grade JSON, write-projection validation, or bridge-backed native import. Do not parallelize the two .NET bridge projects because they share bridge build output.
- For major new test infrastructure, do a small mutation check: intentionally break the protected contract, confirm the test fails clearly, then restore the break before finishing.

## LLM Best Practices For This Repo

- Summarize assumptions before large refactors.
- Before editing behavior, state whether the source of truth is JSON config, runtime code, or persisted save/profile data.
- Update [`docs/ARCHITECTURE.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/ARCHITECTURE.md) whenever module boundaries, runtime flow, or extension guidance changes.
- If domain boundaries change, update `docs/architecture/system-map.md`, `docs/architecture/module-rules.md`, and add an ADR under `docs/architecture/adrs/`.
- Update config docs when adding or moving JSON fields.
- If you add another recurring contributor-facing guide, keep it short, link it from the nearest higher-level doc, and identify its source-of-truth scope.
- Keep scripts and one-off utilities small: **no script should grow beyond 500 lines**. Split large automation into modules or dedicated tools with clear contracts.
- When touching transfer persistence or movement, treat these docs as hard constraints:
  - `docs/openhome-first-mirror-retirement.md`
  - `docs/openhome-persistent-identity-integration.md`
  - `docs/transfer_system/SAVE_EXIT_SAFETY.md`

## Current Gaps

- The canonical Resort backend exists, but the transfer system screen still uses in-memory UI slot state for Pokemon/item movement. Replacing that with backend-backed storage remains deferred.
- Bridge write-projection validates inputs but intentionally refuses real save mutation until target-format PKM conversion and safe slot-write rules are implemented.
- `TransferSystemScreen.cpp` is still large. New behavior should usually land in config, pure controllers, focus/held-move helpers, presenters, or render helpers before expanding the screen directly.
- The app is currently macOS-oriented because audio uses Objective-C++ and Apple frameworks.
