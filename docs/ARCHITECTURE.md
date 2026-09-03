# Pokemon Resort Architecture

This is the central architecture map for the SDL2 app in [`pokemon-resort`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort). It should stay accurate enough that a human or AI agent can decide where a change belongs before editing code.

Domain-first groundwork references:
- [`docs/architecture/system-map.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/architecture/system-map.md)
- [`docs/architecture/module-rules.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/architecture/module-rules.md)

For test strategy, use [`tests/README.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tests/README.md) as the canonical test map. For config ownership, use [`docs/config/README.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/config/README.md). For transfer-system work, read [`docs/transfer_system/README.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/transfer_system/README.md) before editing transfer screen code. For OpenHome-first transfer persistence and safety, read [`docs/transfer_system/SAVE_EXIT_SAFETY.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/transfer_system/SAVE_EXIT_SAFETY.md) plus the OpenHome integration notes (`docs/openhome-*.md`). For bridge work, use [`PKHEX_BRIDGE.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/PKHEX_BRIDGE.md).

## Current State

The app currently implements:

- title intro, title hold, main menu, options menu, and Resort/Trade loading-transition test flows
- Resort submenu routing to the 3D overworld test and standalone TEST ATTEND interaction debug scene
- transfer entry from the main menu
- loading screen while external saves are scanned and probed
- transfer-ticket selection built from bridge/cache summaries
- selected-save deep probe into the transfer system screen
- transfer-system box UI with Box Space, tool carousel, lower info banner, Pokemon action menu, item action menu, keyboard/controller focus, pointer gestures, and temporary in-memory Pokemon/item movement
- persisted user options through `SaveDataStore`
- a canonical SQLite-backed Resort backend foundation that is open at runtime but not yet the transfer screen's storage source of truth

The codebase is transfer-heavy now. `TitleScreen.cpp` still matters, but transfer work should start in the transfer flow, transfer ticket, transfer system, bridge, or backend modules named below.

## Source-Of-Truth Rules

- **JSON config** owns authored layout, timing, text, colors, asset paths, icon placement, animation tuning, input bindings, and audio defaults.
- **Runtime code** owns state machines, input semantics, controller behavior, bridge/cache parsing, persistence decisions, and screen transitions.
- **Persisted save/profile data** owns player settings, canonical Resort Pokemon state, and future durable transfer/storage state.
- **Bridge JSON** is an external process contract. Parse it at boundaries such as `SaveLibrary` or `BridgeImportAdapter`; do not re-query raw bridge JSON from UI rendering code.

When changing behavior, name which source of truth you are changing before you edit.

## Runtime Architecture

### Entry And App Orchestration

- [`src/main.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/main.cpp) forwards an optional title config path into `pr::runApplication` and supports `--clear-save-cache`.
- [`src/core/App.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/App.cpp) owns SDL initialization, config/root bootstrap, window/renderer setup, composition of app-level services/screens, the main loop, input polling, active-screen rendering, and overlay presentation.
- [`src/core/app`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/app) owns app orchestration helpers: screen routing (`screen/`), loading-screen selection (`loading/`), transition timing (`transition/`), audio direction (`audio/`), one-frame requests (`frame/`), user-settings persistence (`persistence/`), and app path helpers.
- Keep `App.cpp` orchestration-focused. New screen-specific state should live in a screen, coordinator, controller, service, or config parser.

### Core Modules

- [`ConfigLoader.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/config/ConfigLoader.cpp) parses app/title config into types from [`Types.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/core/Types.hpp).
- [`Types.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/core/Types.hpp) is the app/title config and persisted settings contract.
- [`Assets.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/assets/Assets.cpp) resolves project-root asset paths, loads textures, renders text, and builds title logo masks.
- [`PokeSpriteAssets.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/assets/PokeSpriteAssets.cpp) owns Pokemon sprite, item icon, misc icon, and SDL texture-cache resolution for transfer UI. Its contract is documented in [`docs/assets/pokesprite_subsystem.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/assets/pokesprite_subsystem.md).
- [`PokemonCryAssets.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/assets/PokemonCryAssets.cpp) owns reusable Pokemon cry path resolution by species id. [`PokemonCryPlayer.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/app/audio/PokemonCryPlayer.cpp) turns those resolved cries into app one-shot SFX requests, so screens/controllers can request Pokemon cries without hard-coding cry folders.
- [`InputBindings.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/input/InputBindings.cpp) maps human-readable key names from JSON to SDL keycodes.
- [`InputRouter.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/input/InputRouter.cpp) translates SDL keyboard, mouse, and controller events into the active [`ScreenInput`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/ui/ScreenInput.hpp).
- [`SaveDataStore.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/save/SaveDataStore.cpp) loads primary/backup user settings and writes atomically.
- [`SaveBridgeClient.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/bridge/SaveBridgeClient.cpp) resolves and launches the external .NET bridge process.
- [`SaveLibrary.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/save/SaveLibrary.cpp) scans the top-level [`saves`](/Users/vanta/Desktop/title_screen_demo/saves) folder, hashes candidates, probes through the bridge, caches summaries, and parses native transfer models for ticket and box UI.
- [`src/resort`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/resort) and [`include/resort`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/resort) own canonical Resort storage, repositories, matching, merge policy, import/export services, mirror sessions, and backend read models.
- [`Audio.mm`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/audio/Audio.mm) is the current macOS audio backend used by the app audio director.

