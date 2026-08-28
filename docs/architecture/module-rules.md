# Module Rules

## Hard Rules
1. `engine` cannot import `gameplay`, `transfer`, or `resort` modules.
2. `gameplay` cannot import `resort/*` or `transfer` internals.
3. `gameplay` may only consume `transfer/contracts/*` from transfer.
4. `transfer/contracts/*` cannot import `gameplay/*` or `ui/*`.
5. Production modules cannot import `mapmaker/*`; the dependency is tool-to-runtime only.
6. `tools/map_maker` document, project, command, selection, and validation modules cannot depend on ImGui or bgfx.
7. Map-maker UI emits intentions through commands; it cannot mutate authored JSON or OWMAP binary planes directly.
8. The embedded world renderer cannot own the editor frame boundary: the host performs exactly one `bgfx::frame()` after world and ImGui submission.
9. `shared/aquarium_geometry` cannot import runtime, rendering, UI, persistence, map-maker, or aquarium-maker modules.
10. Production targets cannot import or link `tools/aquarium_geometry_wasm`; it is a tool-only JSON/WASM adapter.

## Enforcement
- Script: `scripts/check_module_boundaries.sh` enforces the runtime domain import rules (1-4).
- CTest target: `architecture_module_rules_check`
- Map-maker rules (5-8) are review contracts until the boundary checker gains tool-aware include and frame-lifecycle checks; focused map-maker tests protect their data/command behavior.
- Shared-kernel rules (9-10) are enforced by the boundary checker and build-target separation.

## Change Management
Cross-domain boundary changes require an ADR in `docs/architecture/adrs/` and updates to `docs/architecture/system-map.md`.

The native editor boundary is recorded in `docs/architecture/adrs/0001-native-map-maker.md`.
The shared aquarium kernel boundary is recorded in `docs/architecture/adrs/0002-shared-aquarium-geometry-kernel.md`.
