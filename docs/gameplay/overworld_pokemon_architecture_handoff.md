# Overworld Pokemon Architecture Handoff

This is a technical handoff for planning Pokemon appearing in the Resort overworld. It is intentionally not an implementation plan or design spec; it records what the production code looks like today, what boundaries must be preserved, and what the future architecture must allow.

## Current Code Shape

The active 3D Resort entry point is `RESORT -> 3D TEST`. Runtime composition currently flows through:

- `src/core/App.cpp`: constructs app services, including `PokemonResortService`, `TransferFlowCoordinator`, and `Overworld3DTestScreen`.
- `src/core/app/screen/AppScreenCoordinator*.cpp`: switches between title, loading, transfer flow, and `Overworld3DTestScreen`.
- `src/ui/Overworld3DTestScreen.cpp`: owns the current test-map screen, loads `assets/overworld/maps/testing.owmap`, updates the player, camera, follower, effects, and rendering.
- `include/gameplay/world3d/Overworld3DConfig.hpp`: central scene read model for maps, terrain, player spawn, models, NPC config, and rendering config.
- `src/gameplay/world3d/data/OwmapOverworldLoader.cpp` and `SceneMetadataParser.cpp`: load `.owmap` binary terrain plus embedded metadata JSON into `SceneConfig`.
- `src/gameplay/world3d/characters`: player-style movement and sprite animation.
- `src/gameplay/world3d/followers`: single follower Pokemon prototype, including summon config and nature idle behavior.
- `src/gameplay/world3d/rendering`: map and billboard sprite rendering.

The current map format is `.owmap`, documented in `docs/gameplay/owmap_format.md`. It contains a binary header, UTF-8 metadata JSON, byte terrain heights, byte special tiles, and bit-packed collision. Metadata currently supports `visual`, `grid`, `player`, `camera`, `lighting`, `models`, and `characters`. There is no map-level Pokemon spawn-point runtime contract yet.

The current default test map is hardcoded in `Overworld3DTestScreen.cpp` as `assets/overworld/maps/testing.owmap`.

## Existing Idle Behavior Pattern

The closest existing behavior pattern is the follower idle system:

- Config: `config/gameplay/followers/idle_behaviours.json`
- Loader: `NatureIdleConfig.cpp`
- Planner: `NatureIdlePlanner.cpp`
- Runtime executor: `FollowerController.cpp`

Useful qualities to preserve:

- Data-driven weights by nature group and per-nature modifier.
- Behavior planning separated from behavior execution.
- Runtime validates authored values and clamps unsafe config.
- Behavior resolves to small action primitives: move path, wait, face, poke, jump, sleep.
- Pathing respects terrain, collision, ramp rules, occupied tiles, and player occupancy.
- Debug label exists behind config: `enable_active_idle_behavior_debug`.

Do not expand this single-follower controller into general overworld Pokemon AI. It is player-relative, single-actor, and session-json driven. Overworld Pokemon need multi-actor scheduling, perception, reservations, smart-object use, and interaction handshakes.

## Existing Resort Data

Canonical Resort storage lives in `include/resort` and `src/resort`. Important entry points:

- `PokemonResortService`: orchestration facade for profile setup, box views, imports, exports, placement, OpenHome linkage, and mirror sessions.
- `getBoxSlotViews(profile_id, box_id)`: returns `PokemonSlotView` read models for Resort boxes.
- `PokemonSlotView`: exposes `pkrid`, species fields, display name, level, shiny, gender, held item, ability, nature, primary/secondary type, source game, ball, and status-like display metadata.
- `ResortPokemon`: deeper canonical record with hot/warm/cold data and identity.
- `PokemonPresenceState`: Pokemon can be available in Resort, away in game, pending return, or unavailable.

Critical identity rule: `pkrid` is the durable Resort identity. Pokemon are not stable because they can leave the Resort, return from games, evolve, change form, or come back with new snapshots. Overworld runtime state must treat live actors as projections of current Resort records, not as permanent owners of Pokemon data.