## UI Layer

### Shared Screen Surface

- [`ScreenInput.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/ui/ScreenInput.hpp) defines the reusable input vocabulary for screens.
- [`Screen.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/ui/Screen.hpp) extends that vocabulary with `update(dt)` and `render(renderer)`.
- [`OverlayCanvas.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/ui/overlay/OverlayCanvas.hpp) is the reusable logical overlay surface for buttons and lightweight HUD controls. It owns anchoring and hit testing in app/window logical coordinates. [`OverlaySliceLayout.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/ui/overlay/OverlaySliceLayout.hpp) owns reusable three-slice sprite layout for UI panels such as overworld text boxes. SDL screens can draw overlay surfaces directly; bgfx screens should use the same shared layout sources and draw the visible overlay through bgfx so Metal/canvas presentation layers cannot hide it.
- Full-screen UI pages should prefer `Screen` unless they are intentionally only helper/controller code.

### Overworld Interactions

The Gen 4 overworld test routes player-triggered NPC/Pokemon interaction through reusable modules under [`gameplay/world3d/interactions`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/gameplay/world3d/interactions). [`InteractionSequenceController`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/gameplay/world3d/interactions/InteractionSequence.hpp) owns pure source selection and action ordering such as `facePlayer`, `pokemonInteractionSession`, and `textFree`; [`InteractionText`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/gameplay/world3d/interactions/InteractionText.hpp) owns tag-specific weighted text selection and global in-memory cooldowns. Pokemon are script-first and retain the established sequence as a safe fallback. NPC charbins own only `metadata.npcInteractionMode` (`direct_dialogue` by default, or `scripted`); scripted selection falls back to that NPC's `dialogue.lines`. A future spawner may provide a transient runtime interaction script reference with precedence over NPC mode, but charbins do not require a permanent script id. [`Overworld3DTestScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/Overworld3DTestScreen.cpp) remains the scene adapter that locks targets, applies facing, opens the textbox, and asks Haru's player animator to run the size-based interaction activity for Pokemon targets. Keep new interaction rules in the shared interaction modules and authored JSON under `config/gameplay/world3d/`; do not put new script logic directly in the screen.

Detailed authoring and extension guidance lives in [`docs/gameplay/overworld_interactions.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/gameplay/overworld_interactions.md).

### Overworld Tile Packages

Map project entries are runtime instances. Multiple entries may point to one shared `.owmap` source while retaining distinct project ids and layout positions. The overworld loader caches decoded sources by canonical file path and assigns the entry id to each copied scene. This keeps repeated ocean/background chunks out of storage without merging their runtime identity.

Overworld and Attend share one process-wide bgfx device while moving directly between those two 3D screens. The overworld renderer and its GPU resources remain resident during Attend, so returning restores the existing scene instead of rebuilding terrain, RTPKS tiles, buildings, shaders, and character textures. Attend may alter shared backbuffer state, so resume invalidates and recreates only the overworld's small offscreen presentation target; cached world resources remain intact. Backbuffer reset is skipped while its dimensions are unchanged. Individual screen renderers still release their resources when leaving the 3D presentation path; `App.cpp` shuts down the shared device before destroying the Metal view or returning to SDL UI. The main loop caps simulation delta after a blocking upload so animations and transitions do not consume a wall-clock stall in one update.

`.owmap` metadata binds one RTPKS package and stores stable `resortTileId`
placements in decoration layers. [`RtpksTilePackageLoader.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/gameplay/world3d/data/RtpksTilePackageLoader.cpp)
loads meshes, materials, frame animation assets, exact material-motion timelines,
per-axis texture samplers, gameplay tags, and collision authoring metadata.
[`OverworldBgfxRenderer.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/gameplay/world3d/rendering/bgfx/OverworldBgfxRenderer.cpp)
keeps static geometry batched by material, selects globally synchronized pattern
keyframes, and smoothly interpolates UV offsets during submission. Materials may
also carry a source-derived world-UV basis (`uPerTile` / `vPerTile`), so repeating
1x1 placements continue one texture field instead of restarting their UV domain.
Layered water therefore remains layered geometry: each plane keeps its own
uniform/texture alpha, height, sampler, and UV motion rather than becoming a
baked flipbook. Tile geometry is emitted only for authored `.owmap` placements;
the renderer never extends an ocean plane beyond its placed footprint.

World model placements accept self-contained GLBs with morph-target `weights` and
node `rotation` animation channels. [`GlbModelLoader.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/gameplay/world3d/data/GlbModelLoader.cpp)
retains morph deltas, node pivots, vertex colors, and clip data, while
[`GlbModelAnimation.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/gameplay/world3d/data/GlbModelAnimation.cpp)
samples authored clips as synchronized loops. Both the bgfx renderer and SDL fallback
consume that contract, so animated/vertex-colored props referenced by map placements do
not need scene-specific playback code.

Aquarium interiors use an optional module under `gameplay/world3d/aquarium`.
The OWMAP remains the source of the placed tank transform, Aquarium Maker's
navigation export remains the source of water volumes and obstacle holes, and
`config/gameplay/world3d/aquariums.json` owns population, shared scale, movement,
starting anchors, reusable behavior selection, and tank-inspection camera tuning.
The pure simulation resolves Pokemon by name from the Attend catalog and derives
navigation clearance from the selected form's rendered bounds. Its small bgfx
adapter reuses Attend skeletal model data inside the existing world render pass;
`AttendPokemonPresentation` is the shared CPU contract for form/material
selection, facial UV sheets, and composed texture payloads. No aquarium config
means no aquarium actors or per-frame work. See
[`docs/gameplay/aquariums.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/gameplay/aquariums.md).
Player-built tanks use the same simulation rather than a renderer-only actor
path. Their authoritative saved design is rebuilt by `shared/aquarium_geometry`;
its navigation layers and suggested spawns are converted from kernel world units
to the simulator's tank-local metres, then a replaceable population policy
supplies swimmer definitions. Successful construction publication replaces only
the player-built subset, leaving authored tank swimmers and their state intact.

