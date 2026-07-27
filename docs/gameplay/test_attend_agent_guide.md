# TEST ATTEND Agent Development Guide

This guide is the routing contract for AI-assisted work on the Pokemon Attend scene. Use it before editing Attend code, config, tests, or the admin metadata that edits Attend config.

## Read First

Read these files in order for Attend work:

1. `AGENTS.md`
2. `docs/agents/agent-playbook.md`
3. `docs/gameplay/test_attend.md`
4. `docs/gameplay/test_attend_animation_semantics.md` when touching animations, eyes, mouth frames, sleep, wake, emotes, or pet reactions.
5. This file.

If the task touches `pokemon-resort-page/tools/admin/modules/dataeditor/modifiers/gameplay/pokemon_attend/`, also read `../pokemon-resort-page/docs/AGENTS.md`.

## Source Of Truth

Attend config is split by concern. Do not add a value wherever it is convenient.

| Concern | Runtime config | Admin metadata mirror | Runtime parser / struct |
|---|---|---|---|
| Temporary debug selections, active Pokemon, active map, debug buttons | `config/gameplay/pokemon_attend/debug/alola_battle_map_debug.json` | `pokemon-resort-page/tools/admin/modules/dataeditor/modifiers/gameplay/pokemon_attend/debug/alola_battle_map_debug.json` | `include/gameplay/attend/AttendSceneConfig.hpp`, `src/gameplay/attend/AttendSceneConfig.cpp` |
| Camera framing, freecam, pointer, hand cursor, pet detection tuning | `config/gameplay/pokemon_attend/interaction/defaults.json` | `pokemon-resort-page/tools/admin/modules/dataeditor/modifiers/gameplay/pokemon_attend/interaction/defaults.json` | same config parser |
| Pokemon provider defaults, model discovery, scale/yaw, sparse species/form overrides, idle/sleep behavior, semantic animation slots | `config/gameplay/pokemon_attend/providers/gen1_7.json` | `pokemon-resort-page/tools/admin/modules/dataeditor/modifiers/gameplay/pokemon_attend/providers/gen1_7.json` | same config parser |
| Alola map catalog, floor extensions, map placement, sky presets, light, weather material modes, depth of field, shadow | `config/gameplay/pokemon_attend/environments/alola_battle_maps.json` | `pokemon-resort-page/tools/admin/modules/dataeditor/modifiers/gameplay/pokemon_attend/environments/alola_battle_maps.json` | same config parser |
| Attend normal UI, debug UI, corner buttons, profile plate, screen-level HD/SD render mode | `config/gameplay/pokemon_attend/ui/defaults.json` | `pokemon-resort-page/tools/admin/modules/dataeditor/modifiers/gameplay/pokemon_attend/ui/defaults.json` | same config parser |
| Attend audio timing | `config/gameplay/pokemon_attend/audio/defaults.json` | `pokemon-resort-page/tools/admin/modules/dataeditor/modifiers/gameplay/pokemon_attend/audio/defaults.json` | same config parser |
| Scene pixel density shared with overworld | `config/gameplay/world3d/render.json` `worldViewport` | none unless explicitly added later | overworld/render config, not Attend UI config |

Rules:

- If a field is author-facing, add it to both runtime config and admin metadata in the same change.
- If a field controls behavior, parse it into `AttendSceneConfig` and cover it in `attend_scene_config_tests`.
- Keep debug config temporary. Do not put lasting camera, pointer, provider, environment, or UI rules under `debug`.
- Keep provider-specific Pokemon exceptions sparse. Prefer provider defaults and semantic adapters over species hacks.

## Module Map

