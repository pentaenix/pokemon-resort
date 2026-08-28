# Aquarium Construction Implementation Checklist

Updated: 2026-08-28

## Working-state guardrails

- Pokémon Resort branch: `codex/aquarium-construction` from `16441aa3`.
- Aquarium maker branch: `codex/aquarium-construction` from `54b7fa6`.
- Both branches were created in their existing dirty worktrees; no reset, clean, checkout, or broad reformat was performed.
- Pre-existing Resort baseline: 34 tracked files modified plus untracked touch-pool, inspection-facing, wall-clip shader, and aquarium screen-extraction files. The baseline included `CMakeLists.txt` changes before construction work began.
- Pre-existing maker baseline: README, HTML, application, settings, panel, and model-validator changes plus untracked `scripts/rescale-glb-units.mjs`.
- Feature commits must stage explicit files or hunks only. Pre-existing aquarium/rendering work remains user-owned.

## Milestone status

| Milestone | State | Review |
|---|---|---|
| 0 — branches, baseline, contracts, kernel harness | Implemented | Awaiting approval |
| 1 — fixed rectangle vertical slice | Not started | Blocked on Milestone 0 approval |
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

Milestone 1 may begin only after Milestone 0 acceptance evidence is complete and reviewed. It will add the first player-visible rectangle flow; no L/U, roundness, tunnel, or authored-tank editing work belongs in that milestone.
