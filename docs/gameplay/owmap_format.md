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

