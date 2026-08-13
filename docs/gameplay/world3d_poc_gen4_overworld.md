# Gen 4 3D Overworld Foundation

## Scope
This milestone adds a data-driven 3D overworld proof of concept reachable from the title flow:
- Main menu `RESORT` now opens a Resort submenu.
- Resort submenu contains `3D TEST`, `START CINEMATIC` (placeholder), `TEST ATTEND`, and `BACK`.
- Selecting `3D TEST` opens the overworld test screen.
- Selecting `TEST ATTEND` opens a separate Pokemon interaction preview; see `docs/gameplay/test_attend.md`.

Reference for camera facts and Gen 4 behavior source of truth:
- `docs/pk4_documentation.md`

## How To Launch
1. Start app.
2. From title main menu, select `RESORT`.
3. Select `3D TEST`.
4. Use directional input to move.
5. Press `Q` to toggle free-cam (mouse look + arrows/WASD movement, detached from character).
6. Press `J` to trigger follower debug jump.
7. Press `P` to trigger follower debug poke.
6. Press Back to return to title menu.

## Data Locations
Character assets:
- `assets/characters/playable/haru.charbin` (runtime test character package)
- `.charbin` schema and integration contract: `assets/characters/CHARBIN_SCHEMA.md`
- Shared sprite profiles: `config/gameplay/sprites/sprite_profiles.json`
- Shared sprite shadow config: `config/gameplay/sprites/shadow.json`
- Follower session source: `config/gameplay/followers/session.json`
- Follower summon behavior: `config/gameplay/followers/summon.json`
- Follower nature idle behavior: `config/gameplay/followers/idle_behaviours.json`

Map assets and scene config:
- `assets/overworld/maps/testing.owmap` (runtime default)
- `assets/overworld/maps/flat_bootstrap.owmap` (reference sample)
- Interaction behavior scripts: `config/gameplay/world3d/interactions.json`
- Interaction text catalog: `config/gameplay/world3d/interaction_text.json`
- Interaction text box config: `config/gameplay/world3d/textbox.json`
- Interaction text box skins: `assets/overworld/ui/text_boxes.png`
- Temporary Resort-box Pokemon roster: `config/gameplay/world3d/pokemon_spawns.json`
- Overworld script catalog: `config/gameplay/world3d/scripts/script_catalog.json`

Loader policy:
- `.owmap` is the primary runtime format.
- `.map.json` scene loading is removed from runtime.

### Reusable map instances

The Map Studio project is a placement catalog, not a one-file-per-placement rule. Two or more project map entries may reference the same `.owmap` in their `file` field. Each entry keeps its own unique `id`, `gridX`, `gridY`, and `linked` state, while the terrain, tiles, and models are read from the shared source file. Map Studio creates one from the layout map context menu with **Reuse map**; the new separated instance can then be dragged and snapped into the connected layout.

At runtime the shared source is decoded once per world load and copied into instance records. The project entry id replaces the source scene id for that instance, so animation state, anchors, and teleport destinations remain addressable per placement. Door links should target the project instance id. Editing any instance edits the shared `.owmap` source by design.

## OWMAP Binary Format (Runtime)
Primary runtime map format is `.owmap`:
- Header: magic/version/width/height/tileSize/metaLen
- Metadata: UTF-8 JSON blob (scene metadata, no terrain arrays)
- Terrain payload:
  - `height`: `W*H` bytes
  - `special`: `W*H` bytes
  - `collision`: bit-packed, `ceil(W*H/8)`

Detailed specification:
- `docs/gameplay/owmap_format.md`

## Runtime Modules
Menu and app flow:
- `src/ui/title_screen/ResortMenuController.cpp`
- `src/ui/TitleScreen.cpp`
- `src/core/app/screen/AppScreenCoordinatorOverworld3D.cpp`
- `src/core/app/screen/AppScreenCoordinatorAttend.cpp`
- `src/ui/Overworld3DTestScreen.cpp`
- `src/ui/AttendTestScreen.cpp`

