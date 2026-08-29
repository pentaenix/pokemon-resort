# Aquarium Construction Implementation Checklist

Updated: 2026-08-29

## Working-state guardrails

- Pokémon Resort branch: `codex/aquarium-construction-milestone-2` from the reviewed Milestone 1 commit `777a809a`.
- Aquarium maker branch: `codex/aquarium-construction-milestone-2` from clean `main` at `85b75ed`.
- Pre-Milestone 1 aquarium work was integrated into both repositories before these branches were created.
- Milestone 2 changes are confined to Pokémon Resort; the maker branch remains clean because the shared kernel contract did not change.
- No reset, clean, checkout, destructive operation, or broad reformat was performed.
- Feature commits stage explicit Milestone 2 paths only.

## Milestone status

| Milestone | State | Review |
|---|---|---|
| 0 — branches, baseline, contracts, kernel harness | Complete | Approved 2026-08-28 |
| 1 — fixed rectangle vertical slice | Complete | User authorized Milestone 2 on 2026-08-29; detailed usability comments deferred to review |
| 2 — editing and history | Review checkpoint ready | Awaiting player review and revision comments |
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
- [x] Measure generation/upload/load timings and resource counts. Interactive revision 2 recorded `generationUs=6935`, `uploadUs=137`, and a subsequent `loadUs=6895`; two tanks own eight semantic meshes and sixteen bgfx vertex/index-buffer resources by the renderer's resource-count contract.
- [ ] Complete manual aquarium/outdoor/controller regression checks.
- [x] Present Milestone 1 review checkpoint and stop before Milestone 2.

### Milestone 1 usability remediation — blocking approval

Player review found that the construction grid is not visibly readable and placement movement is not usable. Passing semantic input unit tests is therefore insufficient. Milestone 1 remains open until the complete creation interaction below works in the shipping bgfx path.

- [x] Replace the abstract/difficult-to-see layout with a world-aligned construction grid over the actual aquarium floor.
- [x] Render every allowed build cell as a warm translucent yellow tile with a strong yellow border, slight floor offset, and stable depth behavior without changing shared render state.
- [x] Show unavailable/locked cells and authored tanks as visually distinct obstacles; use patterns and icons as well as color.
- [x] Keep the complete build zone framed by the construction camera and readable against glass, sand, water, and authored geometry.
- [x] Root-cause and fix keyboard, D-pad, and left-stick cursor movement in the running game, including held-stick repeat and dead-zone behavior.
- [x] Add pointer-to-world-grid hit testing using the canonical cell transform; mouse interaction operates on the projected world cells.
- [x] Add a reusable semantic gizmo model, renderer, focus state, and hit-test layer. It begins with creation gizmos in Milestone 1 and is extended rather than reimplemented later.
- [x] Add an anchor gizmo, active opposite-corner resize gizmo, footprint outline, occupied-cell silhouette, and explicit Place/Review/Build/Adjust/Cancel/Exit controls.
- [x] Separate footprint completion from building. Releasing a drag or choosing the opposite corner enters Draft Review and does not regenerate or save the tank.
- [x] Support mouse press-drag-release: press chooses the anchor, held movement resizes, and release enters Draft Review.
- [x] Support mouse click-move-click: a click without a drag chooses the anchor, pointer movement previews the rectangle, and the second click enters Draft Review.
- [x] Support keyboard/controller place-move-place: A/Enter chooses the anchor, grid movement resizes, and A/Enter chooses the opposite corner and enters Draft Review.
- [x] In Draft Review, A/Enter or the visible Build control starts the transaction; B/Escape returns to adjustment; Z/Y cancels the draft to Browse. Cancel does not change the document or runtime.
- [x] Show visible invalid markers on offending cells and a short nonnumeric reason when a footprint cannot be built.
- [x] Add shipping-path interaction tests and deterministic visual-model tests; capture the Metal shipping window to verify that the world grid, gizmos, HUD labels, and invalid hint are submitted and visible.
- [ ] Manually verify the complete mouse, keyboard-only, D-pad-only, and left-stick-only flows in aquarium12 before requesting another Milestone 1 review.

#### Revised Milestone 1 creation state flow

```text
Dormant → Entering → Browse
Browse → CreateAnchor
CreateAnchor → ResizeFootprint
ResizeFootprint → DraftReview
DraftReview → Building → Browse
DraftReview → ResizeFootprint       (Adjust / B / Escape)
ResizeFootprint → Browse            (Z / Y or explicit discard)
Browse → Exiting → Dormant
```

`CommittedDesign` remains untouched throughout CreateAnchor, ResizeFootprint, and DraftReview. Only Build may create a transactional commit candidate.

### Gizmo allocation by milestone

