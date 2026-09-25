# Aquarium Future Foundations

Status: deferred roadmap  
Recorded: 2026-09-25

This document preserves the next architectural steps for the aquarium after the
current construction, stocking, exhibit, decoration, visitor, inspection, room,
and terrain work. It is intentionally a restart document, not an instruction to
continue expanding aquarium scope now.

The recommended product decision is to stabilize the current aquarium and return
to the wider Pokémon Resort game. Resume this roadmap when players have enough
rooms to become lost, aquarium progression needs additional exhibit space, or the
main game loop explicitly unlocks new rooms or storeys.

## Deferred player features

### Building map and room travel

Provide a readable top-down building map that:

- Shows discovered rooms and their connections.
- Clearly identifies the current room, entrances, and unavailable destinations.
- Allows direct travel to a discovered, reachable room.
- Uses the normal room-transition/loading pipeline rather than teleporting world
  objects inside the active scene.
- Preserves independent per-room tanks, decorations, stocking, visitors, and
  construction documents.
- Works with keyboard, mouse, and controller without requiring pointer precision.

The first version should present the building topology clearly rather than
pretending that independently loaded rooms already form one metrically accurate
floor plan. Room cards or simplified footprints connected by door lines are
acceptable. A literal architectural drawing should wait until room/storey
coordinates become authoritative.

Direct travel should initially be available only when:

- No construction draft, stocking operation, inspection view, dialogue, or room
  transition is active.
- The destination has been discovered and remains connected to the entrance.
- The destination has a validated arrival anchor.

Use the existing loading/transition owner, restore the follower consistently,
and place the player at the destination's designated map-arrival anchor. Do not
duplicate map loading, aquarium simulation startup, audio, or GPU ownership in
the map UI.

### Multiple storeys and vertical room connections

Add true building storeys only after the map works against the existing room
graph. Current per-cell `floorDepthOverrides` are depressions within one room;
they are not building floors and must not be repurposed as storey identity.

A future storey system should support:

- A room belonging to a stable integer storey.
- Creating a new room from a validated stair landing.
- Independent rooms above or below one another.
- Multiple horizontal doors and vertical connections per room.
- Upper-floor openings, balconies, and overhangs as explicitly authored floor
  coverage—not renderer exceptions.
- Clear camera and occlusion behavior when one storey can see another.
- Visitors and followers routing through vertical connections.

Room-to-room stairs need a new vertical connection contract. Do not overload
`RoomTransition`: that type currently describes one-room terrain steps and owns
lower/upper cells inside the same room.

## Existing foundation to preserve

The current room document already provides useful graph primitives:

- `RoomLayout::id` is stable.
- Each room owns a vector of `RoomDoor` endpoints.
- `DoorConnection` links room/door IDs rather than coordinates.
- Each endpoint stores its own wall and offset.
- Moving one endpoint does not move its peer in the connected room.
- A newly created receiving endpoint is centred on its own opposite wall.
- Runtime travel resolves the destination endpoint and facing.
- Every door must have exactly one internal or external connection.
- Building persistence is versioned, transactional, backed up, and protected
  against newer schemas.

These rules match the desired independent-door behavior. A west-wall door in
room A may connect to an east-wall door in room B; moving B's endpoint along its
east wall must not alter A. Connections describe topology, not shared world
coordinates.

The data model already permits multiple doors in a room. The current player UI
is more restrictive: it exposes a plus handle only on a wall without a door and
does not expose moving existing endpoints. Future work should extend the UI and
validation rather than replacing the ID-based connection model.

Primary source areas:

- `include/gameplay/world3d/aquarium/rooms/AquariumBuildingLayout.hpp`
- `src/gameplay/world3d/aquarium/rooms/AquariumBuildingLayout.cpp`
- `src/gameplay/world3d/aquarium/rooms/AquariumBuildingLayoutJson.cpp`
- `src/gameplay/world3d/aquarium/rooms/AquariumBuildingScene.cpp`
- `src/gameplay/world3d/aquarium/rooms/AquariumRoomRuntime.cpp`
- `src/ui/Overworld3DTestScreenAquariumRooms.cpp`
- `src/ui/Overworld3DTestScreenAquariumRoomHandles.cpp`
- `tests/native/world3d/aquarium_room_layout_tests.cpp`
- `docs/gameplay/aquarium-rooms.md`
- `docs/development/aquarium-construction-checklist.md`

## Proposed future contracts

Names below are illustrative. Settle exact DTO names in an ADR before changing
the persisted schema.

```cpp
struct RoomPlacement {
    std::string room_id;
    int storey = 0;
    int plan_column = 0;
    int plan_row = 0;
};

struct VerticalEndpoint {
    std::string room_id;
    std::string landing_id;
    Cell landing_cell;
    Wall facing;
    int width_cells = 3;
};

struct VerticalConnection {
    std::string id;
    VerticalEndpoint lower;
    VerticalEndpoint upper;
};

struct RoomFloorCoverage {
    // Authored room-local cells. Absence means the current full rectangle.
    std::vector<Cell> included_cells;
    std::vector<Cell> openings;
};
```

`RoomPlacement` gives the map a stable spatial layout without making door links
coordinate-dependent. `VerticalConnection` connects rooms without conflating
building travel with terrain stairs. `RoomFloorCoverage` is the eventual basis
for balconies and openings.