Overworld systems:
- `src/gameplay/world3d/data/JsonOverworldLoader.cpp`
- `src/gameplay/world3d/data/OwmapOverworldLoader.cpp`
- `src/gameplay/world3d/data/SceneMetadataParser.cpp`
- `src/gameplay/world3d/camera/Gen4CameraPreset.cpp`
- `src/gameplay/world3d/camera/Gen4FollowCamera.cpp`
- `src/gameplay/world3d/characters/CharacterController.cpp`
- `src/gameplay/world3d/characters/SpriteSheetAnimator.cpp`
- `src/gameplay/world3d/followers/FollowerConfig.cpp`
- `src/gameplay/world3d/followers/FollowerController.cpp`
- `src/gameplay/world3d/npc/ResortPokemonSpawnConfig.cpp`
- `src/gameplay/world3d/npc/NpcActorDriver.cpp`
- `src/gameplay/world3d/dialogue/OverworldTextboxConfig.cpp`
- `src/gameplay/world3d/dialogue/OverworldTextboxController.cpp`
- `src/gameplay/world3d/dialogue/OverworldTextboxRenderer.cpp`
- `src/gameplay/world3d/interactions/InteractionSequence.cpp`
- `src/gameplay/world3d/interactions/InteractionText.cpp`
- `src/gameplay/world3d/rendering/OverworldMapRenderer.cpp`
- `src/gameplay/world3d/rendering/BillboardSpriteRenderer.cpp`

## Character Metadata Contract
Runtime character packages are `.charbin` files with:
- embedded PNG assets
- package metadata + actions (`idle`, `walk`, etc.)
- sheet/profile binding (`baseProfile` and sheet `profile`)

Shared animation/layout/render settings are resolved by profile from:
- `config/gameplay/sprites/sprite_profiles.json`

Current profile set:
- `character`
- `pokemon_small`
- `pokemon_large`

Shared rendering profile supports:
- `rendering.anchor` (`bottom_center` or `center`)
- `rendering.worldOffset`
- `rendering.screenOffsetPx`

Global billboard presentation (`config/gameplay/world3d/render.json`):
- `billboard.tileAnchorOffsetTiles.forward` — legacy presentation offset; keep `0.0` so actor feet stay on the simulation XZ position.
- `billboard.tileAnchorOffsetTiles.right` — legacy lateral offset; keep `0.0` for collision-aligned actor rendering.

## Temporary Resort-box Pokemon Roster

`config/gameplay/world3d/pokemon_spawns.json` controls the temporary overworld Pokemon source before map spawn points and companion selection exist.

- `enabled`: turns this temporary roster on or off.
- `profileId`: Resort profile to read.
- `boxId`: zero-based Resort box id to read (`0` is Box 1).
- `maxPokemon`: maximum valid Pokemon used from that box, including the follower.

The first valid Pokemon, in box-slot order, becomes the follower. Later selected Pokemon spawn on random valid map tiles and use the roaming behavior. Each actor carries the Resort slot's `form_key` and `shiny` state into charbin appearance selection: matching `formId` and `modifiers: ["shiny"]` sheets are used when present, otherwise the matching base sheet is used. `maxPokemon: 0`, a disabled roster, or an empty box produces no follower and no roster Pokemon. `config/character_testing/characters.json` is only for fixed test actors and does not source Pokemon from Resort boxes.

Per-character `worldOffset` from charbin is unchanged.

Global sprite shadow supports:
- `shadow.enabled`
- `shadow.opacity` (0..1)
- `shadow.pixelCoherent` (if true, shadow size is derived from mask pixels and sprite pixel scale)
- `shadow.feetToShadowBottomPx` (pixel gap from feet to shadow bottom; Gen4 target is `3`)
- `shadow.screenOffsetPx`
- `shadow.colorRgba` (`[r,g,b,a]` 0..255)
- `shadow.radiusXTiles`
- `shadow.radiusZTiles`
- `shadow.worldYLift`
- `shadow.textureWidthPx`
- `shadow.textureHeightPx`
- `shadow.mask` (`textureHeightPx` rows of `textureWidthPx` chars, `X` = filled, others transparent)

Fallback behavior:
- If `shadow.enabled` is `false`, runtime uses the built-in default Gen4 shadow.
- If `shadow.mask` is missing/invalid, runtime falls back to the built-in default Gen4 mask.

Animation behavior is not hardcoded per-character in rendering code.

