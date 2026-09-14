# Independent aquarium rooms

## Controls

Enter Room mode using the room-outline circle, R, or controller right-stick click.
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
AquariumBuildingLayoutJson owns canonical schema-1 JSON. Runtime projection
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
- The south external connection remains intact; the old gallery stays retired.

## Persistence and rendering

The profile's aquarium_builder_lab.room.json stores the whole building using the
existing building schema, synced/read-back atomic promotion and backup recovery.
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

Focused tests cover wall movement, tank protection, three-cell bidirectional
links on every wall, frame round trips and corruption/newer-version recovery.

Player review remains necessary for gestures, controller flow, doorway placement,
travel/restart, tank alignment after north/west resizing, and live renderer rebuild
latency. Existing-door sliding and building history are separate follow-ups.
