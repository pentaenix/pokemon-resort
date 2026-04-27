# Transfer System Guide

This is the working guide for the post-ticket transfer system screen. Read it before changing transfer-system behavior, layout, focus, item/Pokemon movement, lower banner content, or transfer-system rendering.

The purpose of this guide is to keep [`TransferSystemScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TransferSystemScreen.cpp) from becoming the place where every new transfer idea lands. The screen is still important, but it should mostly adapt SDL input/render state to smaller config, controller, presenter, renderer, and movement seams.

## Quick Ownership Map

- [`TransferSystemScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TransferSystemScreen.cpp)
  SDL-heavy adapter. Owns screen lifecycle, live asset/font handles, runtime geometry adaptation, in-memory prototype slot arrays, pointer hit testing, texture/model refresh, and applying temporary moves to current UI slots.
- [`GameTransferConfig.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/GameTransferConfig.cpp)
  Parses [`game_transfer.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/game_transfer.json). New authored layout/style/timing fields belong here.
- [`TransferSystemUiStateController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/TransferSystemUiStateController.cpp)
  Pure top-level UI state: enter/exit fade, pill target, panel reveal, tool-carousel state, and one-shot UI requests.
- [`GameBoxBrowserController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/GameBoxBrowserController.cpp)
  Pure right-panel browsing state: active game box, Box Space mode/row offset, dropdown open/close, highlighted row, scroll clamping, and selected box.
- [`TransferSystemFocusGraph.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/TransferSystemFocusGraph.cpp) and [`FocusManager.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/FocusManager.cpp)
  Keyboard/controller topology, explicit directional edges, current focus, activation callbacks, and focus bounds.
- [`PokemonActionMenuController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/PokemonActionMenuController.cpp)
  Pure normal-tool Pokemon modal state, placement geometry, row hit testing, and row selection.
- [`ItemActionMenuController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/ItemActionMenuController.cpp)
  Pure item-tool modal state, root/put-away pages, labels, placement geometry, row hit testing, and row selection.
- [`PokemonMoveController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/PokemonMoveController.cpp)
  Temporary Pokemon-in-hand state for action-menu moves and swap-tool moves.
- [`MultiPokemonMoveController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/MultiPokemonMoveController.cpp)
  Temporary multi-Pokemon group state: selected Pokemon, original return slots, layout offsets, input mode, pointer position, and candidate pattern placement.
- [`move/HeldMoveController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/move/HeldMoveController.cpp)
  Generic held-object state for Pokemon, Box Space boxes, and held items.
- [`move/Gestures.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/ui/transfer_system/move/Gestures.hpp)
  Reusable hold/drag gesture helpers.
- [`TransferInfoBannerPresenter.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/TransferInfoBannerPresenter.cpp)
  Pure mapping from active context to lower-banner field values.
- [`TransferInfoBannerRenderer.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/TransferInfoBannerRenderer.cpp)
  Lower-banner rendering, text fitting, generated symbols, PokeSprite item icons, and PokeSprite misc icons.
- [`TransferSystemRenderer.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/TransferSystemRenderer.cpp)
  High-level transfer-system draw order and chrome-heavy rendering: animated background, pill, carousel, dropdown, action menus, and held objects.
- [`BoxViewport.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/BoxViewport.cpp)
  Reusable 6x5 box viewport widget. It renders a `BoxViewportModel`; it is not storage.

## Where To Put A Change

Use this section as the first decision point.