The transfer UI has a helper that maps `PokemonSlotView` into `PcSlotSpecies` in `TransferSystemScreenLifecycle.cpp`. That is useful reference for which display fields are already surfaced, but overworld code should not depend on transfer UI models long term.

## Architecture Boundaries To Preserve

The current domain rules matter:

- `engine` cannot import `gameplay`, `transfer`, or `resort`.
- `gameplay` may depend on `engine` and `transfer/contracts` only.
- `gameplay` cannot import `resort/*` or transfer internals.
- `ui` may compose gameplay and app services.

Because of that, an overworld Pokemon system should not query `PokemonResortService` or SQLite from inside `gameplay/world3d`. Use an app/UI composition adapter to fetch Resort box data and pass stable gameplay-facing snapshots into the world. If a reusable contract is needed, put it in an allowed contract layer, not in `resort` directly.

Recommended production direction:

- Keep authored map spawn-point data in the overworld data layer.
- Keep live Pokemon actor simulation in a new `gameplay/world3d/pokemon` folder.
- Keep reusable behavior primitives in subfolders, not inside `Overworld3DTestScreen`.
- Keep Resort storage reads in app/UI composition or a contract adapter.
- Keep smart object definitions and behavior authoring data-driven.

## Spawn-Point Requirements

The map maker will later author Pokemon and character spawn points. Runtime should expect map metadata to grow with spawn-point definitions rather than hardcoding test coordinates.

A spawn point should be treated as an authored capability zone, not only an `(x,y)` tile. It should be able to describe:

- Stable spawn-point id.
- Tile/world position and optional facing.
- Slot capacity and occupancy policy.
- Area tags such as `pool`, `grass`, `shore`, `indoors`, `path`.
- Movement bounds or allowed radius.
- Required or forbidden Pokemon traits.
- Default behavior set or weighted behavior profile.
- Nearby points of interest, entry/exit nodes, and smart-object links.
- Whether a Pokemon may leave the local zone after spawning.

Initial trigger can be scene-open based, but the architecture should isolate trigger policy from spawn resolution. Later triggers may be timed, scene-enter, story/event driven, or explicit refresh.

## Resort Box To Overworld Flow

The first runtime flow should be:

1. Scene opens.
2. App/UI composition asks the Resort profile for available box Pokemon.
3. A gameplay-facing snapshot list is built from `PokemonSlotView`/canonical data.
4. Spawn resolver matches available Pokemon to available map spawn points.
5. Live actor instances are created with `pkrid` identity plus immutable per-scene snapshot fields.
6. Actors run behavior using the scene context, authored spawn context, player context, other actor context, and smart object context.

Do not bind actors to box indices as identity. Box location can influence spawn ordering or availability, but `pkrid` should be the actor identity key. If a Pokemon leaves the Resort or becomes unavailable, the next scene-open spawn pass must omit it or despawn it safely.

Open question for the implementation plan: whether to use only visible Resort box slots at first, or all `AvailableInResort` Pokemon. The current backend has `getBoxSlotViews` for boxes; a broader availability query may need a new service/read-model method outside gameplay.

## Behavioral Context Requirements

The behavior context must be richer than the current follower idle planner. It should include:

- Self: `pkrid`, species id/slug/name, form, size class, types, nature, level, held item, relationship/friendship fields when available, current activity/state, cooldowns.
- Scene: terrain, collision, height, ramps, water/pool tags, spawn zones, points of interest, smart objects.
- Player: tile/world position, facing, interaction availability, relationship value with the Pokemon, whether the player is in a minigame or dialogue.
- Other Pokemon: positions, states, availability for interaction, relationship/favorite-friend metadata when available.
- Reservations: claimed tiles, claimed smart objects, claimed interaction partners, claimed sequence roles.
- Behavior filters: species/type/size/nature constraints and map-authored constraints.
- Randomness: weighted but deterministic enough to test with injected RNG.

Examples the system must support later:

