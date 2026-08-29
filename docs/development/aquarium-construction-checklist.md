# Aquarium Construction Implementation Checklist

Updated: 2026-08-28

## Working-state guardrails

- Pokémon Resort branch: `codex/aquarium-construction-milestone-1` from clean `main` at `33a19c32`.
- Aquarium maker branch: `codex/aquarium-construction-milestone-1` from clean `main` at `85b75ed`.
- Pre-Milestone 1 aquarium work was integrated into both repositories before these branches were created.
- Milestone 1 changes are confined to Pokémon Resort; the maker branch remains clean because the shared kernel contract did not change.
- No reset, clean, checkout, destructive operation, or broad reformat was performed.
- Feature commits stage explicit Milestone 1 paths only.

## Milestone status

| Milestone | State | Review |
|---|---|---|
| 0 — branches, baseline, contracts, kernel harness | Complete | Approved 2026-08-28 |
| 1 — fixed rectangle vertical slice | Ready for review; interactive verification pending | Automated checkpoint 2026-08-28 |
| 2 — editing and history | Not started | Blocked on Milestone 1 approval |
| 3 — height, L/U shapes, roundness | Not started | Blocked on Milestone 2 approval |
| 4 — tunnels | Not started | Blocked on Milestone 3 approval |
| 5 — recovery, performance, release hardening | Not started | Blocked on Milestone 4 approval |

## Milestone 0 checklist

- [x] Create protected feature branches without disturbing dirty changes.
- [x] Record the starting dirty baseline.
- [x] Accept ADR 0002 for the native C++ plus WASM kernel boundary.
- [x] Add kernel ABI 1 and aquarium design schema 1 constants.
- [x] Add discrete tank, mesh, collision, navigation, diagnostic, and statistics DTOs.
- [x] Lock cell-centre and parity-aware footprint transforms with tests.
- [x] Add deterministic validation for the supported rectangle slice.
- [x] Generate semantic structure, flat sand, water, and fixed-glass meshes.
- [x] Generate conservative perimeter collision and one water navigation layer.
- [x] Add canonical aquarium-design JSON parsing and serialization.
- [x] Preserve newer documents as incompatible/read-only.
- [x] Add the native JSON adapter and golden dump.
- [x] Add the maker Emscripten harness against the exact same C++ sources.
- [x] Verify native/WASM equality for the first golden design.
- [x] Run the complete Resort test suite.
- [x] Run maker type/build and existing model validation.
- [x] Capture measured native and WASM generation timings after warm-up.
- [x] Confirm render isolation and existing aquarium/headless smoke coverage; player-visible image review begins in Milestone 1.
- [x] Present Milestone 0 review checkpoint.

## Current golden evidence

- Fixture: `shared/aquarium_geometry/goldens/rectangle-even.aquarium.json`.
- Kernel ABI: 1.
- Design schema: 1.
- Hash: `fnv1a64:5f18ef72142032cb`.
- Meshes: 4 semantic material groups.
- Vertices: 296.
- Indices: 444.
- Triangles: 148.
- Collision: 16 perimeter cells.
- Navigation: 1 layer and 1 suggested spawn.
- Native/WASM result: exact JSON equality.

## Verification log

| Command | Result |
|---|---|
| `cmake --build build --target aquarium_geometry_tests aquarium_design_tests -j4` | Pass |
| `ctest --test-dir build -R 'aquarium_(geometry\|design)_tests' --output-on-failure` | 2/2 pass |
| `npm run validate:kernel` | Pass; native/WASM parity confirmed |
| `cmake --build build -j4` | Pass; shipping executable and all test targets build |
| `ctest --test-dir build --output-on-failure` | 66/71 pass; five documented baseline failures outside the new modules |
| `npm run check` | Pass; existing large-chunk warning only |
| `npm run validate:model` | Pass for all existing maker geometry/navigation scenarios |

The five full-suite failures are pre-existing/current-dirty-work issues: the gameplay-to-resort import in `NpcActorDriver.cpp`, two Attend catalog expectations, the compact window-width expectation, one ramp-boundary expectation, and four ocean sampling expectations. The new tests, existing `aquarium_tests`, docs check, map-maker test label, and title-screen headless smoke pass.

Render isolation was verified from the generated link graph: `title_screen_demo` does not link `aquarium_geometry`, the JSON adapter, Emscripten, Node, or maker code in Milestone 0. No renderer, shader, palette, shadow, aquarium asset, OWMAP, or aquarium population file was changed by this milestone.

## Performance evidence

- Native rectangle kernel, 2,000 samples after 100 warmups: p50 0.4320 ms, p95 0.6478 ms, p99 0.7698 ms, max 1.1264 ms.
- WASM JSON adapter, 200 samples after 20 warmups: p95 3.0888 ms.
- Both are below the Milestone 0/1 rectangle-generation target of 50 ms p95.