| Change | Start here | Tests |
| --- | --- | --- |
| Add/tune layout, colors, fonts, timings, icon placement, menu sizing, speech-bubble style | `game_transfer.json` plus `GameTransferConfig.cpp` | `game_transfer_config_tests`; harness only if wiring changes |
| Change focus movement between controls or slots | `TransferSystemFocusGraph.cpp`, `FocusManager.cpp` | `transfer_system_focus_graph_tests`; harness for player-visible wiring |
| Change selected box, dropdown, Box Space browse behavior | `GameBoxBrowserController.cpp` | `game_box_browser_controller_tests`; harness for screen wiring |
| Change pill, carousel, enter/exit, panel reveal state | `TransferSystemUiStateController.cpp` | `transfer_system_ui_state_controller_tests` |
| Change Pokemon action-menu geometry/rows/navigation | `PokemonActionMenuController.cpp` | `pokemon_action_menu_controller_tests`; harness for accept/pointer flow |
| Change item modal pages/rows/navigation | `ItemActionMenuController.cpp` | add focused controller coverage if missing; harness for item player flow |
| Change held Pokemon semantics | `PokemonMoveController.cpp`, then screen application path | focused move/controller tests where possible; `transfer_system_flow_harness_tests` for wiring |
| Change multi-Pokemon selection or layout-preserving placement | `MultiPokemonMoveController.cpp`, then screen application path | `multi_pokemon_move_controller_tests`; `transfer_system_flow_harness_tests` for drag/drop wiring |
| Change held item or held Box Space object semantics | `HeldMoveController.cpp`, gesture helpers, then screen application path | focused tests where possible; `transfer_system_flow_harness_tests` |
| Change lower-banner data fields, labels, fallback text, or context behavior | `TransferInfoBannerPresenter.cpp` | `transfer_info_banner_presenter_tests`; harness for focus/hover surfacing |
| Change lower-banner visuals, fitting, generated icons, or icon drawing | `TransferInfoBannerRenderer.cpp` | presenter tests if data changes; harness/render-sensitive flow if visible |
| Change transfer chrome draw order, dropdown/menu drawing, held-object drawing | `TransferSystemRenderer.cpp` | relevant focused test plus `transfer_system_flow_harness_tests` |
| Change box grid chrome or slot rendering model | `BoxViewport.cpp` / `BoxViewportModel` | focused widget/model tests if added; harness if visible |
| Change save-derived Pokemon/box data | `SaveLibrary.cpp`, `TransferSelectionBuilder.cpp`, `PcSlotSpecies` | `save_library_cache_tests`, `transfer_selection_builder_tests`, bridge tests if output changes |
| Add durable Resort storage behavior | backend services under `src/resort`, not UI arrays | backend tests first; integration plan before UI mutation |

## Anti-Sprawl Checklist

Before adding state or branches to `TransferSystemScreen.cpp`, ask:

- Is this just a tuneable value? Put it in `game_transfer.json` and parse it in `GameTransferConfig.cpp`.
- Is this a pure rule? Put it in a controller or presenter.
- Is this keyboard/controller topology? Put it in `TransferSystemFocusGraph`.
- Is this temporary held-object state? Put it in `PokemonMoveController`, `MultiPokemonMoveController`, or `HeldMoveController`.
- Is this render-only chrome? Put it in `TransferSystemRenderer`, `TransferInfoBannerRenderer`, or `BoxViewport`.
- Is this bridge/save data? Keep it in `SaveLibrary`, `TransferSelectionBuilder`, or a backend adapter.
- Is this durable Pokemon/storage state? Design it through the Resort backend rather than mutating UI arrays as the source of truth.
- Does it need player-visible coverage? Add the narrowest focused test first, then one harness assertion when SDL wiring matters.

If the honest answer is still “this belongs in `TransferSystemScreen.cpp`,” keep the code as adaptation glue: translate current runtime geometry/input/state into calls to the owning seam, then return.

## Current Prototype Boundaries

The transfer system currently mixes real external-save data with prototype in-memory UI state:

- external game boxes come from parsed bridge data through `SaveLibrary` and `TransferSelectionBuilder`
- transfer screen game/resort slots are updated in memory while the screen is active
- held Pokemon/item movement is not persisted
- multi-Pokemon movement preserves layout for direct slot drops and uses first-empty order for Box Space quick drops
- item put-away and durable bag/deposit semantics are not complete
- canonical Resort storage exists, but the transfer screen does not yet render or mutate it as the source of truth
- bridge write-projection validates input but intentionally does not mutate external saves