Before overhangs are enabled, validation must define whether unsupported upper
cells are allowed. The recommended rule is:

- Upper floor cells may extend beyond lower-room coverage as a deliberate
  overhang.
- Their geometry owns a visible underside and edge fascia.
- Large props, tanks, and structural loads are allowed only where the authored
  construction rules say support is sufficient.
- Openings require guardrails or an explicit barrier contract and must never
  expose unwalkable collision seams.

Do not infer support from whatever geometry happens to render beneath a cell.

## Multiple-door rules

When this work resumes:

1. Allow more than one endpoint on a wall.
2. Require door spans and protected inward lanes not to overlap.
3. Keep at least one valid circulation route among all landings.
4. Continue limiting an endpoint to the wall on which it was created.
5. Centre a newly proposed receiving door using the receiving room's wall
   length, not the source room's wall length.
6. After creation, allow each endpoint to slide independently along its own wall.
7. Never derive connection identity from offsets, wall length, map IDs, or room
   array order.
8. Preserve the exterior entrance as an explicit external connection.

If a receiving wall cannot fit the default three-cell doorway and its protected
lane, room creation must remain a preview with a clear invalid state. It must not
silently resize an existing room or shift another endpoint.

## Suggested delivery order

### F0 — Freeze and baseline

- Keep the current building schema and saves unchanged.
- Fix only crashes, data loss, unusable actions, and severe navigation failures.
- Record representative one-room and multi-room fixtures and current load times.
- Return development focus to the wider Resort game.

### F1 — Read-only topology map

- Derive a map view model from `BuildingLayout`.
- Show current/discovered rooms and door links.
- Add controller focus and accessible non-colour-only states.
- Do not add fast travel, storeys, or schema fields yet.

Acceptance: every valid current building renders deterministically; malformed or
disconnected graphs fail safely without affecting room entry.

### F2 — Map travel

- Add discovered-room persistence if progression requires it.
- Route selection through the existing transition/loading pipeline.
- Define stable arrival anchors and cancellation/error recovery.

Acceptance: travel produces the same loaded room state as walking through its
door, including follower, visitors, music, tanks, and input ownership.

### F3 — Multiple doors per wall

- Add door creation, selection, movement, and deletion gestures.
- Extend overlap, circulation, resize, and connection validation.
- Add building-level undo/redo before making graph edits broad.

Acceptance: several independently positioned endpoints on one wall survive
save/reload and always resolve to their linked peers.

### F4 — Storey graph and stair-created rooms

- Version the building schema with room placements/storeys and vertical links.
- Create an upper/lower room from a stair landing with a centred receiving
  landing, then allow that landing to move within its valid edge/footprint.
- Extend the map to group rooms by storey.

Acceptance: horizontal and vertical travel share one recoverable transition
pipeline; old schema-5 buildings migrate without changing their current layout.

### F5 — Openings, balconies, and overhangs

- Add explicit floor coverage/opening authoring.
- Generate tops, undersides, edge fascia, collision, occlusion, and navigation.
- Validate tanks, props, doors, stairs, fall barriers, and visibility through
  openings before commit.

Acceptance: upper rooms can expose a safe view into lower spaces without holes,
z-fighting, invalid collision, or camera culling regressions.

Stop for player review after every phase. Do not combine the storey schema,
overhang geometry, map UI, and multiple-door editor into one milestone.

## Verification required when resumed

- Canonical JSON round trips and sequential migration from schema 5.
- Primary/backup recovery and newer-version read-only behavior.
- Stable room, door, landing, and connection IDs through edits and undo/redo.
- Multiple endpoints on every wall, minimum spacing, and protected lane checks.
- Reachability from the exterior entrance to every enabled room.
- Map layout determinism and controller-only navigation.
- Map travel equivalence with physical-door travel.
- Follower, visitor, aquarium music, camera, and input restoration.
- Per-room tank/stocking/decor isolation across horizontal and vertical travel.
- Stair landing, floor opening, barrier, collision, and navigation agreement.
- Rendering/culling with rooms above, below, and visible through openings.
- Repeated transition/resource-lifetime stress tests and measured load times.

## Explicit non-goals for the current development phase

- No storey or overhang implementation while core Resort systems remain
  incomplete.
- No conversion of existing terrain depressions into building storeys.
- No global seamless simulation of every aquarium room.
- No coordinate-coupled door pairs.
- No speculative room-shape rewrite merely to support the map.
- No aquarium-only alternative to shared loading, input, camera, audio, or save
  ownership.

## Restart checklist

Before implementing any item in this roadmap:

1. Read repository guidance and the two current aquarium gameplay documents.
2. Inspect the dirty worktree and preserve all player-authored aquarium saves.
3. Re-run focused room-layout, runtime, construction, visitor, and transition
   tests to establish the new baseline.
4. Inspect the current building schema and UI; this document records intent, but
   live code remains authoritative.
5. Confirm which single phase is approved and define its acceptance criteria.
6. Add an ADR before introducing storey placement, vertical connections, or
   floor-coverage schema fields.
7. Work one reviewable phase at a time and update the aquarium construction
   checklist with measured results and player-review findings.