## Follower Runtime Config
Follower runtime is split into two sources:
- Session source: `config/gameplay/followers/session.json`
- Shared summon behavior: `config/gameplay/followers/summon.json`

Session config is intentionally minimal and contains only:
- `pokemonSpecies`
- `pokeballId`
- `nature`
- `forcedBehavior` (optional debug override)

Summon behavior is shared gameplay tuning and contains:
- `followStepDurationMs`
- `ballAnimation.durationMs`
- `ballAnimation.pokeball_hold_animation`
- `ballAnimation.fullAnimation`
- `ballAnimation.fallEnabled`
- `ballAnimation.fallHeightWorld`
- `entryAnimation.durationMs`
- `entryAnimation.startScale`
- `entryAnimation.colorRgba`

Follower flow:
- The first valid Pokemon found in Resort Box 1 becomes the follower Pokemon. `config/gameplay/followers/session.json` remains the fallback when Box 1 has no valid Resort Pokemon package.
- Other valid Resort Box 1 Pokemon spawn at random valid points as roaming Pokemon actors.
- Follower starts hidden.
- On first player movement, ball release animation runs.
- Pokémon entry flash/scale animation plays from size `0` and stays white until full size.
- Follower becomes active and trails one tile behind player path.

Resolution rules:
- Pokémon character package resolves from species through `assets/characters/pokemon/{species}.charbin`
- Poké Ball package resolves from `assets/characters/objects/{id}.charbin`
- Missing ball package falls back to `assets/characters/objects/poke_ball.charbin`

## Nature Idle Behavior
Follower idle behavior is driven by:
- `config/gameplay/followers/idle_behaviours.json`

Nature comes from the follower session JSON:
- `follower.nature`

Runtime rules:
- Nature idle starts only after the player has remained idle for the configured delay.
- Missing or invalid natures disable the system.
- Valid natures are normalized case-insensitively to the canonical Pokémon nature names.
- Behavior selection is weighted by nature group plus per-nature modifiers.
- Natural behavior end restores the follower to its saved origin tile and direction when enabled.
- Player activity cancels the behavior immediately and sends the follower back to normal follow mode.
- The player's occupied tile is treated as blocked while idle behavior pathing is active.

Shared jump/poke tuning also lives in:
- `config/gameplay/followers/idle_behaviours.json`

Relevant fields:
- `natureIdle.jump.heightPixels`
- `natureIdle.jump.durationSeconds`
- `natureIdle.poke.distanceTiles`
- `natureIdle.poke.forwardSeconds`
- `natureIdle.poke.returnSeconds`
- `natureIdle.landingDust.enabled`
- `natureIdle.landingDust.texturePath`
- `natureIdle.landingDust.frameWidth`
- `natureIdle.landingDust.frameHeight`
- `natureIdle.landingDust.frameCount`
- `natureIdle.landingDust.spriteScale`
- `natureIdle.landingDust.screenOffsetYPx`

Landing dust runtime notes:
- Current sheet: `assets/effects/dust.png`
- Layout: `96x32`, three `32x32` frames in one row
- Dust is spawned on jump landing
- A single active dust instance is kept per jumper, so rapid repeat jumps restart that jumper's dust instead of stacking multiple dust puffs on the same actor
- The default dust duration matches the jump cycle duration so the same actor does not leave overlapping dust behind

## Debug Overlay
App-level debug overlay toggle:
- `config/app.json`
- `enable_active_idle_behavior_debug`

When enabled, the overworld screen shows `AIB: ...` in the top-left with the follower's current idle or debug action label.

## Interaction Text Box

NPC/Pokemon interaction prompts are driven by the interaction system documented in `docs/gameplay/overworld_interactions.md`. Pressing Accept while facing an interactable target starts a behavior sequence rather than directly opening the textbox.

Default behavior:

- Pokemon are script-first. With no eligible valid script, targets face the player, then Haru's player charbin enters the size-based interaction activity when available. Free text opens while Haru holds the session stay phase.
- NPC charbins use `metadata.npcInteractionMode`: missing or `direct_dialogue` faces the player and reads that NPC's `dialogue.lines`; `scripted` selects an eligible shared interaction script and falls back to direct dialogue when none is available. Human NPCs do not use Pokemon size-session animations.
- The source boundary reserves future precedence for a transient runtime-assigned script over NPC scripted mode over direct dialogue; spawning and assignment are not implemented here.
- Accept closes the current textbox and advances the sequence.
- Back cancels the sequence, exits any active player interaction session, and unlocks the target.