Do not obscure these boundaries. If a change makes movement persistent, imports Pokemon into Resort storage, writes to external saves, or syncs bag/deposit state, document the new source of truth and update architecture/backend docs in the same change.

## Config Boundary

[`docs/config/game_transfer.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/config/game_transfer.md) is the config reference for this screen.

Good config additions:

- field positions, rows, columns, colors, font sizes, alpha, border thickness
- animation durations or smoothing constants
- asset paths for chrome/tool icons
- player-facing tooltip copy
- simple behavior toggles that still leave semantics in controllers

Poor config additions:

- save mutation rules
- bridge JSON compatibility logic
- persistent storage policy
- ad hoc slot identity or movement state
- controller state that must be reasoned about across frames

## Input And Focus Rules

Keyboard/controller input should move through `InputRouter` into `ScreenInput`, then through transfer-system focus/controller seams.

- Directional focus belongs in `TransferSystemFocusGraph`.
- `FocusManager` owns current focus and activation.
- Screen code may rebuild focus nodes from current geometry, but should not become the place where topology rules live.
- Pointer behavior may require screen hit testing, but durable decisions should still route into controllers.
- Focused valid Pokemon slots and game icons should show speech bubbles; mouse-only visibility remains hover-driven.

## Movement Rules

Temporary movement is split by kind:

- normal Pokemon action-menu moves and swap-tool Pokemon moves use `PokemonMoveController`
- multi-tool group movement uses `MultiPokemonMoveController`
- generic held Pokemon/Box Space box/item state uses `HeldMoveController`
- hold/drag timing helpers live in `move/Gestures.hpp`
- screen code applies successful moves to current slot arrays and refreshes view models/textures

Current movement is intentionally non-persistent. If a future change crosses into persistence, stop and design the backend/write-back boundary first.

## Banner Rules

The lower info banner has two separate responsibilities:

- `TransferInfoBannerPresenter` decides what fields mean for contexts such as Pokemon, empty slot, game icon, tool, pill, and Box Space.
- `TransferInfoBannerRenderer` draws those fields according to `game_transfer.json -> info_banner`.

Add new data fields in the presenter first. Add visual support in the renderer only when the existing field types/layout model cannot express the design.

## Renderer Rules

Prefer render helpers over screen growth:

- `TransferSystemRenderer` for screen-level chrome and draw order
- `TransferInfoBannerRenderer` for banner visuals
- `BoxViewport` for grid/box chrome

Some cursor, speech-bubble, mini-preview, and geometry adaptation still lives in `TransferSystemScreen.cpp`. Those are good future extraction candidates when changing nearby behavior and adding harness coverage.

## Testing Rules

Use [`tests/README.md`](/Users/vanta/Desktop/title_screen_demo/tests/README.md) as the canonical test map.

For transfer-system changes, the usual order is:

1. Add or update the narrowest pure test for the owning seam.
2. Add or update a config parsing test when JSON changes.
3. Add one SDL harness assertion when player-visible wiring matters.
4. Run the focused target(s).
5. Run the full native CTest suite before finishing behavior work.

Common targets:

- `game_transfer_config_tests`
- `transfer_info_banner_presenter_tests`
- `transfer_system_ui_state_controller_tests`
- `game_box_browser_controller_tests`
- `pokemon_action_menu_controller_tests`
- `multi_pokemon_move_controller_tests`
- `transfer_system_focus_graph_tests`
- `transfer_system_flow_harness_tests`

When item behavior expands, add focused item-controller/move tests if the existing suite does not expose the seam clearly enough.

## Documentation Rules

Update this guide when:

- a new transfer-system seam is added
- ownership moves out of or into `TransferSystemScreen.cpp`
- `game_transfer.json` gains a new major authoring concept
- movement becomes persistent
- backend-backed Resort storage replaces any in-memory transfer slot source
- bridge/write-back behavior becomes player-visible from the transfer screen

Keep the guide practical. It should answer “where does this change belong?” faster than reading the whole screen file.
