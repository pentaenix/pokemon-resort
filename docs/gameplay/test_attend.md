# TEST ATTEND Interaction Debug Scene

`TEST ATTEND` is the first debug version of a future Pokemon Amie-style interaction scene.

For AI-assisted implementation work, read [`test_attend_agent_guide.md`](test_attend_agent_guide.md) first. It defines Attend file ownership, config source of truth, UI render-mode rules, admin metadata expectations, and focused test commands.

## Launch Path

1. Start the app.
2. Open `RESORT` from the title menu.
3. Choose `TEST ATTEND`.
4. Press Back to return to the title Resort flow.

## Current Behavior

- `config/gameplay/pokemon_attend.json` is now a small manifest. It chooses `activeDebug` and links to purpose-built files under `config/gameplay/pokemon_attend/`.
- Debug scene files live under `config/gameplay/pokemon_attend/debug/`: set temporary selections such as `activePokemon`, `activeFloor`, `activeSky`, the debug Pokemon spawn stub, and TEST ATTEND-only overlay buttons there. The demo currently selects `dewgong` so provider-level per-Pokemon animation overrides can be tested immediately.
- Interaction defaults live under `config/gameplay/pokemon_attend/interaction/`: `interaction/defaults.json` owns shared camera framing/freecam, pointer-look, and hand cursor tuning.
- Provider files live under `config/gameplay/pokemon_attend/providers/`: `providers/gen1_7.json` owns model discovery defaults, provider scale/yaw defaults, sparse Pokemon overrides, Gen 1-7 Pokemon model interaction adapter data, and provider-level idle/sleep behavior. Per-Pokemon config should only cover exceptions.
- Environment files live under `config/gameplay/pokemon_attend/environments/`: `environments/alola_battle_maps.json` owns ripped Alola battle map catalogs, map extension pairs, weather material modes, sky/light presets, contact shadow, depth of field, and background color.
- `floorDefaults.modelPlacement` owns map placement inside the environment file: `x`/`y`/`z` move the floor set, `yawDegrees` rotates the map to inspect where the Pokemon spawns, and `scale` keeps the floor and any extension GLBs on the same sizing. Floor entries under `floors` choose the base GLB and may list extension GLBs that inherit placement unless they override `modelPlacement`.
- `skyDefaults`, `skyPresetOrder`, and `skyPresets` own data-driven sky presets inside the environment file. Presets currently include `clear`, `sunset`, `cloudy`, and `night`, and each preset can author both the sky gradient and lighting values used by the Pokemon, floor, and floor extensions.
- `interactionAdapter` owns model-source-specific interaction hints such as semantic head nodes, eyelid matching, eye-close animation fallback, eye and mouth expression frame semantics, pet eye-close delay/cooldown, one `headLookStrength` scalar, and model-space head-look axes.
- It is implemented as `AttendTestScreen`, a standalone `Screen`, with bgfx rendering delegated to `gameplay/attend/rendering/AttendBgfxRenderer`.
- Pokemon models use an attend-only GLB loader that preserves skinning, baked animation channels, sampler wrap, material policy metadata, mesh draw metadata, and provider-specific eye data.
- Combined RAE appearance variants are supported from a single GLB. The loader reads root `extras.rae.appearanceVariants` axes when present, with the older root `extras.rae.textureVariants` kept as a normal/shiny compatibility path. The `texture` axis swaps normal/shiny through each normal material's `extras.rae.shinyMaterialIndex`; the optional `form` axis can swap texture-only form/pattern materials through `extras.rae.formMaterialIndices` or show/hide geometry through node `extras.rae.visibleForForms`. Meshes continue to reference default materials; the renderer resolves active form first, then active texture/color. Old separate `_shiny.glb` sibling files are obsolete for this path.
- Geometry-form RAE exports should include independent skeleton nodes, independent skins, `JOINTS_0`/`WEIGHTS_0`, and animation channels for alternate form mesh nodes. GLBs exported before that metadata existed may show alternate forms as static or misplaced until they are re-exported.
- RAE Gen 1-7/3DS exports are consumed by policy instead of per-Pokemon hacks: `extras.rae.renderClass` drives opaque/mask/blend behavior, mesh-node `renderOrder` and `defaultVisible` drive draw assembly, and `eyeSheet`/`eyeExpression` apply the bind-pose eye UV repeat/offset/mirror needed for base-color eye sheets. These RAE eyes should not use the older Violet-style `.lym`/`emissiveTexture` compositor path.
- Older Violet-style models can still use the `.lym`/`emissiveTexture` eye path when the material genuinely has that emissive mask and no RAE eye-sheet role.
- It has no transfer, save scanning, Resort backend, or overworld dependency.
- The title submenu entry emits `TitleScreenEvent::OpenTestAttendRequested`; `AppScreenCoordinator` owns the route.
- Current GLB animation playback uses the authored `pokemon.animation` field when present, then falls back to model-provided wait/idle/stand/slot animations. Petting reactions layer semantic controls over that base pose through the interaction adapter when compatible eye or eyelid animation data is available.
- Environment scenes can enable `viewportLook`, which lightly nudges the camera look target when the mouse reaches the screen edges. It does not translate the camera, and arrow-key camera movement is intentionally disabled. Press `2` or the top-right `WEATHER` overlay button to cycle configured weather modes. Press `3` or the `VIEW` overlay button below Weather to toggle full-body and face framing only when the active Pokemon is large enough for `interaction.camera.faceFraming.minimumPokemonHeight`. Press `4` or the `POKEMON` overlay button to cycle discovered `.glb` models in `assets/pokemon_attend/pokemon_models`. Press `5` or the `COLOR` overlay button to cycle embedded texture variants such as Normal and Shiny when the current GLB exposes them. Press `6` or the `FORM` overlay button to cycle embedded form/pattern variants when the current GLB exposes a form axis.
- Press `7` or the left-side `TIME` overlay button to cycle the selected sky preset. Press `8` or the `EMOTE` button under Time to trigger the configured idle emote. Press `9` or the `SLEEP` button below Emote to trigger the configured sleep animation. Press `0` or the `CRY` button below Sleep to play the active Pokemon cry directly. These debug animation triggers use semantic animation names and silently do nothing when the active Pokemon GLB does not have a matching slot.
- Press `Q` to toggle the overworld-style free camera. While freecam is active, use WASD or arrow keys to move on the ground plane, Space/Shift to move vertically, and left-drag to rotate. Freecam movement speed, mouse sensitivity, initial offset, and pitch clamp live under `camera.advanced.freeCamera`.
- Environment setup is split into clear controls: debug `pokemon.position` for the temporary Pokemon start, interaction `camera.fullBodyFraming.distanceMultiplier` for the main closer/farther auto-focus tweak, interaction `camera.fullBodyFraming` for full-body culling/depth padding, interaction `camera.faceFraming` for gated face view, interaction `camera.advanced.manualCameraFallback` for non-autofocus fallback placement, interaction `camera.advanced.lens.nearClip`/`farClip` for camera clipping, interaction `viewportLook.maxX`/`maxY`/`edgeMarginRatio`/`smoothSeconds` for weighted edge-look limits, provider `interactionAdapter.headLookStrength` for mouse-tracking intensity, provider `idleBehavior` for automatic no-interaction emote/sleep timing, environment `shadow.strength` plus minimum radii/offset for the simple oval contact shadow, environment `floorDefaults.modelPlacement` for large GLB placement/scale/rotation, floor `extensions` for paired map pieces that share placement, environment `activeSky` for `clear`/`sunset`/`cloudy`/`night` sky and light presets, and environment `depthOfField` for scene focus. Lower `camera.fullBodyFraming.distanceMultiplier` to move the auto-focus camera closer, raise it to move farther. `faceFraming.headTargetHeightRatio` should stay high enough to frame large Pokemon faces rather than necks. `fullBodyFraming.verticalOffset` and `faceFraming.verticalOffset` are maximum vertical offsets; the renderer scales them down for small active models so Eevee-sized Pokemon are not viewed from too high above.
- Provider `idleBehavior.emoteAfterSeconds` is the minimum no-interaction time before random emotes can start. `emoteIntervalMinSeconds`/`emoteIntervalMaxSeconds` add the random delay between idle emotes. `sleepAfterSeconds` triggers the configured sleep semantic once after longer inactivity. The current Gen 1-7 provider uses `emote_vocal` after at least `30s` idle and `sleep_loop` after `180s` idle. While asleep, head mouse tracking is disabled. Clicking or petting wakes the Pokemon; if the active GLB has `sleep_start`, the renderer plays that semantic in reverse as the wake-up animation.
- Attend audio timing lives in `config/gameplay/pokemon_attend/audio/defaults.json`. `petHappyCryDelaySeconds` controls the delay between releasing a successful happy pet and playing the active Pokemon cry; it defaults to `0.2`.
- TEST ATTEND uses the same `config/gameplay/world3d/render.json` `worldViewport` as the overworld for scene pixel density. Keep `baseWidth`/`baseHeight` as the DS-like composition size, and raise `upscale` to `2` for a 2x internal render in both scenes.
- Auto-focus camera framing is captured from the model's initial stable pose. Animated Pokemon bounds still drive pointer hit detection, but idle/wait animation bobbing should not move the camera target up and down.
- Lighting is intentionally stylized instead of physically realistic. Sky presets may provide a `lighting` block; the selected preset applies after scene lighting so sky swaps can change the shared tint, brightness, and Pokemon form light together. Use `lighting.pokemonBrightness` to keep the active Pokemon readable, but keep it close to `lighting.backdropBrightness` so the Pokemon still belongs to the scene. Use `lighting.ambient`, `lighting.directional`, `lighting.formShadow`, and `lighting.lightDirection` for cheap normal-based Pokemon shape lighting: the default light comes from above, adds soft form on white Pokemon, and darkens the lower/right side without shadow maps or extra draw passes. Backdrop, floor, and floor-extension meshes share the same backdrop brightness/saturation/contrast and global tint, keeping paired floor pieces lit together while avoiding directional shimmer on the map. `depthOfField.enabled`, `depthOfField.strength`, `depthOfField.maxRadius`, `depthOfField.focusDepth`, and `depthOfField.falloff` soften backdrop texture detail by distance from the active Pokemon: the focus band near the Pokemon remains crisp, then blur ramps in farther back through a weighted soft kernel. This is an art-directed Amie-style focus blur, not photoreal lens simulation.
- Overlay UI is scene data. `ui.weatherButton`, `ui.viewButton`, `ui.pokemonButton`, `ui.textureVariantButton`, `ui.formVariantButton`, and `ui.skyButton` control button anchor, size, margins, padding, corner radius, border width, font size, fill color, border color, text color, and label prefix. The shared overlay canvas owns layout and hit testing; bgfx scenes draw the visible buttons through bgfx so the overlay is not hidden by the 3D presentation layer.