Behavior scripts live in `config/gameplay/world3d/scripts/`. Pokemon free text selection lives in `config/gameplay/world3d/interaction_text.json`: matching text entries are filtered by required tags, the most-specific matching group wins, weighted random selects inside that group, and selected text ids enter a global in-memory cooldown. Human NPC free text instead comes from that character's charbin `dialogue.lines`.

The visible textbox skin still uses `config/gameplay/world3d/textbox.json`.

Textbox text presentation is data-driven under `textbox.text`: `fontPath`, `fontSizePx`, `leftInsetPx`, `rightInsetPx`, and `topInsetPx` select the font, wrapping width, and placement inside the skin. The default uses the regular non-bold `assets/fonts/power clear.ttf` face. The legacy `futureText` object remains readable for compatibility.

The top-right Attend shortcut is authored in the same file under `attendButton`: `enabled`, `iconPath`, `topPx`, `rightPx`, `widthPx`, and `heightPx`. It is visible only while a Pokemon interaction textbox is active. Clicking it or pressing the app-level `input.attend_keys` binding (default `X`) opens Attend without resetting the overworld screen. Attend Back or its top-left return button returns to the same overworld instance, preserving player position and runtime state.

Overworld screen changes use reusable controllers under `ui/transitions`. `config/gameplay/world3d/transitions.json` selects the Attend transition type and owns `durationSeconds`, `circleSegments`, and `maxRadiusScale`. The first type, `black_iris`, runs a four-stage handoff: close around the projected player in the overworld, open over Attend, close over Attend on return, then open over the restored overworld. A scene handoff occurs only after one fully closed frame has been presented. On return, the restored overworld presents one fully open idle frame before its post-Attend script begins, so short actions are visible from their first frame.

The direct Overworld/Attend handoff keeps the overworld bgfx renderer resident and shares the active GPU device between both 3D scenes. Returning therefore does not reload the map, tile package, buildings, shaders, or player textures. The small offscreen world presentation target is recreated after Attend so its framebuffer cannot retain stale shared-backbuffer state. Device shutdown happens only when the app leaves 3D presentation for a 2D screen. Frame delta is capped after synchronous resource stalls so the opening iris still advances over visible frames rather than completing during a load pause.

- `textbox.visibleMode`: `enabled` or `disabled`.
- `textbox.selectedSkinIndex`: choose skin `0` through `12`; visual order goes down the left column first, then down the right column. The 14th bottom-right sheet cell is empty and ignored.
- `textbox.padding.bottomPx` / `textbox.padding.sidePx`: DS/internal viewport padding.
- `textbox.horizontalStretch.stripWidthPx` / `sourceCenterXPx`: the central source strip stretched to fit the visible world viewport width. The left and right stylized sides are copied without stretching.

The active target is interaction-locked during the full sequence so movement, rotation, and idle behavior do not start until cleanup finishes.

## Camera Preset
The POC loads the camera preset ID from map config (`camera.preset`), currently targeting:
- `gen4_platinum_default_exterior`

Current preset values are aligned with `docs/pk4_documentation.md` baseline:
- perspective projection
- distance ~ `666.922119`
- pitch ~ `-59.051514`
- near/far clip ~ `150/900`
- 4:3 framing intent

POC camera tuning is data-driven in map JSON:
- `camera.preset` (scene selects preset only)

Camera preset and free-cam tuning source:
- `config/gameplay/camera/presets.json`
- `config/gameplay/world3d/defaults.json`

Lighting tuning is data-driven in map JSON:
- `lighting.brightness`
- `lighting.tint` (`[r, g, b]` in 0.0-1.0 range)

## Explicit Non-Goals In This Milestone
Not implemented yet:
- collision and permissions
- NPC behavior and schedules
- warps and triggers
- battle transitions
- cinematic runtime
- overworld-to-TEST-ATTEND handoff
- interiors and camera variants (including HGSS variants)
