# Pokemon Summary Work Area

This folder owns the transfer-system Pokemon Summary feature. Keep new Summary-specific code here unless it is truly shared transfer infrastructure.

## Current State

The Summary panel is a Basic-tool action-menu shell, not the final full Summary UI yet.

- `PokemonSummaryConfig.cpp` parses [`config/pokemon_summary.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/pokemon_summary.json).
- `TransferSystemSummaryPanel.cpp` owns panel open/close state, side selection, slide motion, one-sided rounded chrome, pointer behavior, keyboard focus sync, and the selected slot reference.
- `PokemonSummaryContent.cpp` is temporary content only. It draws the selected Pokemon name so click/key selection updates can be verified before the real UI sections exist.

The panel replaces the box opposite the currently summarized slot:

- Game-save box selection opens Summary from the left edge and retracts the Resort box.
- Resort box selection opens Summary from the right edge and retracts the game-save box.
- Empty slots keep Summary open but clear the content model.
- Moving focus to the other box flips Summary to the opposite side.
- Clicking inside Summary is consumed for future Summary controls.
- Clicking top chrome, such as the tool carousel, Pokemon/Items pill, or exit button, closes Summary and lets the original click continue.

The screen-edge side has square corners by design. Only the free edge uses `panel.corner_radius`.

## Integration Points

Summary is intentionally isolated, but a few transfer-screen shards call into it:

- [`TransferSystemScreenActionMenuHelpers.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/TransferSystemScreenActionMenuHelpers.cpp) opens Summary from the Basic-tool action menu.
- [`TransferSystemScreenPointerPressed.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/TransferSystemScreenPointerPressed.cpp) gives Summary first chance while it is open, but allows top chrome pass-through.
- [`TransferSystemScreenNavigate2d.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/TransferSystemScreenNavigate2d.cpp) syncs Summary data after keyboard/controller focus moves.
- [`TransferSystemScreenUpdateSync.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/TransferSystemScreenUpdateSync.cpp) retracts the opposite box using `pokemon_summary_reveal_`.
- [`TransferSystemRendererMain.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/TransferSystemRendererMain.cpp) draws Summary before the box viewports so the retracting box visually slides away over it.
- [`TransferSystemScreenMiniPreview.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/TransferSystemScreenMiniPreview.cpp) hides the mini preview while Summary is visible.

## Config Contract

Use [`config/pokemon_summary.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/pokemon_summary.json) for authored Summary dimensions, motion, chrome, colors, and future Summary layout values.

Current important panel fields:

- `width`: `650`
- `height`: `577`
- `top_y`: `100`
- `corner_radius`: free-edge radius only
- `border_thickness`, `fill_color`, `border_color`
- `enter_smoothing`, `exit_smoothing`, `retracted_box_smoothing`
- `temporary_name_font_pt`, `temporary_name_color`: placeholder-only fields

Keep data-driven values in config. Keep Pokemon data mapping in small presenter/content modules, not in raw draw code.

## Next Steps

1. Replace `PokemonSummaryContent.cpp` placeholder rendering with real feature modules under this folder.
2. Add one subfolder per Summary section as it grows, for example `identity/`, `stats/`, `moves/`, or `ribbons/`.
3. Move each section through a small model/presenter step before drawing. The renderer should receive already-resolved display text, colors, icons, and visibility flags.
4. Expand `pokemon_summary.json` as layout sections are designed. Update [`docs/config/pokemon_summary.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/config/pokemon_summary.md) with every new authored field.
5. Add focused tests when behavior stabilizes. Start with `pokemon_summary_config_tests` for config fields, then use `transfer_system_flow_harness_tests` for visible open/close/focus behavior.

## Guardrails

- Keep files short. Split by feature before a file approaches 500 lines.
- Do not put unrelated transfer-screen behavior in this folder.
- Do not bury Summary layout constants in render code.
- Preserve the click-through behavior for top chrome; users should never need a second click after Summary closes.
- Preserve empty-slot behavior; empty slots clear Summary data without closing the panel.
