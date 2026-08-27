# OWMAP Format (`.owmap`)

`pokemon-resort` supports a hybrid binary overworld format:
- Binary header (fast validation and dimensions)
- UTF-8 metadata JSON blob (scene metadata)
- Packed terrain payloads (height/special/collision)

Reference implementation:
- `pokemon-resort-page/tools/admin/lib/owmap-format.mjs`

## Layout (v1, little-endian)

Offsets:
- `0..3` magic: `0x4F574D31` (`OWM1`)
- `4..5` version: `uint16` (`1`)
- `6..7` width: `uint16`
- `8..9` height: `uint16`
- `10..13` tileSize: `float32`
- `14..17` metaLen: `uint32`
- `18..(18+metaLen-1)` metadata JSON
- next `W*H` bytes: height layer
- next `W*H` bytes: special layer
- next `ceil(W*H/8)` bytes: collision bits

Indexing for all layers is row-major:
- `index = y * width + x`

## Terrain semantics

- `height`: `uint8` per cell (`0..255`)
- `special`: `uint8` per cell
  - `0`: normal flat tile
  - `1`: auto ramp (editor-only; baked to 2–5 or cleared on save — must not appear in shipped `.owmap`)
  - `2..5`: directional ramps (N/E/S/W)
  - `6..9`: convex corner ramps (NE/SE/SW/NW)
  - `10..13`: concave corner ramps (NE/SE/SW/NW)
  - `14`: actor spawn marker; geometrically flat and reserved for `spawnTiles[]`

Corner ramp labels name the affected corner in map space. Corner order in the
runtime height solver is `NW, NE, SE, SW`.
- Convex corners (`cNE`, `cSE`, `cSW`, `cNW`) raise only the named corner.
- Concave corners (`vNE`, `vSE`, `vSW`, `vNW`) keep the named corner low and
  raise the other three corners.

### Cardinal ramp ownership (runtime + editor)

Directional ramp specials (`2` = north, `3` = east, `4` = south, `5` = west) are stored on the **lower-height cell** (the tile with the smaller `height` value). The high side of the slope faces the neighboring cell that is one height unit taller (e.g. `RAMP_N` on tile `(x,y)` means north neighbor `(x,y-1)` has `height[y][x]+1`).

Mesh corners, movement height (`TerrainSurface`), and the map editor preview all use the same rule: the ramp mesh is drawn on the tile that owns the special, not on the upper plateau tile.

Grid axes in the game: `tile_x` → world +X (east), `tile_y` → world +Z (south), north is `tile_y - 1`.
- `collision` on disk: 1 bit per cell (`1` blocked, `0` walkable)
  - unpacked at runtime to `terrain.collision[y][x]` as `uint8` `0/1`
  - the Map Editor can set these bits automatically when painting an RTPKS tile
    whose definition uses `collision.mode: footprint` or `collision.mode: mask`
  - automatic clearing on tile erase is opt-in (`clearOnErase`) so an artist's
    manually authored collision is not removed accidentally

## Interior maps and isolated spaces

`type` defaults to `"exterior"` for existing maps. An interior map uses
`"interior"` and may declare its rendering environment and imported room-shell
alignment without changing the version-1 binary envelope:

```json
{
  "id": "house_test",
  "type": "interior",
  "environment": {
    "space": "interior:house_test",
    "clearColor": [0, 0, 0, 255],
    "renderOtherSpaces": false
  },
  "interior": {
    "shellModelId": "",
    "floorDatum": 0,
    "gridOrigin": [0, 0],
    "openings": [
      { "edge": "south", "from": 4, "to": 6 }
    ],
    "floorCutouts": [
      { "x": 8, "y": 3, "width": 8, "height": 4 }
    ],
    "defaultRoom": {
      "enabled": true,
      "wallHeightTiles": 4.0,
      "frontWallHeightTiles": 0.35,
      "trimHeightTiles": 0.125,
      "walkableInsetTiles": 1,
      "wallFaceOffsetTiles": 0.5,
      "entryExtensionDepthTiles": 0.0,
      "blackTopCap": true,
      "topCapDepthTiles": 0.125
    }
  }
}
```

- `environment.space` groups maps that may be rendered and queried together.
  If omitted it defaults to `"world"` for exterior maps and the map ID for
  interior maps.
- Interior defaults are a black clear color and `renderOtherSpaces: false`.
  Exterior defaults retain the existing sky clear color and render linked maps.
- `shellModelId` identifies the complete imported room shell; it does not replace
  normal `models[]` placement data. Its authored floor, walls, and entrance do
  replace procedural terrain visually, preventing fallback geometry from leaking
  or flickering through the shell.