## Interaction Adapter Contract

Attend gameplay should talk in semantic terms such as `petting`, `face look`, `close eyes`, and eventually `feed` or `hold`. Renderer and model-source details stay behind the adapter.

The current Gen 1-7 adapter defaults are data-driven in `config/gameplay/pokemon_attend/providers/gen1_7.json` under `defaults.interactionAdapter`, and map those semantics onto the provider's exported nodes, animation fallback, and RAE eye-expression sheets. Future providers such as Pokemon HOME or non-3DS model sources should add or swap adapter data/capabilities without changing the attend screen input flow.

The working Gen 1-7/3DS slot, eye-frame, mouth-frame, and reaction-combo dictionary lives in [`test_attend_animation_semantics.md`](test_attend_animation_semantics.md). Runtime config mirrors that dictionary through `interactionAdapter.semanticAnimationSlots`, `interactionAdapter.eyeExpressionFrames`, `interactionAdapter.mouthExpressionFrames`, and `interactionAdapter.reactionCombos`.

The default Gen 1-7/3DS eye-expression frame dictionary is:

- `normal_open`: frame `0`
- `angry`: frame `1`
- `sick_hurt`: frame `2`
- `happy`: frame `3`
- `closed`: frame `4`
- `sad`: frame `5`
- `hit`: frame `6`
- `padding_red_stain`: frame `7`

