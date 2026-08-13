# ADR 0001: Native Map Maker With Exact Runtime Preview

- Date: 2026-08-12
- Status: Accepted

## Context

The browser-based admin map editor accumulated several independent tool panels,
large DOM/JavaScript update paths, duplicated rendering assumptions, and costly
pointer-driven work. Basic map selection, painting, and door movement required
too many mode changes, while the viewport could not guarantee that authored
content matched the C++ game. Keeping a second approximation of the world
renderer would make interiors, animated doors, layered RTPKS terrain, models,
and future smart objects drift further from runtime behavior.

The editor also needs lossless writes. Existing OWMAP metadata contains fields
outside any single editing surface, map-project entries may intentionally reuse
one source file, and stable RTPKS tile IDs are runtime content identifiers.

## Decision

Build `pokemon_resort_map_maker` as a standalone native C++ application under
`tools/map_maker/`, using SDL2 for the window/input boundary, bgfx for rendering,
and Dear ImGui for a single immediate-mode editor shell.

The tool is a one-way architecture consumer:

- production runtime code does not import `mapmaker/*`;
- pure editor document/project/command/selection/validation modules do not
  depend on ImGui or bgfx;
- the editor may consume the established `gameplay/world3d` loading, camera,
  terrain, and rendering seams;
- the old web-admin project location is a migration-time file-discovery
  fallback, not a code dependency.

Add an embedded seam to `OverworldBgfxRenderer` rather than implementing a
second editor renderer. `renderEmbeddedViewport` submits the same world passes
to the renderer-owned pixel target and returns a non-owning texture description.
It does not composite to the backbuffer or call `bgfx::frame()`. The editor
submits ImGui afterward and owns exactly one process-wide frame advance.

Editor preview animation uses an explicit deterministic clock and is disabled
by default. The scene is rebuilt only after committed document changes, not for
pointer hover, panning, zooming, selection, or UI animation. RTPKS previews are
loaded lazily as asset cards become visible.

OWMAP editing uses a lossless document envelope and command-based mutations.
No-op documents round-trip byte-identically; edited documents preserve unknown
metadata, packed collision spare bits, and trailing bytes. Saves use verified
temporary files, backups, atomic replacement, post-write validation, and
periodic separate recovery snapshots. Reused project entries share one loaded
document and command history keyed by normalized source path.

## Consequences

- The viewport is authoritative for game world appearance because it uses the
  production renderer, shader set, tile loader, model loader, and camera rules.
- Direct manipulation can remain responsive: picking is pure, animations are
  normally frozen, thumbnails are lazy, and scene rebuild count is observable.
- Editor-specific UX and future smart-object compilation can evolve without
  adding editor behavior to the shipping game.
- bgfx lifecycle rules are strict: renderer-owned viewport textures become
  invalid after target resize, renderer replacement, or shutdown; the editor
  must not destroy them or advance an extra frame.
- Project JSON authoring can be added behind its own lossless command seam. The
  first native slice treats it as the map/source catalog and writes OWMAP data.
- The current SDL/Metal integration remains macOS-oriented, even though the
  document, command, validation, and catalog cores are platform-neutral.

## Alternatives Considered

### Continue expanding the web admin editor

Rejected as the primary path. It preserves accumulated UI and pointer latency,
keeps three competing tool surfaces, and still requires a second representation
of the game viewport. It remains available during migration.

### Build the editor in Python beside RAE

Rejected. Python is productive for extraction tooling, but a Python UI would
still need a process/FFI boundary or a duplicate renderer to show the exact
SDL2/bgfx game scene. RAE remains responsible for asset extraction and export;
map authoring belongs with the runtime data contract it consumes.

### Use Qt, wxWidgets, or a custom retained UI

Rejected for the initial tool. These can provide richer desktop widgets, but
would add another rendering/input lifecycle and more adapter code before solving
the core correctness problem. Dear ImGui already integrates with the pinned bgfx
dependency and suits a GPU-first editor shell.

### Embed the complete gameplay screen

Rejected. The runtime screen owns simulation, actors, overlays, transitions,
and frame presentation that an editor must control independently. The narrower
renderer seam shares visual truth without importing gameplay-loop behavior.