| Area | Files | Ownership |
|---|---|---|
| Screen state and input | `src/ui/AttendTestScreen.cpp`, `src/ui/attend/AttendOverlay.cpp` | High-level scene state, debug controls, input routing, overlay hit testing. Avoid renderer math here. |
| Config contract | `include/gameplay/attend/AttendSceneConfig.hpp`, `src/gameplay/attend/AttendSceneConfig.cpp` | JSON defaults, parsing, schema compatibility, field naming. |
| Renderer public API | `include/gameplay/attend/rendering/AttendBgfxRenderer.hpp` | Small public surface for the screen. |
| Renderer shared state | `src/gameplay/attend/rendering/AttendBgfxRendererInternal.hpp` | Internal renderer fields and helper declarations. Keep this organized; split when it grows. |
| Frame orchestration | `src/gameplay/attend/rendering/AttendBgfxRendererRender.cpp`, `AttendBgfxRendererSubmit.cpp` | View setup, render order, scene/UI composition. |
| Camera and framing | `AttendBgfxRendererCamera.cpp` | Auto-focus, full-body view, face view, freecam, stable camera target. |
| Pokemon model build | `AttendBgfxRendererPokemonBuild.cpp`, `AttendPokemonModel.cpp` | GLB loading, RAE metadata, variants, material policy, animation data. |
| Animation and interaction semantics | `AttendBgfxRendererAnimation.cpp` | Idle, emote, sleep/wake, pet reactions, expression state. |
| Scene/environment draw | `AttendBgfxRendererScene.cpp`, `AttendBgfxRendererCommon.cpp` | Floor, sky, light, environment textures, common helpers. |
| Normal UI draw | `AttendBgfxRendererCornerButton.cpp`, `AttendBgfxRendererProfilePlate.cpp`, `AttendBgfxRendererUi.cpp` | Corner buttons, profile plate, debug button surfaces, UI texture helpers. |

Do not let one renderer file become the dumping ground. If a file is approaching 500 lines, split by concern before adding another feature.

## Common Tasks

| Task | Change here | Do not change |
|---|---|---|
| Move Pokemon closer/farther in normal view | `interaction/defaults.json` `camera.fullBodyFraming.distanceMultiplier` | Per-species scale unless only one Pokemon/form is wrong |
| Fix one Pokemon/form face or body framing | provider sparse override in `providers/gen1_7.json` | Global camera math if other Pokemon are correct |
| Add a map or map extension | `environments/alola_battle_maps.json` floor catalog / `extensions` | Renderer hard-coded asset paths. Do not copy `modelPlacement` onto each new floor; leave offsets at zero so Alola map placement stays shared |
| Change sky, light, weather, blur, shadow | `environments/alola_battle_maps.json` | Pokemon material hacks |
| Tune hand graphic size | `interaction/defaults.json` hand visual scale | Pet detection bounds |
| Tune pet detection | interaction config and renderer hit/projection logic | Hand graphic scale |
| Add or rename semantic animation behavior | `providers/gen1_7.json` `interactionAdapter` and `docs/gameplay/test_attend_animation_semantics.md` | Raw slot checks inside screen input |
| Change UI colors, button scale, icon offsets, profile plate text | `ui/defaults.json` and admin metadata | Hard-coded bgfx draw constants |
| Change UI HD/SD rendering | `ui/defaults.json` `rendering.normalUi` or `rendering.debugUi` | Per-button or per-icon render mode fields |
| Change entire scene pixel density | `config/gameplay/world3d/render.json` `worldViewport` | Attend-only fake downscale/upscale paths |

## UI Rendering Rules

Attend has two UI screens:

- `normalUi`: player-facing Attend UI, including corner buttons and profile plate.
- `debugUi`: debug overlay buttons used by TEST ATTEND.

Each screen has one render mode: `hd` or `sd`. Default is `hd`.

Do not reintroduce per-element, per-button, or per-icon render modes. They made scaling and compositing unpredictable. If future work needs mixed rendering, add a short design note first and keep the default path screen-level.

When editing UI:

- Keep sizing relative to the visible world viewport, not the raw SDL window border.
- Keep screen-level UI placement stable across window sizes.
- Text should render in front of banners.
- Pixel-art text and icons may use HD UI rendering for readability.
- Button icon scale and button body scale are separate tuning values, but render mode is not separate per icon.
- Transparent icon edges should not softly brighten the button. Prefer explicit mask compositing for white DS-style icons.

