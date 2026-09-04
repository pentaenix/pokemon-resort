# ADR 0002: Shared Aquarium Geometry Kernel

- Date: 2026-08-28
- Status: Accepted

## Context

Pokémon Resort needs editable player aquariums whose render meshes, collision cells, and Pokémon navigation volumes agree. The browser-based aquarium maker already contains proven footprint and passage rules, but its Three.js UI, editor persistence, and export pipeline are unsuitable runtime dependencies. Reimplementing the rules independently in C++ and TypeScript would allow the two products to drift.

The game build must remain independent of browser, npm, Emscripten, Three.js, and map-maker code. Aquarium designs must remain authoritative while derived geometry can be rebuilt.

## Decision

`shared/aquarium_geometry` is the authoritative deterministic geometry kernel. It accepts versioned, discrete design DTOs and returns plain semantic meshes, conservative collision cells, navigation volumes, stable diagnostics, statistics, and a content hash.

The kernel has no SDL, bgfx, UI, filesystem, gameplay, map-maker, or Three.js dependency. Pokémon Resort links it natively. A tool-only adapter under `tools/aquarium_geometry_wasm` compiles the same sources with Emscripten and maps canonical aquarium-design JSON to a versioned WASM result. The aquarium maker consumes that adapter for Resort-compatible features.

The normal game build never invokes Emscripten or npm and never links the WASM adapter. Native and WASM output are compared using shared golden design documents. Unsupported geometry returns stable validation diagnostics rather than a best-effort approximation.

Authored aquarium documents contain only stable IDs and discrete design intent. Render meshes, collision, navigation, caches, and GPU resources are derived.

## Consequences

- Geometry rules have one implementation and one version boundary.
- Cross-runtime determinism becomes a required CI check.
- The maker needs Emscripten only when rebuilding or verifying its kernel artifact.
- The adapter may depend on Resort's canonical design JSON layer, but the core kernel may not.
- Advanced maker-only terrain and decor remain on the legacy generator until explicitly migrated.
- Kernel ABI or schema changes require new golden fixtures and compatible version handling.

## Milestone 3 implementation record

Kernel ABI 2 implements the Resort-compatible rectangle, L, and U footprint
vocabulary, quarter-turn rotation, fitted quarter-cell corner radii, shaped
semantic meshes, conservative collision, and matching navigation. Deterministic
arc constants and canonical output quantization keep native and WASM arrays and
content hashes identical rather than merely visually equivalent.

The Aquarium Maker loads a browser-only build of these exact C++ sources. A
strict compatibility mapper sends only whole-cell rectangle/L/U tanks with flat
sand, uniform corner radii, fixed 55 mm glass, and no advanced passages or decor
through the shared kernel. Unsupported developer features remain on the maker's
legacy generator. The Resort executable still has no Emscripten, npm, Three.js,
or maker dependency.

The Milestone 3 usability revision advances the boundary to kernel ABI 3 and
design schema 2. Schema 2 makes the player installation transform explicit as
`placementOffsetCells: [0.5, 0.5]` while retaining schema 1 reads. The kernel
also accepts canonical local `subtractedCells` for rectangle-first editing and
requires cuts to remain exterior-connected with one connected occupied water
area. This keeps the in-game subtract tool, collision, navigation, native
geometry, and maker WASM adapter on the same deterministic footprint rules.

The direct-manipulation revision advances the boundary to kernel ABI 4 and
design schema 3. Schema 3 adds stable per-convex-corner radius overrides while
retaining the scalar radius as the migration/default value. ABI 4 separates
the water volume from its surface, uses the maker's standard vertical profile
for flat sand, rims, glass, and water level, and shares normals across rounded
arc segments. The in-game handles, saved design, native runtime, and maker WASM
preview therefore consume the same asymmetric-corner and material semantics.

The Milestone 3 player-review correction advances the kernel to ABI 5 without
changing design schema 3. ABI 5 adds the standard solid lower plinth beneath
the lower rim and treats every occupied footprint cell as blocked overworld
collision. This replaces the earlier perimeter-only collision result, which
was insufficient for rounded tanks and multi-cell actor movement. The maker's
WASM preview and Resort runtime continue to consume identical derived output.

The below-floor extension advances the kernel to ABI 6 and design schema 4.
Schema 4 adds discrete half-cell `depthSteps`; older documents migrate by
defaulting depth to zero. ABI 6 uses the maker's Y=0 room-floor convention,
opaque sub-floor body, floor-starting glass, lowered flat sand, and matching
navigation band. The kernel derives whole-litre usable water volume from that
same navigation footprint and height band. Volume remains rebuildable result
data rather than duplicated authored state, so future stock policies cannot
drift from collision/navigation geometry. Resort dynamically cuts its room
floor to the exact generated footprint only for positive-depth tanks; the
maker consumes the same profile through its tool-only WASM adapter.

The first Milestone 4 tunnel slice advances the kernel to ABI 7 without a
schema change because stable tunnel IDs, route kind, and ordered whole-cell
centrelines were already authored fields. ABI 7 derives fixed arch shells,
portal openings, dry corridor collision, vertically layered water navigation,
explicit dry-volume metadata, and adjusted capacity from that centreline.
Native and browser-WASM builds share the new cell-region, validation, and
profile-sweep modules. Rounded and shaped portal seams remain explicit
validation failures until their shared geometry is implemented; the kernel
never substitutes an approximate tunnel silently.

Player review exposed that tunnel centrelines must use the ordinary walking
grid, not the tank footprint's half-cell-shifted drawing grid. Schema 5 makes
that distinction authoritative. Schema-4 routes migrate by extending
positive-edge portals onto the walking lattice. ABI 10 derives a two-cell-wide
visual, sand, dry-volume, lower-water-navigation, capacity, and portal-infill
profile on a half-cell raster, while collision clears only the authored
centreline walking cells. The half-cell shoulders therefore retain collision
even though the arch reads wider visually. Standard passages cut through the
tank base so the authored room floor remains visible. Below-floor passages
retain substrate and water below a transparent, cell-panelled bridge with
thin structure rails. Rounded corners are compatible when their fitted arcs
stay clear of the complete portal opening. Tunnels require tanks at least
three vertical levels tall.

Player review found that transparent bridge panels alone did not communicate a
walkable tunnel floor at game scale. ABI 11 adds a dedicated opaque
`tunnel-frame` semantic material and maker-matched raised side rims plus shallow
cross-separators at each route cell. This keeps the glass centres transparent,
gives both renderers an explicit cool-grey scaffold, and leaves collision and
navigation unchanged.

ABI 12 extends that same semantic frame around both portal arches. The border
is derived from the fixed tunnel profile, rendered on both faces, and does not
alter the authored route, portal aperture, collision, or navigation volume.

ABI 13 sanitizes the clipped water-region polygons shared by sand triangulation
and navigation. Duplicate and redundant collinear points are removed after a
tunnel region is clipped to a rounded footprint, preventing a valid tunnel near
a curved corner from dropping part of the flat sand surface.

## Alternatives Considered

- Separate C++ and TypeScript implementations constrained by golden files: lower initial porting cost, but still permits semantic drift.
- Offline-only mesh compiler: keeps the game small, but cannot support immediate in-game editing without shipping or hosting the compiler.
- Embedding the browser editor, Three.js, or a Python runtime: rejected because of runtime size, ownership, and dependency costs.