The Operations Desk Map Editor owns RTPKS authoring. Its Tile Pack Editor may
reorganize tabs and smart paths or append assets, but must never renumber an
existing stable tile id. Automatic tile collision is applied to the `.owmap`
terrain collision grid while painting; it is not a second runtime collision
source. Eight-neighbor terrain transitions are an editor concern: RTPKS metadata
identifies a transition family and mask, Map Studio resolves the stable tile id
when painting, and the native runtime renders the stored id without topology
inference. See [`docs/gameplay/owmap_format.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/gameplay/owmap_format.md)
and [`docs/gameplay/owmap_tile_layers_proposal.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/gameplay/owmap_tile_layers_proposal.md).

### Native Pokemon Resort Map Maker

[`tools/map_maker`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tools/map_maker) owns the standalone `pokemon_resort_map_maker` executable. It is an authoring consumer of the game data/rendering contracts, never a production runtime dependency. Its detailed build, usage, recovery, logging, and extension guide is [`tools/map_maker/README.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tools/map_maker/README.md); the boundary decision is recorded in [`ADR 0001`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/architecture/adrs/0001-native-map-maker.md).
Pure editor interaction state and semantic lens colors live below the ImGui shell,
so Play capture and authoring visualization rules can be regression-tested without
launching SDL. The native shell publishes the logical-to-framebuffer scale before
each ImGui frame; the editor-local bgfx bridge renders UI view rectangles at the
physical backbuffer size. These details remain inside the standalone target.

The editor is split by responsibility:

- `document/` owns lossless OWMAP v1 decoding, mutation, validation, byte-preserving no-op serialization, and verified atomic saves;
- `project/` and `app/ProjectWorkspace` own project discovery, lossless world-graph mutation, runtime-compatible path resolution, and one loaded document/command stack per normalized reusable source;
- `commands/`, `selection/`, and `validation/` own pure undoable editing contracts and door/link/anchor/project diagnostics;
- `assets/` projects RTPKS sidecar metadata and model manifests, keeping thousands of preview PNGs compressed until visible;
- `interaction/WorldPicker` intersects the same terrain triangles submitted by the world renderer without allocating in the pointer-hover path;
- `preview/ExactWorldPreview` loads runtime scene data and delegates to `OverworldBgfxRenderer::renderEmbeddedViewport`;
- `ui/EditorShell` consumes a UI model and emits editing intentions; it does not own document mutation.

The default authoring surface is a spatial World graph. It positions exterior
maps, lays linked interiors out as satellites, renders door travel edges, and
supports undoable project moves and adjacent-map creation. Opening a node enters
the cached, clipped top-down Map workspace; cell edits do not load or resubmit a
3D scene. The embedded renderer is a separate Play workspace and returns its
renderer-owned pixel-target texture without backbuffer composition or
`bgfx::frame()`. It uses the shipping character controller, terrain traversal,
collision, water state, camera, sprite animation, renderer, and authored
environment clocks. Switching maps remains immediate, and an exact scene is
built only when Play needs a different or edited source.

`tools/map_maker` is added to CMake with `EXCLUDE_FROM_ALL`. Normal game builds
do not compile editor-only ImGui, document, or UI targets; `./pkr mapbuilder`
builds the explicit editor target and launches it.

Unknown project fields, OWMAP metadata, collision spare bits, and trailing bytes
must survive editor changes. Reused map entries retain separate project identity
while sharing the same source document and history. Smart-object features must
compile to normal runtime OWMAP/RTPKS data instead of adding editor logic to
`gameplay/world3d`.

### Overworld Script Engine

[`gameplay/world3d/scripts`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/gameplay/world3d/scripts) owns reusable script loading, validation, tag/proximity matching, priority/weight selection, and cooldowns. Author scripts as individual JSON files listed by [`script_catalog.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/gameplay/world3d/scripts/script_catalog.json). The Script Engine Operations Desk module owns safe file editing and validation. Existing follower idle and NPC movement remain compatibility executors: scripts decide eligibility while their established planner, pathing, jump, and landing-dust implementations perform motion. Do not move terrain physics or actor collision into the editor.

