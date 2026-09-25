# Independent aquarium rooms

Deferred building-map, multiple-door, storey, and overhang work is recorded in
[Aquarium Future Foundations](aquarium-future-foundations.md). Those features are
intentionally frozen while development returns to the wider Resort game.

## Controls

Enter Room mode using the room-outline circle, R, or controller right-stick click.
The top row selects Layout, Floor, Levels, Transitions, Walls, or Decorations.
In Levels, click two opposite cells or press-drag-release to select a rectangular
section. A vertical knob then offers ground level plus five whole-tile depression
levels. Dragging or stepping the knob rebuilds the procedural floor and retaining
geometry immediately; Cancel restores the selection's previous terrain. The
normal angled construction camera is the default so vertical changes remain
legible. The overhead camera button is an optional planning view for large
selections, with pan available while it is active.

Transitions provides one Stairs tool. Mark a uniform section of lower floor
beside an existing higher ledge. Height and cardinal direction are inferred; no
direction picker is needed. The tool raises only the stair blocks required for
the rise, retaining the marked width, and cannot cut a flight through higher or
mixed terrain. Marking any visible stair block removes the whole generated
flight and restores its original lower floor. Stairs render five
palette-matched treads and darker risers per level. Black 2 tile 140 was
evaluated, but it contains only two textured slope quads and reads as a ramp in
this room. Stairs use the existing smooth directional-ramp traversal contract.
Legacy saved ramps remain loadable and display as stairs. Each generated stair
edge connects exactly one floor level. Editing floor heights clips the painted
rectangle to the room and removes any touched or invalidated stairs instead of
rejecting the edit. This does not alter overworld ramp rules.

Four wall knobs replace the width/depth toolbar. Drag/release a knob, or click,
move, then click again. Walls snap to whole cells; a dotted outline previews them.

Each wall without a door has a green plus handle. Select it and move along the
wall to position a three-cell doorway. Finishing adds a candidate room with a
centred opposite-wall doorway. Checkmark/Y saves the building. Cancel/B first
cancels the gesture, then the room draft. Arrows/D-pad select handles; A picks up
or finishes a handle. Directional input adjusts it. Wheel zoom is available
between gestures. Existing-door sliding is not exposed yet.

## Boundaries and invariants

AquariumBuildingLayout owns signed room bounds and stable door endpoint links.
AquariumBuildingLayoutJson owns canonical versioned JSON. Runtime projection
keeps scenes zero-based and records their stable room origin. RoomHandles caches
occupancy per gesture; commit revalidates fresh occupancy.

- North/west wall changes preserve the opposite edge and existing tank positions.
- Resizing one room never changes another room or its doorway offset.
- Connections join room/door IDs, not coordinates.
- Receiving rooms default to 17×13, or 13×17 for east/west entrances.
- Doors reserve three cells across and two inward. All three halo trigger cells
  resolve their linked destination; internal travel follows destination facing.
- Full shifted tank footprints must clear the inner wall face by half a cell.
  Bounds, doorway lanes, tank occupancy and door-to-door circulation are validated.
- Floor depths are integers from zero through five and use one world tile per
  step. The normal floor retains world Y=0; negative world positions are derived.
- Boundary and doorway-lane cells may be lowered. Side/back procedural wall
  segments extend downward while preserving the established wall-top height.
  A height selection that changes a tank platform must contain its complete
  support footprint or the entire edit is rejected. Partial overlap is allowed
  when painting the level the tank already occupies because it changes nothing.
  Selections may extend beyond the room; only in-bounds cells are changed.
  Decorations and doorway lanes do not veto height painting.
- Terrain generation adds vertical retaining faces between different levels.
  Unequal flat edges remain blocked unless an authored transition joins two
  cardinally adjacent cells whose depths differ by exactly one. The lower cell
  owns the directional ramp special and inherits its floor-cell palette. Only
  the connected retaining-wall segment is omitted; adjacent height edges keep
  their walls. Stair meshes own the rendered transition surface while retaining
  the same directional-ramp movement contract. Transitions cannot occupy tank
  cells, doorway lanes, or room-decoration anchors.
- The aquarium-only black south facade follows the authored south-edge floor
  height. It continues below that floor to hide tank undersides, but lowered
  sections reaching the cutaway edge remain visible instead of being covered
  up to the original room datum. The short south wall/trim is omitted on those
  lowered segments so it does not leave a floating rim across the opening.
- The south external connection remains intact; the old gallery stays retired.

## Persistence and rendering

The profile's aquarium_builder_lab.room.json stores the whole building using
building schema 5, synced/read-back atomic promotion and backup recovery. Sparse
`surfaces.floorDepthOverrides` entries author `{column,row,depth}`; absent cells
remain level zero. Terrain vertices, retaining faces, collision edges, and world
heights are derived rather than persisted.
Room transitions author stable IDs, `ramp`/`stairs` kind, and lower/upper local
cells. Directional terrain specials and stair render meshes are derived.
Each room retains its independent roomId.aquarium.json for geometry and stocking.
OWMAP assets are not rewritten.

Tank document envelope v6 records roomFrame: {column, row}. Legacy documents use
frame zero; local footprint origins are rebased on load. Room edits do not rewrite
tank saves. The next confirmed tank edit persists its current frame. Geometry
kernel schema/ABI stays unchanged. Newer documents are protected read-only.

Commits stage CPU scenes, save the building, then rebuild through the existing
map-transition renderer owner. A crash after save promotion recovers the new
layout on load. This is not an asynchronous GPU transaction. Tank history is
retained for unchanged frames; origin changes reset in-memory tank history.
Room changes do not yet have durable undo/redo.

## Verification and follow-ups

Focused tests cover wall movement, tank protection, discrete depth projection,
door/wall/tank height safety, schema round trips, three-cell bidirectional links
on every wall, frame round trips and corruption/newer-version recovery.

Player review remains necessary for gestures, controller flow, doorway placement,
travel/restart, tank alignment after north/west resizing, and live renderer rebuild
latency. Floor-height painting and retaining faces also require live visual review.
Levels and stairs use a fixed logical picking plane and an angled construction
camera by default, with an optional north-oriented top-down planning view. Level
selection uses a six-position vertical knob rather than separate depth buttons.
Stairs are additive: the footprint must be uniform lower floor beside an
existing higher ledge, so drawing can raise the selected blocks but never cut
through higher terrain. Its run determines the supported rise, its other
dimension determines stair width, and intermediate bands are derived
automatically. Every generated stair edge remains exactly one level high.
Generated stairs appear as blocks instead of direction arrows; marking any block
removes its complete flight and restores the original lower platform. A
right-side pan knob and dedicated zoom arrows keep large rooms navigable.
Existing-door sliding and building history remain separate follow-ups.
