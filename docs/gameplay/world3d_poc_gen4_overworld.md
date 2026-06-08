# Gen 4 3D Overworld Foundation

## Scope
This milestone adds a data-driven 3D overworld proof of concept reachable from the title flow:
- Main menu `RESORT` now opens a Resort submenu.
- Resort submenu contains `3D TEST` and `START CINEMATIC` (placeholder).
- Selecting `3D TEST` opens the overworld test screen.

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

Loader policy:
- `.owmap` is the primary runtime format.
- `.map.json` scene loading is removed from runtime.

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
- `src/ui/Overworld3DTestScreen.cpp`

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
- interiors and camera variants (including HGSS variants)