### Temporary Overworld Pokemon Roster

[`ResortPokemonSpawnConfig`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/gameplay/world3d/npc/ResortPokemonSpawnConfig.hpp) owns the temporary Resort-box roster contract, loaded from [`config/gameplay/world3d/pokemon_spawns.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/gameplay/world3d/pokemon_spawns.json). It selects a profile and box, caps the total valid Pokemon used by the overworld, reserves the first one for the follower, and leaves the remaining selected entries to [`NpcActorDriver`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/gameplay/world3d/npc/NpcActorDriver.hpp) as random roaming spawns. `maxPokemon` includes the follower. An empty or disabled roster produces neither a follower nor roster Pokemon. Authored entries in `config/character_testing/characters.json` are separate fixed test actors and must not use a Resort-box source.

### Title, Menu, Options, And Placeholder Sections

- [`TitleScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TitleScreen.cpp) owns title-flow state transitions, high-level title/menu/options/section coordination, and one-shot title events consumed by `App.cpp`.
- [`MainMenuController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/title_screen/MainMenuController.cpp) owns top-level menu selection and selected-row to menu-action mapping.
- [`OptionsMenuController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/title_screen/OptionsMenuController.cpp) owns options selection, value cycling, labels, and mapping to/from persisted `UserSettings`.
- [`src/ui/loading`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/loading) owns loading-screen implementations and the factory used by app flows. The Resort transfer boat screen includes its WhiteIdle/enter/loading/exit state machine and flat 2D SDL rendering.
- [`SectionScreenController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/title_screen/SectionScreenController.cpp) owns legacy placeholder section identity for any remaining placeholder menu destinations.
- [`TitleScreenRender.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/title_screen/TitleScreenRender.cpp) owns title/menu/options/section rendering helpers, button geometry, texture caches, and logo shine generation.

Title-side effects should flow through typed `TitleScreenEvent` values. Avoid adding more boolean consume methods.

### TEST ATTEND Interaction Debug Scene

[`AttendTestScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/AttendTestScreen.cpp) is a standalone `Screen` reached from `RESORT -> TEST ATTEND`. It renders a data-driven Pokemon interaction debug scene through [`AttendBgfxRenderer.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/gameplay/attend/rendering/AttendBgfxRenderer.cpp). [`config/gameplay/pokemon_attend.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/gameplay/pokemon_attend.json) is a small manifest that points to concern-specific files under `config/gameplay/pokemon_attend/`: debug scene files own temporary selections and TEST ATTEND-only overlay buttons, interaction files own shared camera/pointer/hand defaults, provider files own reusable Pokemon/model defaults and interaction adapters, and environment files own floor/extension catalogs plus edge-proof sky/light/presentation presets. Per-Pokemon entries should stay sparse: model paths are inferred from `activePokemon` plus the provider file pattern, while common placement, animation naming, idle behavior, interaction adapter defaults, and eye-close naming are inferred from defaults or provider rules. The current scene uses one world-placed environment GLB floor, weather material groups, mouse-edge look-target nudging, a simple data-driven contact shadow, and an initial auto-focus camera calculation that frames the active Pokemon from its setup pose without following animation. It is intentionally independent from transfer, Resort storage, and the Gen 4 overworld proof of concept.

TEST ATTEND uses a Pokemon-specific GLB path under `gameplay/attend/rendering`: it preserves skeletal skinning, transform animation channels, material sampler wrap, RAE material policy extras, and Pokemon eye metadata beyond the overworld prop contract. The overworld loader separately supports looping morph-target weight animation for environmental props. RAE Gen 1-7/3DS exports must be rendered from `extras.rae.renderClass`, mesh `renderOrder`/`defaultVisible`, `eyeSheet`/`eyeExpression` UV metadata, and appearance variant metadata. Combined normal/shiny/form/pattern GLBs should expose root `extras.rae.appearanceVariants`; the renderer resolves form visibility/material swaps first and texture/color swaps second. Base-color eye sheets must not be routed through the older Violet-style `emissiveTexture`/`.lym` eye compositor unless the material actually exports that emissive-mask pattern.

The TEST ATTEND weather button is laid out and hit-tested by [`AttendOverlay.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/attend/AttendOverlay.cpp), a scene-specific adapter over the shared overlay canvas. Its bgfx-visible button surface is drawn by [`AttendBgfxRenderer.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/gameplay/attend/rendering/AttendBgfxRenderer.cpp) from the same rect and scene `ui.weatherButton` style data. Keep new shared overlay layout, anchoring, and hit-test rules in `ui/overlay`; keep scene meanings such as weather, feed, or tutorial actions in the consuming screen/module.

App-level routing for this placeholder lives in [`AppScreenCoordinatorAttend.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/app/screen/AppScreenCoordinatorAttend.cpp). The current return target is the title Resort flow. A future overworld-launched attend route should suspend the active overworld with `Overworld3DTestScreen::suspendForAttend()`, release heavyweight presentation resources while preserving overworld state, and resume with `resumeAfterAttend()` instead of `resetForNextLaunch()`.