## Dependencies and environment

- Emscripten 6.0.8 is maker-side tooling only.
- Emscripten caches under `aquarium-maker/.cache/`, which is ignored.
- The normal Resort target has no Emscripten, Node, npm, Three.js, or maker dependency.
- Homebrew's existing `node@22` was reinstalled after the Emscripten dependency installation changed a shared library; normal `node` and `npm` commands were reverified.

## Next milestone boundary

Milestone 0 is approved. Milestone 1 begins from `codex/aquarium-construction-milestone-1` after both nested repositories' `main` branches contain the integrated pre-Milestone 1 state. It will add the first player-visible rectangle flow; no L/U, roundness, tunnel, or authored-tank editing work belongs in that milestone.

## Milestone 1 work log

- [x] Start from clean `codex/aquarium-construction-milestone-1` branches in both nested repositories.
- [x] Add explicit `aquarium12` construction configuration and placement mask.
- [x] Add semantic Z/Y activation and construction-owned input routing.
- [x] Add fixed rectangular draft, validation, confirmation, and cancellation.
- [x] Add construction camera and keyboard/mouse/controller overlay flow.
- [x] Add transactional per-profile document persistence and recovery.
- [x] Add dynamic collision, navigation ownership, and replaceable population policy.
- [x] Add committed bgfx tank rendering with isolated glass state.
- [x] Add automated tests and fault-injection coverage.
- [ ] Measure generation/upload/load timings and resource counts. Native generation is measured; runtime `loadUs`, `uploadUs`, mesh, vertex, triangle, and resource diagnostics are instrumented and await an interactive commit.
- [ ] Complete manual aquarium/outdoor/controller regression checks.
- [x] Present Milestone 1 review checkpoint and stop before Milestone 2.

### Milestone 1 implementation inventory

- `AquariumConstructionSession`: committed/draft state separation, rectangle placement, fast validation, player-cell protection, cancellation, and immutable commit candidates.
- `AquariumDesignStore`: canonical per-profile/map documents, read-back validation, durable temporary writes, validated backups, atomic promotion, and newer/invalid recovery behavior.
- `AquariumCollisionOverlay`: generated collision composed over static OWMAP terrain queries used by player, follower, and NPC movement.
- `AquariumPlayerRuntime`: rebuildable kernel geometry/navigation/collision plus replaceable population policy; the v1 policy supplies one deterministic Wishiwashi actor per tank.
- `AquariumConstructionOverlay`: nonnumeric grid, cursor, silhouette, fixed-height ticks, confirm/cancel glyphs, and pattern-plus-color validity feedback.
- `PlayerAquariumBgfxRenderer`: four semantic material passes, locally scoped glass/water state, stable transparent sorting, and staged candidate upload/publish/discard ownership.
- `Overworld3DTestScreenAquarium`: aquarium12 document lifecycle, worker generation, render-thread upload, transactional commit publication, diagnostics, and map-scoped construction configuration.
- `InputRouter` and `ScreenInput`: semantic Z/Y action plus construction-owned left-stick navigation that does not leak to other screens.

### Milestone 1 automated evidence

| Command | Result |
|---|---|
| `cmake --build build --target aquarium_runtime_tests title_screen_demo -j4` | Pass |
| Focused aquarium/input/headless `ctest` selection | 8/8 pass |
| `cmake --build build -j4` | Pass |
| `ctest --test-dir build --output-on-failure` | 68/73 pass; the same five pre-Milestone 1 baseline failures remain |
| `npm run check` in aquarium maker | Pass; existing large-chunk warning only |
| `npm run validate:kernel` in aquarium maker | Pass; parity hash remains `fnv1a64:5f18ef72142032cb` |

New automated coverage includes unavailable-map activation, keyboard Z/controller Y routing, construction-owned stick input, minimum rectangle sizing, overlap/bounds/player-cell rejection, cancellation invariance, stable IDs/revisions, canonical save round trips, backup recovery, newer-file preservation, failed-write injection, combined collision, navigation derivation, replaceable population policy, and staged fake-GPU publication/discard semantics.

### Milestone 1 performance and manual status

- Native rectangle kernel, 2,000 samples: p50 0.3166 ms, p95 0.4101 ms, p99 0.5593 ms, max 0.8143 ms.
- Latest maker WASM validation: p95 2.7295 ms; native/WASM canonical output remains identical.
- Runtime commits log `generationUs`, `uploadUs`, document `loadUs`, tank/mesh/vertex/triangle counts, and active resource counts.
- Interactive timing collection and the aquarium12/controller/outdoor visual checklist remain unverified because the macOS test session was locked during the attempted GUI run. The temporary aquarium12 startup override was restored immediately; normal startup is `assets/overworld/maps/0.owmap` and the normal headless smoke passes.
- Milestone 2 remains blocked until this checkpoint is reviewed and the pending interactive checks are completed.