| Milestone | Gizmos and visualization owned by the milestone |
|---|---|
| 1 — rectangle creation | Shared gizmo foundation; yellow world-cell grid; hover/focus cell; anchor handle; opposite-corner resize handle; footprint outline/silhouette; validity markers; Build/Adjust/Cancel affordances. These are blocking Milestone 1 requirements. |
| 2 — editing and history | Tank selection outline; centre move handle; four corner and four edge resize handles; original-versus-candidate ghost; locked authored-tank badge; delete confirmation target; undo/redo state feedback. |
| 3 — height, L/U, roundness | Vertical height handle with discrete layer pips; rotation handle; L-notch and U-opening handles; corner-radius arc handles and fitted-radius preview. No numeric labels. |
| 4 — tunnels | Boundary portal sockets; entry/exit handles; cell-centred route; elbow handle; direction arrows; occupied/dry corridor cells; invalid crossing/connectivity markers. |
| 5 — hardening | Prompt consistency, scalable/high-contrast variants, reduced motion, color-vision review, focus/audio/rumble polish, visual regression captures, and performance/resource stress coverage for all gizmos. |

The shared gizmo foundation owns semantic handles, focus, hit testing, and visual state only. Construction commands and documents remain authoritative; gizmos never mutate saved data directly.

### Revised Milestone 1 acceptance evidence

- The yellow build cells are plainly visible in an aquarium12 screenshot at the normal game resolution.
- The rendered grid and mouse hit target resolve to the same canonical cell in odd/even transform fixtures.
- Ten consecutive steps in each direction work with keyboard, D-pad, and left stick without moving the player.
- Mouse drag and click-move-click produce the same canonical rectangle document as keyboard/controller place-move-place.
- Drag release and opposite-corner placement stop in Draft Review without changing revision, save files, collision, navigation, actors, meshes, or GPU generation.
- Build from Draft Review performs exactly one transaction and one revision increment.
- B/Escape adjustment and Z/Y draft cancellation restore predictable states with no leaked resources.
- A controller-only user can enter construction, understand the grid/gizmos, place and review a tank, build or cancel it, and exit construction.
- Outdoor and non-aquarium maps render no construction grid and receive no construction movement.

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
- Player testing subsequently demonstrated that the grid is not visibly readable and placement movement is not usable. This supersedes the automated checkpoint and reopens Milestone 1.
- Interactive timing collection still awaits a successful player-visible commit. The temporary aquarium12 startup override was restored; normal startup is `assets/overworld/maps/0.owmap` and the normal headless smoke passes.
- The user authorized moving into Milestone 2 on 2026-08-29 and asked to defer detailed comments until its review checkpoint.

### Milestone 1 usability-remediation evidence

- Root cause: the original construction overlay used the SDL presentation renderer, but the shipping macOS bgfx path releases that renderer before Metal initialization. The session cursor could change while the overlay was never submitted. The construction visual now has a dedicated bgfx world/HUD renderer.
- The camera now derives its target from the configured build-zone bounds; the prior fixed pose did not reliably frame aquarium12's construction mask.
- A real Metal-window capture at the normal 800×500 game viewport shows the warm-yellow cell grid over aquarium12, locked/dark occupied cells, cursor and anchor/resize diamonds, red invalid cells with non-color markers, a `SPACE IS BLOCKED` hint, and labeled `BUILD`, `ADJUST`, and `CANCEL` controls.
- Projected-cell tests use the same `Gen4FollowCamera::worldToScreen` transform as the rendered world mesh, so pointer selection and visualization share the canonical whole-cell coordinates.
- Input-router tests demonstrate ten consecutive whole-cell repeat steps for keyboard, D-pad, and left stick; the left stick remains construction-owned and honors its dead zone/release contract.
- Focused verification after the remediation: `aquarium_runtime_tests`, `input_router_tests`, and `title_screen_headless_smoke` pass 3/3.
- Temporary direct-to-aquarium launch and forced-draft code used for capture was removed immediately after visual QA. Normal title-screen startup and `assets/overworld/maps/0.owmap` were restored.
- Player feedback was deferred, and the user explicitly advanced work to Milestone 2 on 2026-08-29.

## Milestone 2 work log