### Transfer Flow Shell

- [`TransferFlowCoordinator.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TransferFlowCoordinator.cpp) owns the runtime transfer shell after the title menu requests TRANSFER. It starts async ticket scans and selected-save deep probes, owns concrete SDL screen instances, and reports high-level return/audio/SFX signals to `App.cpp`.
- [`TransferFlowController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_flow/TransferFlowController.cpp) owns the pure transfer-flow state machine: active transfer sub-screen, loading purpose, pending deep-probe selection, remembered last-viewed game box, return-to-title requests, and transfer-system entry requests.
- [`TransferSelectionBuilder.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_flow/TransferSelectionBuilder.cpp) converts `SaveLibrary` summaries/deep probes into UI-facing `TransferSaveSelection` data.

If a transfer flow rule can be tested without SDL or PKHeX, prefer `TransferFlowController` or `TransferSelectionBuilder`.

### Overworld Door Travel

Door visuals are RTPKS tile semantics; door behavior, links, and destination
anchors are OWMAP metadata. Pure lookup and script sequencing live under
`include/gameplay/world3d/doors` and `src/gameplay/world3d/doors`. The
`Overworld3DTestScreen` adapter intercepts movement before bounds/collision,
drives the circular transition, teleports the character, and dispatches
trigger-phase animation to `OverworldBgfxRenderer`. Door sequences themselves
remain data-authored in the overworld script catalog.

### Loading And Transfer Ticket Selection

- [`src/ui/loading`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/loading) owns reusable loading screens. `PokeballLoadingScreen.cpp` owns the black rotating Pokeball screen, and `ResortTransferLoadingScreen.cpp` owns the boat/resort transfer screen. Both are created through `LoadingScreenFactory` and read [`loading_screen.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/loading_screen.json); see [`docs/loading/README.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/loading/README.md).
- [`TransferTicketScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TransferTicketScreen.cpp) renders transfer tickets from already parsed transfer summaries. It should not probe saves or parse bridge JSON.
- [`TransferTicketListController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_ticket/TransferTicketListController.cpp) owns ticket list state: selection, scroll target/current offset, pointer drag/click behavior, rip activation timing, fade-to-black handoff, return-to-title requests, and selected-save handoff.

Ticket party sprites resolve through `PokeSpriteAssets` from parsed `party_slots`. Save names, trainer names, dropdown labels, and box titles should stay on the Unicode-preferring font path.

### Transfer System Screen

Read [`docs/transfer_system/README.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/transfer_system/README.md) before making transfer-system changes. It is the practical ownership guide for deciding whether a change belongs in config, a controller, focus graph, movement helper, presenter, renderer, backend service, or `TransferSystemScreen.cpp`.