- High-relationship Pokemon detects player in range, approaches, emotes, allows interaction, and can follow near the player like a partner while in range.
- Pokemon detects another Pokemon, a pool entry node, and an object of interest, then chooses based on constraints, nature, weights, and current reservations.
- Pokemon-to-Pokemon activities such as chase, emote exchange, sequence jumps, and following.
- Smart-object activities such as two Pokemon passing a ball back and forth.
- Pokemon-initiated player interaction that can show an emote, open dialogue, offer an optional minigame, and allow the player to opt out.

## Interaction Handshakes

Multi-actor interactions need a handshake before execution. The initiator should not simply start an animation and hope the target follows.

A robust flow should include:

- Candidate selection.
- Capability and constraint check for all participants.
- Reservation request for participants, path endpoints, and smart object roles.
- Accept/decline from each participant based on current state.
- Committed interaction contract with roles and ordered sequence steps.
- Execution.
- Completion, cancellation, or timeout cleanup that releases reservations.

This applies to Pokemon-Pokemon interactions, Pokemon-player interactions, and smart-object interactions. The player can be represented as a participant role for optional minigames.

## Smart Objects

Smart objects should own how they are used. A ball, pool entry, bench, toy, or food station should expose:

- Interaction id and required roles.
- Required participant traits.
- Valid approach points and orientation.
- Optional minigame entry for player participation.
- Sequence/action definition for participants.
- Cooldowns, occupancy, and cancellation policy.

Pokemon behavior should ask smart objects for available interactions rather than hardcoding ball or pool behavior in the Pokemon controller.

## Folder And Module Guidance

For planning, assume new code should live under dedicated folders, for example:

- `include/gameplay/world3d/pokemon/`
- `src/gameplay/world3d/pokemon/`
- `include/gameplay/world3d/pokemon/behaviors/`
- `src/gameplay/world3d/pokemon/behaviors/`
- `include/gameplay/world3d/pokemon/spawning/`
- `src/gameplay/world3d/pokemon/spawning/`
- `include/gameplay/world3d/smart_objects/`
- `src/gameplay/world3d/smart_objects/`

Keep files focused. Avoid large scripts or generated utilities over 500 lines. Prefer small config parsers, pure planners, actor controllers, and tests.

Likely integration points later:

- Extend `Overworld3DConfig.hpp` with spawn-point and smart-object read models.
- Extend `SceneMetadataParser.cpp` to parse spawn-point metadata from `.owmap` JSON.
- Extend `docs/gameplay/owmap_format.md` when the metadata contract changes.
- Add gameplay tests near `tests/native/world3d/` for parsing, spawn resolution, behavior scoring, reservations, and handshakes.
- Compose the Pokemon system in `Overworld3DTestScreen` only as a screen adapter, not as the behavior owner.
- If `gameplay` needs Resort Pokemon data, define a narrow read-model/contract that does not import `resort`.

## Testing Expectations

Start with pure tests before SDL rendering:

- Spawn-point parser accepts authored metadata and rejects unsafe values clearly.
- Spawn resolver handles empty boxes, more Pokemon than slots, more slots than Pokemon, unavailable Pokemon, and trait mismatches.
- Behavior scoring filters impossible actions before applying weights.
- Fire Pokemon cannot select water-only/swim-only behavior unless explicitly allowed by authored override.
- Reservation manager prevents two Pokemon from claiming the same role/tile/object.
- Handshake failure leaves both actors in a valid idle state.
- Smart-object contract can place two Pokemon and optionally the player into assigned roles.

SDL/harness coverage should be added only after the core contracts are stable and player-visible behavior appears in the screen.

## Non-Goals For First Implementation

- Do not build a full behavior tree framework before the production contracts are clear.
- Do not store durable friendship/relationship state in live actor objects.
- Do not let gameplay systems own SQLite or Resort service calls.
- Do not hardcode pool/ball/player-follow logic inside one giant Pokemon controller.
- Do not make spawn points depend on transfer UI `PcSlotSpecies` as the long-term model.
- Do not treat the current follower session JSON as the real source of Resort Pokemon.

The clean first milestone is a production-shaped vertical slice: scene-open spawn resolution from current Resort availability into map-authored spawn points, with a small but extensible actor/behavior context and testable reservations. The design layer can then choose exact behavior names, weights, and interaction content without changing the core boundaries.