- [x] Create clean `codex/aquarium-construction-milestone-2` branches in both repositories without changing maker files.
- [x] Add semantic create/edit/delete commands with exact before/after values, stable tank IDs, and document-order preservation.
- [x] Add move and all eight whole-cell resize-handle transformations.
- [x] Add player-tank selection while keeping authored geometry non-selectable and physically visible.
- [x] Add selected outlines, a centre move gizmo, four corner and four edge resize gizmos, and projected mouse hit targets.
- [x] Add original-position ghosts plus valid/invalid candidate silhouettes and non-color invalid markers.
- [x] Add deletion confirmation and make confirmed deletion undoable.
- [x] Add undo/redo history with branch invalidation and monotonically increasing durable revisions.
- [x] Route edits, deletion, undo, and redo through the Milestone 1 worker-generation, validation, staged-GPU, transactional-save, and publication pipeline.
- [x] Add operation tokens so cancelled or superseded worker results cannot publish.
- [x] Add keyboard shortcuts, mouse manipulation, and controller-only palette focus/actions without leaking construction input to gameplay.
- [x] Split construction UI integration into a focused source file rather than expanding the oversized screen implementation further.
- [x] Add structured command kind, history action, operation token, revision, generation, upload, mesh, and resource diagnostics.
- [x] Add automated command, history, cancellation, overlap/bounds, stale-worker, visual-gizmo, input-isolation, serialization, and resource-lifecycle coverage.
- [x] Verify selected and move-draft visuals in the shipping Metal renderer; cancel the draft and confirm the profile remained at revision 2.
- [ ] Player manual review: complete mouse flow and controller-only flow, then supply revision comments.
- [x] Stop at the Milestone 2 review checkpoint; do not start height/L/U/roundness work.

### Milestone 2 implementation inventory

- `AquariumConstructionCommand`: immutable semantic create/edit/delete commands and deterministic forward/reverse application.
- `AquariumConstructionHistory`: publication-bound undo/redo stacks; cancelled drafts and failed candidates never enter history, and a new command clears the abandoned redo branch.
- `AquariumTankEditing`: cell selection, footprint enumeration, canonical centre selection, whole-cell movement, and eight-direction resizing.
- `AquariumConstructionSession`: selected/edit/delete states, committed-versus-draft separation, operation tokens, history candidates, monotonic revisions, and stable selection across publication.
- `AquariumConstructionVisual` and the bgfx construction renderer: selection, ghost, validity, move/resize gizmos, action palette, disabled history state, and high-contrast controller focus.
- `Overworld3DTestScreenAquariumConstructionUi`: construction-only pointer projection, gizmo/HUD dispatch, palette focus, error feedback, and exit restoration.
- The existing runtime builder, collision overlay, population policy, save store, and staged bgfx resource owner are reused unchanged as the publication boundary.

### Milestone 2 automated and visual evidence

| Command | Result |
|---|---|
| `cmake --build build -j4` | Pass; normal title startup restored after visual QA |
| `ctest --test-dir build -R 'aquarium_(runtime\|command)_tests\|input_router_tests\|title_screen_headless_smoke' --output-on-failure` | 4/4 pass |
| `ctest --test-dir build --output-on-failure` | 69/74 pass; exactly the same five documented baseline failures, with no new failure |
| `npm run check` in aquarium maker | Pass; existing large-chunk warning only |
| `npm run validate:kernel` in aquarium maker | Pass; parity hash `fnv1a64:5f18ef72142032cb`, WASM p95 1.7829 ms |
| `npm run validate:model` in aquarium maker | Pass for all existing geometry, tunnel, below-floor, and decor validation scenarios |

New tests demonstrate exact move/edit/delete inversion, stable identity and order, history branching, newer-operation token precedence, cancellation from selection/move/resize/review/delete/building, authored-obstacle non-selection, overlap and bounds rejection, canonical undo serialization, projected centre/corner gizmo hit testing, disabled/enabled history presentation, and controller edit-input capture.

Shipping Metal inspection at the normal 800×500 game viewport showed both loaded player tanks, the yellow build grid, cyan selected footprint, centre and eight perimeter handles, focused Move/Resize/Delete/Undo/Redo/Done palette, blue original footprint, green valid move candidate, and red cross-hatched out-of-bounds candidate with `OUTSIDE BUILD AREA`. The temporary aquarium12 startup/selection harness was removed immediately; the unsaved draft was cancelled and the saved document remained revision 2.

### Milestone 2 acceptance and review boundary

- Selection, move, eight-direction resize, deletion, undo, and redo are represented by commands and use the same transactional commit pipeline.
- Stable IDs survive every edit and history operation; revisions increase for forward commands, undo, and redo.
- Invalid overlaps and out-of-bounds edits cannot prepare or publish commands.
- Authored tanks are not part of the player document and cannot be selected or modified.
- Cancel and stale worker publication leave document, collision, navigation, population, GPU ownership, history, and save revision unchanged.
- Mouse users can select a tank/click its centre or edge gizmos/drag/review; keyboard and controller users can reach the same actions through semantic focus and whole-cell cursor movement.
- Milestone 3 remains blocked until the user reviews this checkpoint and explicitly approves height, L/U shapes, and corner-roundness work.