## Animation And Interaction Rules

Gameplay asks for semantic actions, not raw exported slots.

Use semantic names such as:

- `idle_default`
- `emote_vocal`
- `emote_happy`
- `sleep_start`
- `sleep_loop`
- `pet_happy`

Rules:

- If a Pokemon does not have the requested animation, the action should silently do nothing or fall back through configured candidates.
- Idle random emotes must not wake sleeping Pokemon.
- Hovering must not wake sleeping Pokemon.
- Clicking or petting wakes sleeping Pokemon only if the Pokemon actually supports sleep behavior.
- While asleep, disable head mouse tracking.
- Do not allow petting until wake-up is complete.
- Wake-up should reverse `sleep_start` when present.
- Sleep and wake expression timing should preserve the authored sequence: sleep lowers head before eyes close; wake raises head before eyes open.

## Camera And Framing Rules

- Full-body camera framing should use a stable setup-pose target so idle animation bobbing does not move the camera.
- Pointer hit detection may use animated projected bounds; camera target should not follow every animation frame.
- Face mode is for large Pokemon only, gated by config.
- Entering and exiting face mode should be a smooth camera transition, not a jump.
- Double-click enter should require the face/head hit region; double-click exit can happen anywhere while in face mode.
- Keep camera clipping, distance, and culling tunable through interaction config.

## Config Naming Rules

Use names that describe the thing being moved or scaled.

Good:

- `cornerButtons.left.buttonScale`
- `cornerButtons.left.iconOffsetX`
- `profilePlate.rowGap`
- `camera.fullBodyFraming.distanceMultiplier`
- `floorDefaults.modelPlacement.scale`

Bad:

- `scale`
- `height`
- `offset`
- `x`
- `renderMode` inside every button

Short field names like `x`, `y`, `z`, and `scale` are acceptable only inside an already specific object such as `modelPlacement` or `position`.

## Admin Metadata Rules

The admin tool is for humans tuning values. It must make the target obvious.

- Group fields by feature, not by raw JSON file when that makes the UI unusable.
- Use clear labels such as `Left corner button scale`, not `scale`.
- Put rare or dangerous values under collapsed advanced groups.
- Preserve the three-column layout for corner button editing unless the user asks for a different layout.
- Add enum/select controls for constrained values such as `hd`/`sd`.
- Runtime config and admin metadata must stay in sync.

## Tests To Run

For most Attend config or renderer changes:

```bash
cmake --build /Users/vanta/Desktop/title_screen_demo/pokemon-resort/build --target title_screen_demo
cmake --build /Users/vanta/Desktop/title_screen_demo/pokemon-resort/build --target attend_scene_config_tests
ctest --test-dir /Users/vanta/Desktop/title_screen_demo/pokemon-resort/build -R attend_scene_config_tests --output-on-failure
```

For Pokemon GLB loading, RAE metadata, materials, variants, or animation sampling:

```bash
cmake --build /Users/vanta/Desktop/title_screen_demo/pokemon-resort/build --target attend_pokemon_model_tests
ctest --test-dir /Users/vanta/Desktop/title_screen_demo/pokemon-resort/build -R attend_pokemon_model_tests --output-on-failure
```

Before finishing changes that touch JSON or admin metadata:

```bash
cd /Users/vanta/Desktop/title_screen_demo/pokemon-resort && git diff --check
cd /Users/vanta/Desktop/title_screen_demo/pokemon-resort-page && git diff --check
```

Run the full native suite only when the change crosses app routing, shared overlay, input, assets, or other modules outside Attend.

## Review Checklist

Before finishing:

- The source of truth is in the right config file.
- Runtime config and admin metadata match.
- New fields have defaults and parser coverage.
- No per-species hack was added for a provider-wide issue.
- No per-button/per-icon UI render mode was reintroduced.
- UI placement uses visible viewport coordinates.
- Camera target does not follow idle animation bobbing.
- Sleep, wake, emote, and petting rules remain semantic.
- Focused tests were run or the reason for not running them is stated.
