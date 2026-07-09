# Test Layout

This is the canonical testing map for the repository. Other docs should link here instead of copying partial target lists.

This repository now uses a top-level [`tests`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tests) folder to separate test intent by level:

- [`tests/unit`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tests/unit) for fast, isolated tests with minimal external dependencies
- [`tests/integration`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tests/integration) for tests that exercise real dependencies such as `PKHeX.Core` and sample save files
- [`tests/e2e`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tests/e2e) for higher-level executable or packaging smoke tests

## Current Coverage

- **Bridge unit:** argument validation, missing-file behavior, probe compatibility fields, import command errors, and write-projection command validation.
- **Bridge integration:** probe smoke tests on committed saves under `tests/test-data/saves/` (`BridgeProbeIntegrationTests`), write-back round-trip tests (`BridgeWriteBackIntegrationTests`) when `tests/test-data/pkhex_bridge/writeback_fixture.sav` or `PKHEX_WRITEBACK_FIXTURE_PATH` is provided, a schema-2 PC payload compatibility sweep over committed `tests/test-data/saves/` binaries (optional `PKHEX_WRITEBACK_COMPAT_FIXTURES` override), and held-item patch parity tests (`HeldItemTransferIntegrationTests`) using `tests/test-data/pkhex_bridge/item_transfer.sav`.
- **Bridge e2e:** CLI smoke coverage for the built helper.
- **Native backend:** SQLite-backed Resort storage, import, matching/merge, mirror sessions, export projection, managed returns, rollback, bridge import parsing, and backend seed/export tooling.
- **Native core/config/assets:** app transition state, frame-request aggregation, config loading contracts, config-driven input routing, input router behavior, shared overlay canvas layout/hit testing, PokeSprite Pokemon/item/misc asset resolution, Pokemon cry asset resolution, save-bridge failure message formatting (stdout JSON / stderr fallbacks), and save-library cache generation/hit/miss/staleness behavior.
- **Native title:** main menu, Resort submenu, options, placeholder section controllers, Resort loading-transition state, headless boot smoke, and title-flow harness coverage for key navigation plus music/SFX event contracts.
- **Native attend:** split Pokemon/Alola-map/sky attend config, species-name model resolution, initial auto-focus camera config, large-Pokemon face-view gate config, weighted mouse edge-look config, simple shadow config, pet eye-close delay/cooldown, eye-expression frame config, semantic animation slot config, pet-happy reaction combo config, shared overlay coverage, Pokemon GLB asset loading, skinning animation sampling, Violet eye-mask material coverage, and RAE Gen 1-7/3DS material-policy coverage for render class, sampler wrap, eye sheets, and mesh draw metadata.
- **Native transfer flow/ticket:** transfer selection mapping, pure transfer-flow controller behavior, transfer-ticket list controller behavior, and transfer-ticket Unicode rendering harness coverage.
- **Native transfer system:** `game_transfer.json` parsing, info banner presentation, top-level transfer UI state, game box browser/dropdown/Box Space behavior, Pokemon action-menu behavior, multi-Pokemon move layout rules, focus-graph topology, and SDL harness coverage for keyboard/controller/pointer flows including Box Space, dropdown activation, speech-bubble visibility, Pokemon moves, multi-select moves, and held item move/swap/cancel behavior.

Current native CTest targets:

```text
resort_storage_tests
app_transition_controller_tests
app_frame_requests_tests
title_screen_menu_tests
title_screen_options_tests
title_screen_section_tests
resort_menu_controller_tests
attend_scene_config_tests
attend_pokemon_model_tests
overlay_canvas_tests
resort_loading_transition_screen_tests
transfer_ticket_list_tests
transfer_selection_builder_tests
transfer_flow_controller_tests
game_transfer_config_tests
pokemon_summary_config_tests
transfer_info_banner_presenter_tests
transfer_system_ui_state_controller_tests
game_box_browser_controller_tests
pokemon_action_menu_controller_tests
multi_pokemon_move_controller_tests
transfer_system_focus_graph_tests
input_router_tests
config_loader_tests
pokesprite_assets_tests
pokemon_cry_assets_tests
save_bridge_client_tests
save_library_cache_tests
input_config_integration_tests
title_screen_headless_smoke
title_screen_flow_harness_tests
transfer_system_flow_harness_tests
transfer_ticket_unicode_harness_tests
```

