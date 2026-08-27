# Pokemon Resort Map Maker

`pokemon_resort_map_maker` is the standalone native editor for Pokemon Resort
overworld and interior maps. It edits the same `.owmap` files consumed by the
game and previews them through the production bgfx overworld renderer. The old
web admin map tool remains available during migration, but it is not a runtime
dependency of this editor.

The current source of truth is:

- map membership, reusable map instances, and tile-package selection:
  `map_project.json`
- terrain, tile layers, models, doors, links, and anchors: `.owmap`
- stable tile IDs, previews, animation metadata, and smart-set seeds: `.rtpks`
  plus its `.rtpks.meta` sidecar
- reusable model metadata: `assets/overworld/models/*/model.json`

The editor edits both the project graph and its OWMAP sources. Unknown project
and OWMAP fields are retained. An untouched map saves to the same bytes it was
loaded from, including collision spare bits and trailing data.

## Build

The tool uses the same macOS dependencies as the game:

```bash
brew install sdl2 sdl2_image sdl2_ttf zstd pkg-config
cd /Users/vanta/Desktop/title_screen_demo/pokemon-resort
cmake -S . -B build
cmake --build build --target pokemon_resort_map_maker
```

Launch the default project:

```bash
./pkr mapbuilder
```

The map-maker subdirectory is `EXCLUDE_FROM_ALL`: `./pkr build` and the shipping
game executable do not compile or link editor-only code. `./pkr mapbuilder`
builds only the explicit standalone target and forwards additional arguments.

The project is discovered in this order:

1. `config/gameplay/world3d/map_project.json`
2. `assets/overworld/maps/map_project.json`
3. the legacy sibling web-admin project at
   `../pokemon-resort-page/tools/admin/data/map-projects/default.json`

Use an explicit project or initial map when needed:

```bash
./build/pokemon_resort_map_maker \
  --project /absolute/path/to/map_project.json \
  --map interior_842
```

Other supported command-line modes are:

```bash
# Parse every unique map source and report project/door/link/anchor diagnostics.
./build/pokemon_resort_map_maker --validate-project

# Validate a specific project without opening a window.
./build/pokemon_resort_map_maker \
  --project /absolute/path/to/map_project.json \
  --validate-project

# Exercise a bounded real native render loop for build/CI smoke checks.
./build/pokemon_resort_map_maker --smoke-test

# Override bgfx backend selection when diagnosing a renderer problem.
./build/pokemon_resort_map_maker --renderer metal
```

`--validate-project` is the preferred pre-commit content check. `--smoke-test`
is a renderer/lifecycle check; it does not replace document or project tests.

## Working In The Editor

The window deliberately has one editing shell:

- the top row owns project actions and the **World / Map / Play** workspaces;
- one narrow tool rail selects the current operation;
- one contextual panel shows only that tool's assets and settings;
- one selection inspector shows exact values for the selected cell or object;
- the center is the spatial world graph, top-down map canvas, or exact game view;
- the bottom status row exposes frame, input-latency, and rebuild metrics.

### World workspace

The World workspace is the project navigator for large games. Exterior maps are
touching world cells at their authored `gridX/gridY` positions, so north, south,
east, and west neighbors read as one continuous overworld instead of detached
graph cards. Interiors use smaller floating cards beside the exterior that
links to them. Door travel has its own visual language: a portal notch on a
shared exterior seam, or a curve when travel is non-spatial. Reciprocal travel
is cyan, one-way travel is purple, and missing destinations or anchors are red.

- type in **Find a map** to filter a project with hundreds of maps;
- click a card or list item to make that map active;
- double-click a card or list item to open it in the Map workspace;
- drag a card to a new snapped world position; the move is undoable;
- middle-drag, or use the Hand tool, to pan; the wheel only zooms the graph;
- hover a card and use a `+` handle to create an adjacent exterior or interior;
- optionally make the new card reuse any existing map source, so repeated water,
  room, or template maps share one OWMAP document and edit history;
- **+ New map** in the World view opens a creation modal for standalone interior
  or exterior maps, including useful size presets and exact width/height fields.
  Standalone maps are placed clear of the existing world graph and begin without
  adjacency or travel links; creating one opens it immediately in the Map view.
- new maps are real OWMAP files with a grid, ground layer, tile package, player
  spawn, environment defaults, and project entry—not placeholder UI nodes.
