# Loading Screens

Loading screens are created through `createLoadingScreen` in `ui/loading/LoadingScreenFactory.hpp`.
Keep loading-screen configuration in `config/loading_screen.json`.

## Available Types

- `LoadingScreenType::Pokeball` creates the black rotating Pokeball loading screen.
- `LoadingScreenType::ResortTransfer` creates the boat/resort transfer loading screen.
- `LoadingScreenType::QuickBoatPass` uses the boat loading screen as a no-stop transition.

Use the Pokeball screen for general async loading:

```cpp
auto loading_screen = createLoadingScreen(
    LoadingScreenType::Pokeball,
    renderer,
    window_config,
    fallback_font_path,
    project_root);
loading_screen->enter();
```

Use the boat screen for resort or transfer travel:

```cpp
auto loading_screen = createLoadingScreen(
    LoadingScreenType::ResortTransfer,
    renderer,
    window_config,
    fallback_font_path,
    project_root);
loading_screen->enterWithMessageKey("message_transport_pokemon");
```

`enterWithMessageKey` calls `setLoadingMessageKey` and then `enter`. Use it for interactive/test flows that should wait for player input.

Use the quick boat pass for quick predictable transitions where loading has already effectively finished:

```cpp
auto loading_screen = createLoadingScreen(
    LoadingScreenType::QuickBoatPass,
    renderer,
    window_config,
    fallback_font_path,
    project_root);
loading_screen->beginQuickPass();
```

If a quick transition has a small amount of work that might run longer than expected, start the animation immediately and wait on the ocean view after the boat leaves:

```cpp
loading_screen->beginQuickPass(true);
startAsyncWork();

// Later, when work is done:
loading_screen->markLoadingComplete();
```

The player should never sit on a blank pre-animation page for quick pass. The boat begins right away, and if work is still pending after the boat exits, waves/clouds/sun keep animating until `markLoadingComplete()` lets the ocean exit.

For real loading work, use `beginLoadingWithMessageKey` and signal completion when the work finishes:

```cpp
loading_screen->beginLoadingWithMessageKey("message_transport_pokemon");

// Later, when async work is done:
loading_screen->markLoadingComplete();

// Keep rendering/updating the loading screen until this is true,
// then switch to the destination screen.
if (loading_screen->isLoadingAnimationComplete()) {
    openDestinationScreen();
}
```

The optional second argument overrides `resort_transfer.minimum_loop_seconds`. The config default is currently `1.0`, so omitted calls spend at least one second in the middle/loading loop after the intro finishes. Pass `0.0` only when a caller intentionally wants the boat loading screen to skip that middle hold once work is done.

For the Resort transfer boat screen, a load that completes before the boat reaches the middle skips the loading-loop pause and suppresses the message. If a non-zero minimum is supplied, the boat reaches the middle, shows the message, and holds for that minimum duration.

The Pokeball screen has no intro/middle/outro staging, but it honors the same minimum duration before `isLoadingAnimationComplete()` becomes true. It ignores message keys.

## Resort Transfer Text

The Resort transfer screen reads its message settings from `resort_transfer.message` in `config/loading_screen.json`.

Add new messages under `texts`:

```json
"texts": {
  "message_transport_pokemon": "Transporting\nPokemon...",
  "message_opening_resort": "Opening\nResort..."
}
```

Then call:

```cpp
loading_screen->beginLoadingWithMessageKey("message_opening_resort", 1.5);
```

If a key is missing, the screen falls back to `default_key`, then to the first configured text.

## Disabling Text

To disable Resort transfer text, set the selected text to an empty string:

```json
"texts": {
  "message_transport_pokemon": ""
}
```

To keep the first idle page blank, leave `show_in_idle` as `false`. Setting it to `true` draws the configured message on the idle page too.

## Text Styling And Motion

These `resort_transfer.message` fields control the message without code changes:

- `font`: message font path, currently `assets/fonts/loading_card.TTF`
- `font_size`: message size
- `color`: `#RRGGBB` or `#RRGGBBAA`
- `center_x_ratio`: horizontal center as a viewport-width ratio
- `center_bottom_offset`: vertical center measured from the bottom of the viewport
- `line_spacing`: extra pixels between lines
- `max_width_ratio`: max texture width as a viewport-width ratio
- `intro`: fade/float-in stage (`start`, `duration`, `ease`)
- `outro`: fade-out stage (`start`, `duration`, `ease`)
- `enter_y_offset`: starting downward offset before the intro finishes
- `default_key`: fallback text key

Keep new loading-screen implementations in `include/ui/loading` and `src/ui/loading`, then add them to the factory rather than constructing them directly in app flow code.

## Temporary Trade Demo

Until Trade has a real destination, the Trade menu button uses a temporary loading preset from `resort_transfer.temporal.trade_button`:

```json
"temporal": {
  "trade_button": {
    "loading_type": "quick_boat_pass",
    "message_key": "message_transport_pokemon",
    "simulated_load_duration": 0
  }
}
```

`loading_type` selects what the Trade button is testing: `pokeball`, `quick_boat_pass`, or `boat_loading`. `message_key` selects the configured message for boat loading. `simulated_load_duration` is only the fake work duration for this temporary test hook.

Quick boat pass movement is reusable loading-screen config, not temporal test data:

```json
"quick_pass": {
  "duration": 1.65
}
```

Use `resort_transfer.quick_pass` to tune the no-stop boat transition speed and staging.

Normal boat loading minimum-loop time is reusable loading-screen config too:

```json
"minimum_loop_seconds": 1
```

Callers can override it with `beginLoadingWithMessageKey("message_transport_pokemon", seconds)`. Use omitted/default for the authored global value, `0.0` for no middle hold, or a larger value for flows that need a longer readable loading beat.

## Screen-To-Screen Flow

For a real flow, create the chosen loading screen through the factory, set it as the active screen, start the destination work, and keep updating/rendering the loading screen. When the work finishes, call `markLoadingComplete()`. Switch to the destination screen only after `isLoadingAnimationComplete()` returns true.

Use these choices:

- `Pokeball`: general loading with the black rotating Pokeball.
- `QuickBoatPass`: fast transition. Call `beginQuickPass()` when work is already done, or `beginQuickPass(true)` before work starts and `markLoadingComplete()` when it finishes.
- `ResortTransfer`: full boat loading. Call `beginLoadingWithMessageKey("message_transport_pokemon")`, then `markLoadingComplete()` when work finishes.