[`TransferSystemScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TransferSystemScreen.cpp) is intentionally a **thin entrypoint** now (update orchestration + the smallest remaining screen glue). Most transfer-system behavior lives in focused shards under [`src/ui/transfer_system`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system).

The transfer-system screen layer still owns:

- concrete SDL screen lifecycle and asset/font handles
- adaptation from keyboard/controller/pointer input into controllers
- pointer hit-testing that depends on current rendered geometry
- in-memory game/resort slot arrays for the current non-persistent transfer prototype
- box viewport model refresh and texture prewarming
- application of temporary Pokemon/item moves to those in-memory slots
- selected-save/game title context
- some remaining cursor, speech-bubble, and mini-preview drawing helpers

**Working rule**: treat `TransferSystemScreen.cpp` as an orchestrator. If you are about to add a “specific” helper, first place it in the closest existing shard (or create one) under `src/ui/transfer_system/TransferSystemScreen*.cpp`.

New transfer-system behavior should first look for one of these smaller seams:

- [`GameTransferConfig.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/GameTransferConfig.cpp) parses [`game_transfer.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/game_transfer.json). If a visual/timing/style value is authorable, parse it here.
- [`TransferSystemUiStateController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/TransferSystemUiStateController.cpp) owns pure top-level UI state: first-enter fade, pill target, panel reveal, tool-carousel slide/selection, exit progression, and one-shot requests.
- [`GameBoxBrowserController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/GameBoxBrowserController.cpp) owns active game box index, Box Space mode/row offset, dropdown animation, highlighted row, scroll clamping, and dropdown selection.
- [`TransferInfoBannerPresenter.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/TransferInfoBannerPresenter.cpp) maps active context into lower-banner field values.
- [`TransferInfoBannerRenderer.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/TransferInfoBannerRenderer.cpp) draws the lower info banner, text fitting, generated symbols, PokeSprite item icons, and PokeSprite misc icons.
- [`PokemonActionMenuController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/PokemonActionMenuController.cpp) owns the normal-tool Pokemon action menu state, geometry, hit testing, and row selection.
- [`PokemonSummaryConfig.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/summary/PokemonSummaryConfig.cpp) parses [`pokemon_summary.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/pokemon_summary.json) for the Basic-tool Summary panel shell and future Summary content authoring.
- [`summary/README.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/summary/README.md) is the Summary handoff note: current behavior, integration points, and next implementation steps.
- [`summary/PokemonSummaryContent.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/summary/PokemonSummaryContent.cpp) is the temporary Summary content module. It only renders selected Pokemon names until the real Summary sections replace it feature-by-feature.
- [`ItemActionMenuController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/ItemActionMenuController.cpp) owns the item-tool modal pages, labels, geometry, hit testing, and row selection.
- [`PokemonMoveController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/PokemonMoveController.cpp) owns temporary Pokemon-in-hand state for action-menu moves and swap-tool moves.
- [`MultiPokemonMoveController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/MultiPokemonMoveController.cpp) owns temporary multi-Pokemon group state, original return slots, layout offsets, pointer mode, and pattern placement checks for the green multi tool.
- [`move/HeldMoveController.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/move/HeldMoveController.cpp) owns the generic held-object state for Pokemon, Box Space boxes, and held items.
- [`TransferSystemFocusGraph.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/TransferSystemFocusGraph.cpp) builds the deterministic keyboard/controller navigation topology for transfer-system controls.
- [`FocusManager.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/FocusManager.cpp) applies explicit directional edges, optional spatial fallback, activation callbacks, and current focus bounds.
- Transfer-system chrome and draw order are split into renderer shards under `src/ui/transfer_system/`:
  - `TransferSystemRendererMain.cpp` (orchestration + background)
  - `TransferSystemRendererTopBar.cpp` (pill + tool carousel)
  - `TransferSystemRendererDropdowns.cpp` (dropdown chrome/list)
  - `TransferSystemRendererMenus.cpp` (action menus)
  - `TransferSystemRendererHeld.cpp` (held sprites + multi-drag visuals)
