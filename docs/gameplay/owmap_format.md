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

## Loader architecture in this repo

- Shared metadata parser:
  - `src/gameplay/world3d/data/SceneMetadataParser.cpp`
- JSON loader:
  - `src/gameplay/world3d/data/JsonOverworldLoader.cpp`
- OWMAP loader:
  - `src/gameplay/world3d/data/OwmapOverworldLoader.cpp`

Both loaders produce the same `SceneConfig` shape:
- `include/gameplay/world3d/Overworld3DConfig.hpp`

## Runtime default map

Current 3D test default:
- `assets/overworld/maps/testing.owmap`

## Validation implemented

- magic check
- version check (`== 1`)
- dimension sanity (`>0`, `<=256`)
- payload size/truncation checks
- metadata JSON parse check
