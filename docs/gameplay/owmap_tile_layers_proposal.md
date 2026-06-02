# OWMAP Tile Layer Proposal

This is a proposal for extending `.owmap` with paintable tile-type layers.
It is intended for the map editor/export pipeline before the C++ runtime format
is changed. The current runtime `.owmap` format is documented in
`docs/gameplay/owmap_format.md`.

## Goal

Maps should support tile-type painting independently from terrain height,
special mechanics, collision, characters, and placed 3D models.

Tile types describe what a tile is:
- grass
- path
- flowers
- snow
- rock ground
- stairs surface
- water
- debug/editor surfaces

Special tiles describe unusual behavior layered onto terrain:
- ramps
- one-way movement
- jump ledges
- forced movement
- non-climbable walls
- warp markers

Collision remains a separate fast blocked/unblocked layer.
Height remains a separate elevation layer.
Placed 3D models, NPCs, and interactables should be stored as coordinate-based
object lists, not as another dense map layer.

## Recommended Cell Type

Use `uint16` tile type IDs for map tile layers.

```cpp
using TileTypeId = std::uint16_t;

constexpr TileTypeId EmptyTileType = 0;
```

Reasons:
- `uint8` only provides 256 values, or 255 usable tile types if `0` is empty.
- `uint16` provides 65,536 values, which is large enough for one global tile dictionary.
- The memory cost is still small.

For a 100 by 100 map with 7 tile layers:

```text
100 * 100 * 7 * 2 bytes = 140 KB
```

For a 256 by 256 map with 7 tile layers:

```text
256 * 256 * 7 * 2 bytes = 917,504 bytes
```

## Global Tile Dictionary

The project should have one global tile dictionary. Map cells store only compact
IDs. The dictionary stores render, editor, gameplay, and pathfinding metadata.

Runtime C++ shape:

```cpp
enum class TileRenderKind {
    Static,
    Animated,
    Water,
    Grass,
    Snow,
    Debug
};

struct TileAnimationDefinition {
    std::vector<std::uint16_t> frames;
    std::uint32_t frame_time_ms = 0;
    bool loop = true;
};

struct TileMaterialDefinition {
    std::string texture;
    std::string shader;
};

struct TileTypeDefinition {
    TileTypeId id = EmptyTileType;
    std::string name;
    TileRenderKind render_kind = TileRenderKind::Static;
    float path_weight = 1.0f;
    std::vector<std::string> tags;
    std::optional<TileAnimationDefinition> animation;
    std::optional<TileMaterialDefinition> material;
};
```

Authoring/storage shape:

```json
{
  "0": {
    "id": 0,
    "name": "empty",
    "renderKind": "static",
    "editorColor": "#00000000",
    "pathWeight": 1.0,
    "tags": []
  },
  "4": {
    "id": 4,
    "name": "grass",
    "renderKind": "static",
    "editorColor": "#32d25f",
    "pathWeight": 1.05,
    "tags": ["grass", "ground"]
  },
  "42": {
    "id": 42,
    "name": "water_deep",
    "renderKind": "water",
    "editorColor": "#4aa7ff",
    "pathWeight": 8.0,
    "tags": ["water", "surfable"],
    "animation": {
      "frames": [0, 1, 2, 3],
      "frameTimeMs": 160,
      "loop": true
    },
    "material": {
      "texture": "assets/overworld/tiles/water_deep.png",
      "shader": "water_gen4"
    }
  }
}
```

Runtime can load the dictionary into an `unordered_map` for ownership and a dense
lookup table for hot rendering/pathfinding reads:

```cpp
std::unordered_map<TileTypeId, TileTypeDefinition> tile_types;
std::vector<const TileTypeDefinition*> tile_type_lookup;
```

## Map Tile Layers

Each map stores up to 7 tile-type layers. Each layer is a dense row-major array
of `uint16` tile type IDs.

```cpp
constexpr std::size_t MaxTileTypeLayers = 7;

struct TileTypeLayer {
    std::string name;
    std::vector<TileTypeId> tiles; // width * height, row-major
};

struct MapTileTypes {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::vector<TileTypeLayer> layers;
};
```

Indexing:

```cpp
const TileTypeId id = layer.tiles[y * width + x];
```

`0` means empty/no tile on that layer. This keeps upper layers simple and allows
sparse overlays without a special sparse encoding.

Recommended layer usage:
- layer 0: base ground
- layer 1: overlay, such as flowers or snow
- layer 2: decoration or secondary overlay
- layers 3 through 6: reserved for rare DS-style multi-layer cases

Layer index is part of the map data. It should not be encoded into the tile ID.
The same tile type ID should mean the same thing on any layer.

## Animated Tiles

Animated tiles should be represented in the tile dictionary, not in map cells.

The map cell remains:

```cpp
TileTypeId tile_type_id;
```

The dictionary defines animation:

```json
{
  "id": 42,
  "name": "water_deep",
  "renderKind": "water",
  "animation": {
    "frames": [0, 1, 2, 3],
    "frameTimeMs": 160,
    "loop": true
  }
}
```

This allows water, grass, snow, and other animated surfaces to share the same map
format. The renderer chooses the draw path from `renderKind`.

## Relationship To Existing Layers

The future map model should keep these responsibilities separate:

```text
height      uint8 grid       elevation only
special     uint8 grid       unusual tile mechanics
collision   bit grid         blocked/unblocked movement
tileLayers  uint16 grids     paintable tile identity and visual/gameplay type
objects     object list      3D models, NPCs, interactables, props
```

Pathfinding can use multiple inputs:
- `collision` for hard blocked checks
- `height` and `special` for ramps, ledges, and traversal rules
- tile dictionary `pathWeight` and `tags` for soft preferences

For example, a pathfinder can slightly prefer path tiles over grass without
making grass blocked.

## Proposed OWMAP Extension

This should be introduced as a new `.owmap` version or a clearly versioned
extension. Do not silently append this to v1 without a version check.

Suggested binary payload after existing terrain layers:

```text
tileLayerCount uint8
for each tile layer:
  width * height * uint16 little-endian tile type IDs
```

Layer names and optional layer metadata should live in the metadata JSON:

```json
{
  "tileLayers": [
    { "name": "base" },
    { "name": "overlay" },
    { "name": "decoration" }
  ]
}
```

The binary header remains authoritative for `width`, `height`, and `tileSize`.
All tile layer arrays must use the same row-major indexing:

```text
index = y * width + x
```

## Editor Guidance

The editor should:
- paint tile layers as `uint16` tile type IDs
- reserve tile type ID `0` for empty
- validate that no map contains more than 7 tile layers
- validate that every non-zero tile ID exists in the global tile dictionary
- keep animated/water behavior in the dictionary, not duplicated into cells
- keep placed models and NPCs as coordinate records, not tile-layer IDs
- export explicit `special` values for mechanics and never rely on runtime inference

## Runtime Guidance

The runtime should:
- load tile layers into dense row-major `std::vector<TileTypeId>` arrays
- resolve tile IDs through the global tile dictionary
- use a dense lookup table for hot renderer/pathfinder access
- treat unknown non-zero tile IDs as authoring/export errors
- let specialized renderers handle `renderKind`, such as water, animated surfaces, or snow