- [`BoxViewport.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/BoxViewport.cpp) renders reusable 6x5 transfer box chrome from `BoxViewportModel`. It is not canonical Resort storage.

#### Focus And Navigation Ownership

Directional keyboard/controller navigation belongs in `TransferSystemFocusGraph` and `FocusManager`.

- Use explicit edges for predictable multi-panel behavior.
- Update focus-graph tests when adding or moving interactive controls.
- Keep raw neighbor wiring out of `TransferSystemScreen.cpp` unless it is only adapting current runtime geometry into focus nodes.
- Focused valid Pokemon slots and game icons should surface speech bubbles. Mouse-only bubble visibility remains hover-driven.

#### Held Movement And Pointer Gestures

Temporary held-object behavior is split deliberately:

- `PokemonMoveController` owns Pokemon move semantics for the normal action menu and swap tool.
- `MultiPokemonMoveController` owns selected-group layout and target pattern calculation for the multi tool.
- `HeldMoveController` owns generic held state for Pokemon, Box Space boxes, and held items.
- `move/Gestures.hpp` provides reusable hold/drag gesture helpers for Box Space and quick-drop style behavior.
- `TransferSystemScreen` applies accepted moves to current in-memory slots, refreshes viewport models, and requests pickup/putdown SFX.

Movement remains in-memory for the external-save column. **Resort column persistence**: dropping a game PC Pokémon onto a Resort slot calls `PokemonResortService::importParsedPokemon` using merged bridge import bytes (`PcSlotSpecies.bridge_box_payload_*`) plus `source_game` parsed from the bridge `import` stdout. Resort-only moves continue to use `movePokemonToSlot` / `swapResortSlotContents`. External save write-back remains bridge-layer work.

#### Item Tool Behavior

The yellow item tool currently supports temporary in-memory held-item movement between occupied Pokemon slots.

- `ItemActionMenuController` owns modal pages and actions such as Move Item, Swap Item, Put Away, Back, and Cancel.
- `TransferSystemScreen` applies item pickup, place, swap, cancel, and current modal consequences to `PcSlotSpecies.held_item_id/name`.
- Bag/deposit/storage persistence is not implemented. Put-away style actions should remain explicit placeholders until backend and bag rules exist.
- Item icons should go through `PokeSpriteAssets`, not manually built file paths.

#### Renderer Boundaries

Prefer visual work in render helpers:

- lower info banner visuals: `TransferInfoBannerRenderer`
- transfer-system chrome/draw order/action-menu drawing: `TransferSystemRenderer`
- reusable box chrome: `BoxViewport`
- title/menu/options rendering: `TitleScreenRender`

`TransferSystemScreen.cpp` still contains some geometry-dependent cursor, speech-bubble, mini-preview, and adaptation drawing. When touching those areas, consider whether the next useful step is extraction to a renderer helper with focused harness coverage.

## Save, Bridge, And Backend Data Flow

### Transfer Probe Flow

1. `SaveLibrary` scans the top-level `saves` directory without recursion.
2. It hashes candidate files and checks the transfer probe cache.
3. On a cache miss or stale cache, `SaveBridgeClient` launches the .NET bridge.
4. The bridge emits one JSON object.
5. `SaveLibrary` parses light ticket summaries plus richer `PcSlotSpecies` party/box models.
6. `TransferSelectionBuilder` converts those models into `TransferSaveSelection`.
7. Ticket and transfer-system screens render from parsed native models.

Do not build new UI directly against raw bridge JSON or legacy `box_1` strings. Use parsed native models and extend them when needed.

### Options Persistence Flow

1. `OptionsMenuController` changes settings.
2. `TitleScreen` raises a save request event.
3. `App.cpp` converts scene settings into `SaveData`.
4. `SaveDataStore` writes primary and backup saves atomically.

### Canonical Resort Storage Flow

The Resort backend is separate from the options save file and from transfer-ticket preview data.

- `App.cpp` resolves the same portable save directory as options (`title_screen.json` persistence: SDL pref path or project `save/` fallback), opens `PokemonResortService` at `save_directory / persistence.resort_profile_file_name` (default `profile.resort.db`), runs migrations, and calls `ensureProfile("default")` so default empty Resort boxes exist if missing (idempotent).
- Repositories own SQL and migrations.
- `BridgeImportAdapter` parses import-grade bridge JSON into `ImportedPokemon`.
- `PokemonMatcher` owns identity lookup.
- `PokemonMergeService` owns canonical merge policy.
- `PokemonResortService` orchestrates import, placement, history, profile boxes, and read models.
- `PokemonExportService` and `MirrorSessionService` own export projection snapshots and managed mirror sessions.

The transfer screen still uses in-memory UI slot state. See [`docs/backend/frontend_integration.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/backend/frontend_integration.md) before replacing that with backend-backed storage.

Durable cross-generation travel is migrating to an OpenHome-first model. Keep these docs current before changing backend or transfer persistence contracts:

- [`docs/openhome-first-mirror-retirement.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/openhome-first-mirror-retirement.md)
- [`docs/openhome-persistent-identity-integration.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/openhome-persistent-identity-integration.md)
- [`docs/transfer_system/SAVE_EXIT_SAFETY.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/transfer_system/SAVE_EXIT_SAFETY.md)

`docs/transfer_system/MIRROR_PROJECTION_ARCHITECTURE.md` remains as legacy design history only.

### Future: explicit player save bootstrap

Not implemented yet; current builds still initialize Resort storage on startup for transfer MVP work.

Planned direction:

- Require an explicit **New Game** (or equivalent) action on the main menu before treating a player profile as created; until then, block **Transfer** and other flows that need durable Resort state.
- That flow may temporarily hide or replace the normal main-menu buttons with a focused setup step.
- Grow the JSON save (`SaveData` / primary `.sav`) beyond user options to carry game-level profile metadata (identity, progression flags, etc.) while keeping Resort canonical Pokemon in SQLite.
- Optionally move first-time Resort DB creation and `ensureProfile` from implicit `App.cpp` startup into that explicit flow.

When implementing this, keep [`title_screen.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/title_screen.json) `persistence` (save directory + `resort_profile_file_name`) as the single portable path contract so Resort files and the options save stay aligned.

## Configuration Surface

Shared application config lives in [`app.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/app.json):

- desktop window size
- renderer logical size and design coordinates
- app title
- input bindings
- shared audio assets and default volumes

The target/design resolution is `1280x800`. The desktop preview window is smaller by default; changing the preview window is not the same as changing logical/design coordinates.

Screen config lives in:

- [`title_screen.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/title_screen.json)
- [`loading_screen.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/loading_screen.json)
- [`transfer_select_save.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/transfer_select_save.json)
- [`game_transfer.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/game_transfer.json)
- [`pokemon_summary.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/pokemon_summary.json)

