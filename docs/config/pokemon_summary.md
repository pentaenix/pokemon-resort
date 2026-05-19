# `pokemon_summary.json`

[`config/pokemon_summary.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/pokemon_summary.json) owns the Pokemon Summary panel used from the transfer-system Basic tool.

The Summary panel replaces the opposite box module instead of opening as a global modal:

- focusing a Pokemon in the right game-save box retracts the left Resort box and slides Summary in from the left screen edge
- focusing a Pokemon in the left Resort box retracts the right game-save box and slides Summary in from the right screen edge
- future single-box screens can keep Summary open by default via `open_when_game_box_absent`

## Major Sections

- `panel.enabled`: allows the feature shell to be turned off without removing the action-menu row.
- `panel.enter_smoothing` / `panel.exit_smoothing`: exponential motion tuning for the Summary panel.
- `panel.retracted_box_smoothing`: snappier exponential motion tuning while the opposite box retracts or returns.
- `panel.width`: panel width. Current design target is `650`.
- `panel.height` and `panel.top_y`: match the transfer box module height and vertical position.
- `panel.corner_radius`, `panel.border_thickness`, `panel.fill_color`, `panel.border_color`: Summary chrome styling. Colors may use tokens from `design.json`.
- `panel.open_when_game_box_absent`: future single-box flow hint; Summary should be always open when no opposite game box module exists.
- `panel.temporary_name_font_pt`, `panel.temporary_name_color`: temporary placeholder text style while real Summary content modules are being built.

## Runtime Boundaries

Config owns Summary panel size, edge motion tuning, and chrome style.

Runtime code owns:

- which Pokemon is being summarized
- which side Summary replaces
- back/cancel behavior
- Summary content, tabs, and data mapping
- source-box interactivity while Summary is open

The edge that touches the screen is intentionally square, while the free edge keeps the configured radius. This makes Summary read as a panel coming from the screen edge instead of a floating modal.

While Summary is open, clicking or navigating to another box slot keeps Summary open and swaps the selected slot. Empty slots clear Summary data without closing the panel. Moving from one box module to the other flips Summary to the opposite side.

The current content renderer is intentionally temporary and lives in [`PokemonSummaryContent.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/summary/PokemonSummaryContent.cpp). It only draws the selected Pokemon name so selection updates can be verified before the real Summary features land. The implementation handoff lives beside it in [`summary/README.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/summary/README.md).

When content is added, keep authored positions, labels, colors, and tab layout in this file. Keep Pokemon data mapping in focused presenter/controller code rather than in render glue.
