# `transfer_select_save.json`

[`config/transfer_select_save.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/transfer_select_save.json) authors the transfer-ticket selection screen shown after TRANSFER is chosen and save probing completes.

The screen renders in the app's `1280x800` logical/design coordinate system. Ticket text offsets are local to the ticket art, so ticket internals can stay stable even if the full-screen header, list viewport, or background animation changes.

## Major Sections

- `transfer_screen.fade`: fade-in and fade-to-black timing.
- `transfer_screen.header`: title/subtitle text, center points, font sizes, and colors.
- `transfer_screen.list`: first ticket position, vertical separation, scroll speed, and viewport clipping rectangle.
- `transfer_screen.audio`: transfer lobby music and fade/silence timing.
- `transfer_screen.background_animation`: shared scrolling background tuning.
- `ticket.font_sizes`: text sizing for ticket labels and values.
- `ticket.layout`: ticket-local positions for boarding pass text, game title, trainer name, party sprites, and stats.
- `ticket.selection`: selected-ticket border style and beat animation.
- `ticket.rip_animation`: ticket activation tug/rip timing, distance, rotation, and pivot.
- `palette.game_colors`: stable game-id to color mapping used for ticket theming.

## Runtime Boundaries

Config owns visual placement, colors, text sizes, animation timing, and palette values.

Runtime code owns:

- save scanning and bridge probing in `SaveLibrary`
- conversion from save summaries to `TransferSaveSelection` in `TransferSelectionBuilder`
- ticket list state, selection, scroll, drag/click, rip activation, and handoff timing in `TransferTicketListController`
- rendering and pointer-to-view adaptation in `TransferTicketScreen`

Do not parse bridge JSON or inspect raw save files from the ticket screen.

## Tests To Update

- `transfer_ticket_list_tests` for list/selection/rip controller behavior.
- `transfer_selection_builder_tests` for save-summary to UI-selection mapping.
- `transfer_ticket_unicode_harness_tests` when changing transfer-ticket text/font paths.
- `transfer_system_flow_harness_tests` only when the ticket change affects handoff into the transfer system.