- a newly created interior immediately displays a darker checker floor and a
  four-tile-high trimmed cutaway wall envelope with a black top cap in both
  Top-down and Play. Wall faces sit in the center of the one-cell boundary band,
  with floor and perpendicular wall pieces cropped after intersection rather
  than resized; their UV scale remains unchanged.
  Those cells appear as **Room wall** in the Collision lens. The default
  three-cell south opening carves three reachable boundary cells and forms a
  three-by-one entry vestibule at normal tile scale. With nothing selected,
  use the inspector's **Map size** fields to resize it; the edit is undoable and
  keeps the overlapping terrain, tile layers, paths, and valid interior openings;
- every new interior proposes three independently movable south-edge arrival
  nodes: `entry_left`, `entry`, and `entry_right`, all facing into the room. Door
  authoring proposes the middle `entry` node, but its destination dropdown can be
  changed at any time. The Arrival anchors tool can add missing nodes in one click,
  move them, change their facing, or remove them with the normal Delete action;

The tool rail and map markers use the bundled Kenney/Font Awesome symbol fonts.
Full tool names and shortcuts remain available in hover tooltips; single-letter
placeholder controls are not part of the authoring interface.

Maps marked `[linked]` are separate project instances backed by one normalized
OWMAP source. Editing one updates every instance that references that source;
the file is loaded and saved only once.

The normal interaction model is direct:

- click a terrain cell, model, or door to inspect it;
- drag models, doors, and anchors directly on the top-down grid. Placed GLB models
  are shown as cached top-down silhouettes calculated from their real geometry,
  transformed by the authored yaw and scale. Selected models (and all models at
  high zoom) show their projected width and depth in tiles; unreadable models
  fall back to their catalog footprint;
- choose a tile asset, then click-drag to paint on the active layer;
- use **Erase** to clear only the active layer or **Clear** to remove everything
  bound to a cell across terrain, layers, doors, and models;
- use **Height** and **Block** brushes for elevation and collision; selecting a
  cell also exposes exact height, ramp/corner special, and collision values;
- choose **Actor spawn** as the terrain shape to author a future spawn marker,
  then select random boxed Pokémon, NPC with partner Pokémon, or NPC without
  Pokémon as its allowed occupant. This metadata is saved but is not used by
  the current random spawner;
- RAE interior tiles appear in `interior_<role>` asset categories when their
  RTPKS definitions include `interior.role` or `interior.<role>` tags. Paint
  them on ordinary tile layers; leave `interior.shellModelId` empty for tiled
  rooms. They render over the procedural default room, allowing the fallback
  floor and walls to be replaced gradually;
- each authoring tool changes the canvas into the view needed for that job:
  Height is a fixed blue-to-red heat map with values and ramp directions,
  Collision shows authored walkable/blocked cells in green/red and automatic
  room-wall collision in purple, and terrain tools
  distinguish active-layer cells from the composed result underneath;
- Erase previews only the active-layer content it will remove, while Clear
  previews the destructive all-data operation with a red crossed cell;
- create, rename, show/hide, reorder, and delete layers in the contextual panel;
- middle-drag to pan and use the wheel to zoom only the hovered viewport;
- use the visible one-cell halo around the map for cardinal outside door triggers;
- switch to **Play** for the production renderer. WASD or arrow keys drive the
  shipping grid controller, collision, ramps, terrain binding, water state, and
  walk/swim animation. Reset, start-at-selection, focus, camera pan, zoom,
  environmental animation pause/restart, and time scrub are available. Entering
  Play captures movement immediately; Escape or an outside click releases it,
  and one viewport click recaptures it. Arrow keys never navigate editor controls
  while playing.

The native window is framebuffer-scale aware on Retina displays. UI geometry
remains in logical points while ImGui text and icons rasterize at the actual
backbuffer density, avoiding the scaled low-resolution font atlas that made the
original editor appear blurry.

One pointer gesture produces one undo entry. Top-down map switches and edits do
not build a game scene. RTPKS thumbnails remain compressed until visible, tile
composition is cached until the document changes, and only visible grid cells
are drawn. Map switching never constructs a game scene. Entering Play refreshes
the production scene only when its source changed; returning to an unchanged
Play view reuses the live scene.

