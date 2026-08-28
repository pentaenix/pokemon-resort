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

## Alternatives Considered

- Separate C++ and TypeScript implementations constrained by golden files: lower initial porting cost, but still permits semantic drift.
- Offline-only mesh compiler: keeps the game small, but cannot support immediate in-game editing without shipping or hosting the compiler.
- Embedding the browser editor, Three.js, or a Python runtime: rejected because of runtime size, ownership, and dependency costs.