## Regression Strategy For Humans And AI Agents

Use the test pyramid before and after refactors:

- Fast native unit/contract tests should catch most C++ regressions first: controller rules, config parsing, input routing, and storage services.
- Transfer-system behavior is now split deliberately: `GameTransferConfig.cpp` owns JSON parsing, `TransferSystemUiStateController.cpp` owns pill/carousel/enter-exit state, and `GameBoxBrowserController.cpp` owns right-panel box browsing, Box Space, and dropdown behavior. Put new transfer-system tests at the narrowest seam that can express the rule without SDL.
- The normal-tool Pokemon action menu is split between `PokemonActionMenuController.cpp` for pure state/geometry/row selection and `TransferSystemRenderer.cpp` for SDL drawing. Add or update `pokemon_action_menu_controller_tests.cpp` for placement, hit testing, and row navigation rules, then keep one transfer-system harness assertion for accept/pointer player flows.
- Transfer-system directional navigation now has its own pure seam in `TransferSystemFocusGraph.cpp`. If keyboard/controller movement changes, add or update a pure focus-graph test first, then keep one SDL harness assertion for the player-visible flow.
- Transfer-system rendering is partly concentrated in `TransferSystemRenderer.cpp`, with lower-banner drawing in `TransferInfoBannerRenderer.cpp` and some cursor/speech-bubble/mini-preview adaptation still inside `TransferSystemScreen.cpp`. Rendering refactors should pass the transfer-system harness and the relevant focused tests for the seam touched.
- Transfer-system lower-banner content is now data-driven through `game_transfer.json -> info_banner`. Field-to-data mapping lives in `TransferInfoBannerPresenter.cpp`, while `TransferInfoBannerRenderer.cpp` draws the configured layout, including split label/value text styling. Add pure presenter tests for new field mappings first, then keep one harness assertion for the focus/hover flow that actually surfaces the banner.
- The lower banner is context-aware now: `pokemon`, `game_icon`, `tool`, `pill`, and `empty` states can each render different configured fields. When changing tooltip copy or placeholder behavior, update the presenter test and one SDL harness expectation together so hover/focus rules stay honest.
- Origin-region banner behavior is a presenter contract now: prefer per-Pokemon origin/met metadata over save-title inference, especially for games like HG/SS where one save can legitimately contain both Johto- and Kanto-origin catches.
- Mini-preview sprite sizing is a real render contract now: `mini_preview.sprite_scale` should be able to make preview sprites larger than their individual grid cells while still staying clipped to the preview panel. If that regresses, inspect `TransferSystemScreen.cpp` and the transfer-system harness.
- Transfer-system box changes should feel consistent across input modes: dropdown/list selection should animate through the same box-content slide path as button/arrow navigation, and opening a box from Box Space should clear any mini-preview immediately whether it was triggered by keyboard or pointer.
- Transfer save names, trainer names, dropdown labels, and box-title text should stay on the Unicode-preferring font path. If Japanese text, full-width punctuation, or other non-ASCII characters regress into tofu boxes, inspect `Font.cpp`, `TransferTicketScreen.cpp`, `TransferSystemScreen.cpp`, and `BoxViewport.cpp`.
- Native integration tests should prove authored JSON still connects to runtime behavior, especially controls. If an input test fails, inspect `pokemon-resort/config/app.json`, `InputConfig` in `Types.hpp`, `ConfigLoader.cpp`, `InputBindings.cpp`, and `InputRouter.cpp` in that order.
- Transfer-select cache tests use a fake bridge executable through `PKHEX_BRIDGE_EXECUTABLE`, so they validate real `SaveLibrary` cache decisions without depending on .NET. If those fail, inspect `SaveLibrary.cpp` cache key/hash/staleness logic and the diagnostics fields `used_cache`, `bridge_result.bridge_path`, and `bridge_result.command`.
- Smoke tests should only prove that the executable boots far enough to enter the app loop. They intentionally do not replace focused unit/integration tests.
- Native harness tests are the SDL equivalent of Playwright-style user flows. They run in a separate test executable, use real config plus `InputRouter`, and may enable test-only read access with `PR_ENABLE_TEST_HOOKS`. Do not define that macro for the shipping player target.
- Transfer-system harness coverage now lives in [`transfer_system_flow_harness_tests.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tests/e2e/native/transfer_system_flow_harness_tests.cpp). Use it for keyboard/controller/pointer regressions that depend on real SDL screen wiring, especially Box Space, dropdown activation, speech-bubble visibility on focused valid targets, temporary Pokemon move/pickup/drop flows, and other focus-driven behavior that pure controllers cannot catch alone.
- Ticket Unicode coverage now lives in [`transfer_ticket_unicode_harness_tests.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tests/e2e/native/transfer_ticket_unicode_harness_tests.cpp). Use it when changing transfer-ticket text rendering, transfer font loading, or save-name/title text paths.
- Bridge unit/integration/e2e tests are separate because they use the .NET PKHeX helper and sample saves. Run them when touching `tools/pkhex_bridge`, save probing, import-grade JSON, or bridge-backed native import.
- Automated tests load committed binaries under [`tests/test-data/saves`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tests/test-data/saves) (not the mutable top-level [`saves`](/Users/vanta/Desktop/title_screen_demo/saves) folder).