## Shortcuts

The primary modifier is Command on macOS and Ctrl on other platforms.

| Action | Shortcut |
| --- | --- |
| Save all changed map sources | Primary+S |
| Undo | Primary+Z |
| Redo | Primary+Shift+Z or Primary+Y |
| Duplicate selection | Primary+D |
| Delete selection | Delete |
| Focus selection | F |
| Performance diagnostics | Primary+Shift+D |
| Toggle Play / Map | F6 |
| Select | Q |
| Terrain tile tool | B |
| Erase active layer | E |
| Clear whole cell | C |
| Paint height | H |
| Paint collision | X |

The **Delete** action is selection-driven rather than layer-driven: deleting a
door or model does not require changing tools first. Project validation remains
the final authority for whether door links, destination anchors, allowed
directions, and one-cell cardinal door halos are coherent.

## Save And Recovery Guarantees

Normal saves are defensive:

1. encode and validate the OWMAP in memory;
2. write a sibling `.tmp` file;
3. read it back and decode it again;
4. preserve the previous file as `<map>.owmap.bak`;
5. atomically replace the target and verify the final bytes;
6. restore the backup if post-replace validation fails.

World-graph saves use the same temporary-file, parse-back, backup, and atomic
replace pattern for `map_project.json`. Map moves and additions preserve unknown
future project fields and participate in World-workspace undo/redo.

An OWMAP with unresolved editor-only `special=1` cells is rejected rather than
silently writing incomplete runtime data. A corrupt primary map automatically
falls back to its verified `.bak` when the workspace opens.

While an edited source is dirty, the app also writes periodic emergency copies
under:

```text
build/map-maker-state/recovery/<map-id>.recovery.owmap
```

These recovery files never silently replace authored maps. If a crash occurs,
preserve the damaged source first, inspect the recovery copy, and promote it
manually only after validation. A successful project save removes the recovery
copies for every source it saved.

## Logs And Diagnostics

Structured JSON-lines logs live at:

```text
build/map-maker-state/logs/map_maker.log
```

The logger rotates bounded history as `map_maker.1.log`, `map_maker.2.log`, and
so on. Use **Performance diagnostics** to inspect FPS, p95 frame time, smoothed
input latency, and exact-scene rebuild count. Temporary serialized documents
used to refresh the exact preview live under `build/map-maker-state/preview/`;
they are not authored map sources.

When reporting a failure, include the project path, active map ID, the relevant
validation code, and the matching log records. Do not attach ROMs or raw game
assets.

## Validation And Tests

Focused editor tests are registered with CTest from
`tests/native/mapmaker/`. They protect:

- lossless OWMAP parsing, serialization, atomic save, and backup recovery;
- project discovery, normalized shared sources, and map switching;
- undo/redo transactions and whole-gesture document commands;
- path-local tile/model/door edits and universal deletion;
- selection plus door/link/anchor/project validation;
- exact terrain picking, lazy RTPKS/model catalogs, and preview extraction;
- persistent Play input capture and deterministic height/collision lens colors;
- autosave recovery, structured log rotation, and frame metrics.

Run the tool tests through the normal suite:

```bash
cmake --build build --target pokemon_resort_map_maker_tests
ctest --test-dir build -L map_maker --output-on-failure
```

For a player-visible renderer or data-boundary change, also run the complete
native suite:

```bash
ctest --test-dir build --output-on-failure
```

## Extension Rules

- Keep `tools/map_maker/document`, `project`, `commands`, `selection`, and
  `validation` independent of ImGui and bgfx.
- UI code emits intentions; commands own mutations and undo state.
- Keep stable `resortTileId` values stable. Reordering tabs must not renumber
  existing tiles.
- New smart objects should compile down to ordinary OWMAP metadata/tile edits;
  the game runtime must not depend on editor-only behavior.
- Reuse `ExactWorldPreview` and `renderEmbeddedViewport`; do not fork a visually
  similar editor renderer.
- The editor owns one `bgfx::frame()` per UI frame. The embedded world renderer
  only submits its world passes and returns a renderer-owned texture.
- Production game targets must never include `mapmaker/*` headers.

The architectural rationale is recorded in
[`docs/architecture/adrs/0001-native-map-maker.md`](../../docs/architecture/adrs/0001-native-map-maker.md).