Blinking and petting currently use `normal_open` and `closed`. The other names are reserved so later reaction logic can ask for semantic eye states instead of raw frame numbers.

The default Gen 1-7/3DS mouth-expression frame dictionary is:

- `closed_normal`: frame `0`
- `angry_open`: frame `1`
- `happy_open`: frame `2`
- `sad_closed`: frame `3`
- `happy_open_wide`: frame `4`
- `unused_5`: frame `5`
- `closed_narrow`: frame `6`
- `unused_7`: frame `7`

Mouth-sheet Pokemon, such as Eevee-style models, use this dictionary independently from eyes. The renderer applies mouth frames only to mouth-classified sheet materials, so future reactions can mix one body animation with different eye and mouth expressions.

Pokemon with separate iris/pupil meshes use a stencil-backed eye composite in the attend bgfx renderer: eye-sclera materials write the eye mask, `eye_iris` materials draw only through that mask, and the iris layer is suppressed on the configured `closed` frame so blink/pet-close does not leave detached pupils.

When a pet drag reaches `minPetSeconds`, the renderer can play the combo's low-weight `readyAnimation` cue and `readyMouth` frame while the pointer is still held. When that pet drag then ends successfully, the screen triggers the `pet_happy` combo. The Gen 1-7/3DS defaults resolve that to the first available `emote_happy` slot candidate, the `happy` eye frame, and the `happy_open` mouth frame. Reaction animations are blended as overlays over idle using `fadeInSeconds` and `fadeOutSeconds`; `largePokemonFadeScale` lengthens those blends for large models, and `eyeLingerSeconds` keeps the happy expression briefly after the body returns to idle.

Pointer petting uses the renderer's last projected Pokemon bounds, not a species-specific hard-coded oval. That locator is intentionally model-source neutral and should remain reusable when the active Pokemon provider changes.

Sparse per-Pokemon overrides should be reserved for exceptions. The preferred path is:

- infer common concepts from model hierarchy, skin weights, animation names, and source conventions
- keep source-specific naming rules isolated to adapter data/code
- expose reusable semantic outputs to petting, feeding, and reaction controllers

## Future Overworld Handoff Contract

For now, `TEST ATTEND` is only reachable from the title Resort submenu. When the overworld can launch it directly, the route should preserve overworld state:

- entering from overworld calls `Overworld3DTestScreen::suspendForAttend()` before activating `AttendTestScreen`
- suspend releases heavyweight presentation resources such as bgfx while preserving map, player, follower, camera, and event state
- returning calls `Overworld3DTestScreen::resumeAfterAttend()` and restores the existing overworld session instead of calling `resetForNextLaunch()`

Keep this interaction feature independent from transfer storage and external save movement.