Good failure messages should name the broken contract and the likely source file. When adding tests, prefer observable behavior and data contracts over private implementation details, so future refactors can improve the code without rewriting the suite.

Coverage goals are qualitative first and numeric second. For changed or high-risk behavior, keep meaningful coverage above 80% by protecting the real contracts: successful paths, edge cases, bad input, rollback/error handling, config defaults/overrides, and user-visible flows. A test that merely executes lines without detecting a realistic regression does not count as useful coverage.

For substantial refactors or new harnesses, use a small mutation check before calling the suite trustworthy: temporarily break the contract under test, confirm the correct test fails with a helpful message, restore the break, then rerun the relevant suite. Never leave mutation changes in the final worktree.

Run the two .NET bridge projects sequentially. They share the bridge project output directory, so parallel `dotnet test` runs can race while generating files under `tools/pkhex_bridge/obj`.

### After a large change (save, bridge, transfer, or resort)

**Agents and humans should run the combined stack**, not only `ctest`: native CTest does **not** run the .NET PKHeX bridge tests.

From the repository root:

```bash
tests/run_all_tests.sh
```

This runs, in order: bridge unit tests → bridge integration tests → `cmake` configure/build for `pokemon-resort` → **full `ctest`** (includes SDL harnesses such as `transfer_system_flow_harness_tests`, `title_screen_flow_harness_tests`, `save_bridge_client_tests`, backend tests, etc.).

Optional: probe the built bridge DLL against committed fixtures under `tests/test-data/saves/`:

```bash
RUN_BRIDGE_CLI_E2E=1 tests/run_all_tests.sh
```

Touching `tools/pkhex_bridge`, `SaveBridgeClient`, `SaveLibrary`, import/write-back, or transfer UI should trigger this full run before finishing.

To validate write-back across every PKHeX-supported generation, set `PKHEX_WRITEBACK_COMPAT_FIXTURES` to a path-list containing one save per generation (Gen 1-9). The integration test writes only temporary copies.

## Run commands

```bash
DOTNET_CLI_HOME=/Users/vanta/Desktop/title_screen_demo/.dotnet \
NUGET_PACKAGES=/Users/vanta/Desktop/title_screen_demo/.nuget/packages \
dotnet test /Users/vanta/Desktop/title_screen_demo/pokemon-resort/tests/unit/pkhex_bridge/PKHeXBridge.UnitTests/PKHeXBridge.UnitTests.csproj
```

```bash
DOTNET_CLI_HOME=/Users/vanta/Desktop/title_screen_demo/.dotnet \
NUGET_PACKAGES=/Users/vanta/Desktop/title_screen_demo/.nuget/packages \
dotnet test /Users/vanta/Desktop/title_screen_demo/pokemon-resort/tests/integration/pkhex_bridge/PKHeXBridge.IntegrationTests/PKHeXBridge.IntegrationTests.csproj
```

```bash
/bin/zsh /Users/vanta/Desktop/title_screen_demo/pokemon-resort/tests/e2e/pkhex_bridge/run_bridge_cli_e2e.sh
```

```bash
cd /Users/vanta/Desktop/title_screen_demo/pokemon-resort
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Build the backend seed/export utility:

```bash
cd /Users/vanta/Desktop/title_screen_demo/pokemon-resort
cmake --build build --target resort_backend_tool
```
