# `game_transfer.json`

[`config/game_transfer.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/game_transfer.json) is the authoring file for the post-ticket transfer system screen. It is parsed by [`GameTransferConfig.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/GameTransferConfig.cpp) and consumed by [`TransferSystemScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TransferSystemScreen.cpp), transfer-system controllers, and render helpers.

Use this file for transfer-system layout, visual style, animation timing, labels, icon field placement, and tuneable interaction constants. Do not use it for persistent Pokemon state, bridge parsing, save mutation, or controller state-machine rules.

## Ownership Boundary

Config owns:

- positions, sizes, colors, fonts, alpha, smoothing, and timing values
- asset paths for transfer-system chrome and tool icons
- lower info-banner field layout and context-specific tooltip copy
- action-menu visual style and simple behavior toggles
- Box Space hold durations and preview sizing

Runtime code owns:

- which screen or mode is active
- which slot/control is focused
- what happens when the player accepts, backs out, drags, drops, swaps, or cancels
- how parsed `PcSlotSpecies` data is transformed into visible banner values
- persistence and future write-back behavior

Persisted data owns:

- user/profile state that survives restarts
- future durable Resort storage or external-save write-back state

## Major Sections

### `background_animation`

Controls the animated transfer background texture scale and scroll speed. This is visual only.

### `fade`

Controls transfer-system enter/exit fade durations. Flow decisions still live in `TransferSystemUiStateController` and `TransferFlowController`.

### `box_viewport`

Controls shared game/resort box viewport presentation:

- arrow texture and colors
- box-name and Box Space text style
- footer arrow offset
- content slide smoothing
- Pokemon sprite scale/offset
- held-item overlay size and tint under `item_tool`
- Box Space preview sprite scale/offset under `box_space_sprites`

Pokemon and item textures should still resolve through `PokeSpriteAssets`.

### `mini_preview`

Controls the Box Space mini-preview panel: enable flag, dimensions, corner/border style, screen offsets, smoothing, and preview sprite scale.

The mini preview is a render contract. Runtime code decides when it appears and what box it previews.

### `box_space_long_press`

Controls hold durations for Box Space interactions:

- `box_swap_hold_seconds`
- `quick_drop_hold_seconds`

Gesture state and cancellation thresholds live in transfer-system move/gesture code and `TransferSystemScreen` adaptation.

### `pokemon_action_menu`

Controls visual style and selected behavior toggles for the normal Pokemon action menu and related held-sprite drawing:

- modal width/row sizing/padding
- colors, alpha, borders, and font size
- placement gap and animation smoothing
- background dimming for non-selected sprites
- occupied-drop behavior toggles such as `modal_move_swaps_into_hand` and `swap_tool_swaps_into_hand`
- held Pokemon shadow style and held sprite scale

Menu state, row hit testing, and row selection live in `PokemonActionMenuController`. Applying moves to slots lives in runtime code.

### `info_banner`

Controls the lower info banner and is intentionally data-rich. It contains:

- banner height, separator style, background color
- default text style
- generated gender symbol style
- icon directories and unknown icon fallback
- tooltip title/body copy for tool, pill, and Box Space contexts
- `layout` rows and columns for each context family
- ordered `fields` describing which data appears in which contexts

Banner field mapping belongs in `TransferInfoBannerPresenter`. Banner drawing belongs in `TransferInfoBannerRenderer`.

#### Banner Contexts

Supported contexts include:

- `pokemon`: occupied or focused Pokemon slot
- `empty`: empty slot placeholder fields
- `game_icon`: selected external save summary
- `tool`: current tool carousel item
- `pill`: Pokemon/item storage pill toggle
- `box_space`: Box Space mode

Fields can reuse layouts by setting `layout`, as the tooltip fields do with the `game_icon` layout.

#### Banner Icons

Icon fields can draw from:

- parsed Pokemon data, such as ball, held item, type, shiny, Pokerus, and markings
- PokeSprite item icons for held items
- PokeSprite misc icons for status/type/marking-style groups
- configured game icon directories

Do not add manual icon path building in screen code. Extend `PokeSpriteAssets` or the banner presenter/renderer when a new icon source is needed.

### `pill_toggle`

Controls the Pokemon Storage / Bag Storage pill track, sizing, colors, font, and animation smoothing. Runtime code owns what each mode actually enables.

### `tool_carousel`

Controls the tool carousel viewport, hidden/rest positions, icon sizes, selector style, slide behavior, texture paths, and per-tool frame colors.

Tool semantics live in transfer-system input/controller code:

- Multiple/Drag Zone uses drag selection plus layout-preserving multi-Pokemon movement
- Basic Move uses normal Pokemon action-menu move behavior
- Swap Pokemon uses held Pokemon move behavior
- Held Items opens item action behavior

### `box_name_dropdown`

Controls the game box dropdown chrome, visible row capacity, font, colors, animation smoothing, margins, and scroll sensitivity.

Dropdown open/close state, highlighted row, selected box, and scrolling rules live in `GameBoxBrowserController`.

### `selection_cursor`

Controls focused selection cursor style and the speech bubble configuration:

- cursor color, alpha, border, padding, minimum size, corner radius, beat animation
- speech-bubble text style, fill/border, padding, sizing, triangle, margins, and label format

Focus navigation belongs in `TransferSystemFocusGraph` and `FocusManager`. Config should only tune the visual shape and label format.

## Authoring Expectations

- Keep all coordinates in the app's `1280x800` logical/design coordinate system.
- Prefer adding new banner fields through `info_banner.fields` plus presenter support instead of special-case drawing.
- Keep tooltip copy in config when it is player-facing and mode-specific.
- Keep animation constants positive and test edge cases such as disabled previews or zero visible dropdown rows.
- When adding new icon fields, decide whether the source is a parsed Pokemon value, a PokeSprite item icon, a PokeSprite misc icon, or a game-specific configured path.
- When adding item-tool behavior, document whether it is temporary in-memory state, backend-persisted Resort state, or external-save write-back.

## Tests To Update

Use the narrowest useful test first:

- `game_transfer_config_tests` for new parsed fields/defaults.
- `transfer_info_banner_presenter_tests` for new banner data mapping.
- `transfer_system_ui_state_controller_tests` for top-level UI state or animation rules.
- `game_box_browser_controller_tests` for box/dropdown/Box Space behavior.
- `pokemon_action_menu_controller_tests` for Pokemon modal geometry and row behavior.
- `transfer_system_focus_graph_tests` for keyboard/controller navigation topology.
- `transfer_system_flow_harness_tests` for SDL-wired player flows such as focus-driven speech bubbles, Box Space activation, item movement, or pointer/keyboard integration.

Run the full native CTest suite before finishing behavior changes.