See [`docs/config/README.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/config/README.md) for ownership guidance and [`docs/config/game_transfer.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/config/game_transfer.md) for the transfer-system config reference.

## Testing Map

Use [`tests/README.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tests/README.md) as the canonical test source. As of the current build, the native CTest suite includes storage/backend, title controllers, transfer ticket, transfer flow, transfer-system config/state/browser/action-menu/focus, input/config, PokeSprite and Pokemon cry assets, save-library cache, headless boot, title flow harness, transfer-system harness, and transfer-ticket Unicode harness coverage.

Run:

```bash
cd /Users/vanta/Desktop/title_screen_demo/pokemon-resort
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Run .NET bridge tests sequentially when touching the bridge, save probing, import-grade JSON, write-projection validation, or bridge-backed native import.

## Build And Platform Notes

- Build system: [`CMakeLists.txt`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/CMakeLists.txt)
- Primary target: `title_screen_demo`
- Backend/test utility target: `resort_backend_tool`
- Native tests are registered with CTest; do not rely on a single executable name.
- Language mode: C++17 plus Objective-C++ for the current macOS audio backend.
- Current platform assumptions: macOS with `SDL2`, `SDL2_image`, `SDL2_ttf`, `AVFoundation`, and `Foundation`.
- Save parsing bridge: external .NET helper under [`tools/pkhex_bridge`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tools/pkhex_bridge); guarded write-back logic is split under `tools/pkhex_bridge/WriteBack/` so `BridgeWriteBack.cs` stays orchestration-only and new projection domains add appliers instead of growing one file past ~500 lines.
- Shipping bridge path: publish a self-contained helper and bundle it next to the executable or inside app resources.

For cross-platform work, isolate OS-specific behavior behind small adapters. Avoid adding platform checks inside scene logic.

## Architectural Strengths

- Clear process boundary around PKHeX.
- Strong typed config boundary for app/title settings and dedicated parser for transfer-system config.
- Many high-risk transfer behaviors now have pure controller seams.
- Save writes are defensive and backup-aware.
- Resort backend separates canonical storage from preview/ticket UI models.
- Tests cover both narrow controller contracts and SDL harness flows.

## Current Architectural Constraints

- Some transfer-system code is still “too big” and should continue splitting when touched (for example: config parsing and banner rendering files that have grown past the preferred 500-line target).
- Keep a hard modularity budget in the transfer system: if any transfer-system `.cpp` grows beyond **500 lines**, split it into smaller components/shards as part of the same change.
- Transfer-system Pokemon/item movement uses in-memory UI models; Resort persistence goes through `PokemonResortService`. External game saves can persist **PC box names and PC box slot layout** via PKHeX bridge `write-projection` (`projection_schema` 2) using import-grade encrypted PKM payloads captured at transfer-system entry.
- Bridge `write-projection` performs optional real save mutation only through validated projections (backup copy first); unsupported edits remain out of scope until modeled in the projection.
- Audio is still macOS-specific.
- Some render responsibilities remain split between screen methods and renderer helpers.

## Recommended Next Refactors

Near-term:

- Continue extracting geometry-dependent transfer cursor, speech-bubble, and mini-preview drawing from `TransferSystemScreen` when tests can protect the move.
- Add focused tests around held item behavior before expanding bag/deposit semantics.
- Keep adding transfer config fields through `GameTransferConfig` rather than one-off JSON reads.
- Add small platform/service adapters for audio, save-path resolution, and helper-process spawning before serious cross-platform work.

Mid-term:

- Replace transfer-system in-memory slot state with canonical Resort storage read models and explicit persistence/write-back flows for Resort-bound Pokémon.
- Extend external-save editing only through `write-projection` and import-grade payloads (no ad hoc PKM guessing in native code).
- Add platform-specific build/package notes when Windows, Linux, or Android become active targets.
- Consider a broader screen/router abstraction only when gameplay requirements justify it; do not introduce one just to hide current flow complexity.

## Handoff Notes For Future Agents

Read task-specific files rather than always starting from `TitleScreen.cpp`:

- Use `AGENTS.md` for the task-oriented first-read list.
- Use this architecture doc for module boundaries.
- Use `docs/transfer_system/README.md` before changing transfer-system behavior, layout, movement, focus, banner, or rendering code.
- Use `docs/config/README.md` before adding JSON fields.
- Use `tests/README.md` before deciding what to run.
- Use `PKHEX_BRIDGE.md` before touching save probing, bridge JSON, import-grade reads, or write-back validation.
- Use backend docs before building UI against canonical Resort storage.

When in doubt, preserve existing seams: config parser, pure controller, presenter, renderer, bridge boundary, save-library parser/cache, backend service. New behavior should usually extend one of those before expanding `App.cpp`, `TitleScreen.cpp`, or `TransferSystemScreen.cpp`.