- A shell-less interior renders `defaultRoom` automatically: a muted checker
  floor, full back and side walls, a low south cutaway wall, baseboard, and trim.
  RTPKS floor and wall tiles render over this foundation, so a new interior is
  presentable before it is decorated. Resizing the OWMAP immediately resizes the
  procedural room. Set `defaultRoom.enabled` to `false` to opt out. The floor and
  wall palettes may be overridden with `floorColors.checkerA/checkerB` and
  `wallColors.northSouth/eastWest/trim/baseboard/topCap` RGBA arrays. Default
  back and side walls are four tiles tall. `wallFaceOffsetTiles: 0.5` places each
  face through the center of its boundary cell. The complete floor cells remain
  at their original scale and are cropped at the wall plane. Perpendicular wall
  pieces are likewise cropped at their intersection—neither floor nor wall UVs
  are stretched. The default one-cell boundary collision
  aligns with those faces, while explicit openings and cardinal-halo doors carve
  reachable paths through it.
  New interiors author a three-cell-wide south opening in the final in-bounds
  row, producing exactly one three-by-one entry vestibule at normal tile scale.
  Put the arrival anchor and a scripted invisible exit trigger on its center
  cell. A `MOVE_PLAYER` action before `TRANSITION_CLOSE` lets the character step
  onto that threshold before transferring, without drawing another floor row.
  `entryExtensionDepthTiles` optionally adds complete extra rows beyond that
  boundary (rounded to the nearest row); it defaults to `0`.
  `walkableInsetTiles` controls the wall-cell collision band; the default is `1`,
  and cardinal-halo doors or explicit openings carve through it.
  `blackTopCap` and `topCapDepthTiles` hide the reverse
  side of the wall with a narrow outward-facing cap.
- `floorDatum` is the source-model world-space floor elevation used during import.
- `floorCutouts` removes geometry from the procedural default-room floor without stretching adjacent cells. Legacy `{x,y,width,height}` tile rectangles remain supported. A convex `localPolygon` paired with `placementId` is transformed with that model's position/yaw/scale, then cut exactly from intersecting cells while preserving each cell's original UV domain. This is the preferred form for rounded tanks, angled stairwells, and tunnels; collision remains explicitly authored in the OWMAP.
- `gridOrigin` is the source-model X/Z position corresponding to OWMAP tile `[0,0]`.
- Boundary openings use inclusive tile spans and remove the matching procedural
  wall segments as well as describing imported-shell entrances.

These fields are metadata, so older OWMAP v1 readers ignore them. Runtime travel
activates the destination space before applying its anchor coordinate; a halo
door trigger may still sit outside the stored grid.

### Actor spawn markers

Future actor spawning locations are authored with terrain `special=14` and a
matching metadata entry. The runtime parses these entries into `SceneConfig`,
but the current random spawning system does not consume them yet.

```json
{
  "spawnTiles": [
    { "id": "actor_spawn_4_7", "tile": [4, 7], "allows": "pokemon_random_from_boxes" },
    { "id": "actor_spawn_6_7", "tile": [6, 7], "allows": "npc_with_partner" },
    { "id": "actor_spawn_8_7", "tile": [8, 7], "allows": "npc_without_pokemon" }
  ]
}
```

`allows` accepts exactly `pokemon_random_from_boxes`, `npc_with_partner`, or
`npc_without_pokemon`. Editors keep the sparse metadata and the special plane in
sync. Clearing a cell removes both records.

## RTPKS decoration layers

`metaJson.tilePackage` binds the RTPKS package. `metaJson.tileLayers.layers[]`
stores visible decoration layers whose `cells[y][x]` values are stable
`resortTileId` numbers or `null`. Tile definitions in the package may include:

- palette tab membership and smart-path grids for editor organization
- namespaced gameplay tags and typed properties
- footprint or mask-based automatic collision authoring rules
- frame/material animation metadata, runtime texture frames, and baked named
  vertex clips for imported skeletal tiles

The map does not duplicate these definitions. Renaming tabs, changing tags, or
adding animation therefore updates the package without rewriting every `.owmap`
that uses the same stable IDs.

RAE interior tile kits use ordinary RTPKS entries tagged `interior` and
`interior.<role>` with an `interior.role` property. A tiled interior has no
`shellModelId`; floor and wall bundles are placed in normal tile layers and are
loaded by the existing runtime RTPKS path. Legacy shell interiors remain valid.

### Terrain transition families

Map Studio supports RTPKS-authored eight-neighbor terrain families. A palette
brush tile names `transition.brushFamily`; every member names the same
`transition.family` and a normalized `transition.mask`. The low four mask bits
are north, east, south, and west. The high four are northwest, northeast,
southeast, and southwest; a diagonal bit is ignored unless both adjacent
cardinal bits are set. This produces the standard 47 blob shapes.

Painting or erasing a family member resolves that cell and its eight neighbors,
then stores the selected member's ordinary stable `resortTileId`. Runtime code
does not infer the topology. The shipped `sand-grass` family uses Black 2 sand
tile 103 and grass body 3322. Its 46 edge/corner meshes reuse the existing sand
and grass materials, while body 3322 is the non-redundant fully surrounded case.
The boundary is hard pixel-stepped geometry rather than a color or alpha
gradient.

```json
{
  "transition.family": "sand-grass",
  "transition.mask": 70,
  "transition.baseTileId": 103,
  "transition.overlayTileId": 3322
}
```

A door is deliberately split across two authoring records:

- The visible door is an RTPKS tile tagged `interaction.door`, with
  `interaction.kind: door`, `door.front`, and trigger-phase animation metadata.
- The behavior is an OWMAP `doorTriggers[]` entry. It activates when movement
  approaches its coordinate from an allowed direction and references a map link,
  a door script, and optionally the RTPKS tile that should animate.

This split also represents invisible doors: omit `visual` and retain the movement
trigger. Trigger coordinates may be one cell outside the stored grid, such as
`[x, -1]`, so walking north through a doorway can activate an exterior halo tile
before the normal bounds check rejects the step.

The Admin GLB compiler preserves door node/skinning animation by sampling named
clips into each tile mesh. `door.animation.open` selects the forward clip;
`door.animation.close: named` plus `door.animation.closeClip` selects a separate
close clip. Texture-motion doors store the opening timeline once and play it in
reverse when closing. Playback state belongs to the addressed map/layer/cell,
not to the shared tile definition.

```json
{
  "anchors": [
    { "id": "inside_entry", "tile": [4, 7], "facing": "north" }
  ],
  "links": [
    {
      "id": "house_entry",
      "destinationMapId": "house_interior",
      "destinationAnchorId": "inside_entry"
    }
  ],
  "doorTriggers": [
    {
      "id": "house_front_door",
      "kind": "door",
      "tile": [12, 8],
      "activation": "move_toward",
      "allowedDirections": ["north"],
      "visual": { "mapId": "", "layerId": "buildings", "tile": [12, 8] },
      "linkId": "house_entry",
      "scriptId": "door_enter_default"
    }
  ]
}
```

`anchors[].tile` is the exact teleport coordinate. `anchors[].facing` becomes the
player facing after teleport. `visual.mapId` is optional and defaults to the map
that owns the trigger; setting it allows an interior return trigger to close the
exterior door after teleporting back.

Native-editor interior creation adds `entry_left`, `entry`, and `entry_right` on
the room's south row, facing north into the room. Door authoring proposes the
middle `entry` node, while all three remain ordinary anchors that can be moved,
rotated, removed, restored, or selected explicitly. `destinationAnchorId` is
optional when the destination is unambiguous: one anchor resolves to that anchor,
and a map with exactly one door resolves to that door tile facing inward. Authors
only need to choose an anchor when a destination has multiple possible entrances.
Broken or ambiguous links remain inert and cannot close the screen.

Only the anchor cell stores a multi-cell tile ID. Editor hit testing resolves
every covered cell back to that anchor: erasing or eyedropping any part affects
the whole tile. Painting a new footprint first removes every same-layer tile
whose footprint intersects it, including tiles anchored outside the newly
clicked cell, so 1x3 and 3x3 pieces cannot silently overlap.

## Loader architecture in this repo

- Shared metadata parser:
  - `src/gameplay/world3d/data/SceneMetadataParser.cpp`
- JSON loader:
  - `src/gameplay/world3d/data/JsonOverworldLoader.cpp`
- OWMAP loader:
  - `src/gameplay/world3d/data/OwmapOverworldLoader.cpp`

Both loaders produce the same `SceneConfig` shape:
- `include/gameplay/world3d/Overworld3DConfig.hpp`

Door link resolution and ordered door-script state live in
`gameplay/world3d/doors/DoorTravel`, while the screen adapter owns transitions,
teleport application, forced exit movement, and RTPKS animation commands.

## Runtime default map

Current 3D test default:
- `assets/overworld/maps/0.owmap`

The current test loop enters `interior_842.owmap` through the animated RTPKS
door at exterior tile `[16, 15]`. The interior exit lives at `[6, 10]`, one
cell beyond its 14×10 south edge, and uses `door_exit_default` to return, step
the player south, and close the exterior door.

RAE interior sidecars may provide an inferred `heightStep` and `heightMask`.
Map Studio writes the step as `terrainVisual.floorHeightScale`; the native
metadata parser applies it per map as `TerrainConfig::height_per_floor`.
Because OWMAP stores one height per X/Z cell, vertically overlapping source
floors must be split into separate interior maps. RAE reports those cells rather
than discarding that limitation silently.

## Validation implemented

- magic check
- version check (`== 1`)
- dimension sanity (`>0`, `<=256`)
- payload size/truncation checks
- metadata JSON parse check
