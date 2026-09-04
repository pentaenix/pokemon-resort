# Aquarium Construction Implementation Checklist

Updated: 2026-09-03

## Working-state guardrails

- Pokémon Resort branch: `codex/aquarium-construction-milestone-5` from the approved Milestone 4 checkpoint.
- Aquarium maker branch: `codex/aquarium-construction-milestone-5` from the approved Milestone 4 checkpoint.
- Pre-Milestone 1 aquarium work was integrated into both repositories before these branches were created.
- Kernel ABI 14 is consumed by the maker's browser-only WASM adapter; Milestone 5 changes the Resort runtime and shared verification only.
- No reset, clean, checkout, destructive operation, or broad reformat was performed.
- Feature commits stage explicit Milestone 5 paths only.

## Milestone status

| Milestone | State | Review |
|---|---|---|
| 0 — branches, baseline, contracts, kernel harness | Complete | Approved 2026-08-28 |
| 1 — fixed rectangle vertical slice | Complete | User authorized Milestone 2 on 2026-08-29; detailed usability comments deferred to review |
| 2 — editing and history | Complete | User authorized Milestone 3 on 2026-08-29 |
| 3 — height, L/U shapes, roundness | Complete | User advanced through the revision checkpoint |
| 3.5 — below-floor depth and derived volume | Complete | User advanced to tunnels after review |
| 4 — tunnels | Complete | User authorized Milestone 5 on 2026-09-03 |
| 5 — recovery, performance, release hardening | Review checkpoint ready | Automated gate complete; live visual/controller soak remains |

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
- Kernel ABI: 14.
- Current design schema: 5; schema 1/2/3/4 documents remain readable migration inputs.
- Rectangle hash: `fnv1a64:a7c4b0150a3c90e3`.
- Meshes: 5 semantic material groups (structure, flat sand, water volume, water surface, glass).
- Vertices: 188.
- Indices: 282.
- Triangles: 94.
- Collision: all 24 occupied cells.
- Navigation: 1 layer and 1 suggested spawn.
- Native/WASM result: exact JSON equality for all eight current fixtures, including depth, rounded-tunnel sand, bridge framing, and connected tunnel networks.

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

Milestone 5 is the final v1 hardening checkpoint. Feature expansion remains
out of scope until its live controller, rendering, save-recovery, and repeated
map-transition review is accepted.

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
- [x] Close review gap: keyboard/controller resize now chooses the nearest of all eight handles from the grid cursor and visibly highlights that handle before confirmation.
- [x] Close review gap: nearby authored collision remains marked as locked context even when its cells sit just outside the yellow construction mask.
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
| `cmake --build build --target title_screen_demo -j4` plus focused runtime/input/headless checks after review fixes | Pass; shipping integration compiled and 3/3 focused tests pass |
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
- The user explicitly approved Milestone 3 after this checkpoint.

## Milestone 3 work log

- [x] Bump the shared kernel to ABI 2 and retain schema version 1 compatibility.
- [x] Add deterministic rectangle, L, and U occupancy with quarter-turn rotation.
- [x] Add fitted quarter-cell corner roundness and deterministic rounded boundaries.
- [x] Generate shaped structure, flat sand, water, glass, collision, and navigation from one footprint.
- [x] Add native rectangle/L/U rotation, radius, winding, seam, finite, manifold, and safety-limit tests.
- [x] Add rounded-L and rotated-U golden documents alongside the rectangle fixture.
- [x] Make the Aquarium Maker browser runtime consume the exact kernel sources through WASM for the compatible subset.
- [x] Reject stale WASM ABI artifacts and stale asynchronous maker results.
- [x] Keep maker terrain, decor, rocks, plants, asymmetric profiles, and unsupported passages on its existing developer path.
- [x] Add in-game shape, height, rotation, roundness, L-notch, and U-opening property commands.
- [x] Keep property drafts immutable until review/build and make B/Escape discard only the property draft.
- [x] Add shape-aware selection, move, resize, collision, overlap checks, tank centres, population positions, and runtime geometry.
- [x] Add nonnumeric property ticks, live arcs, rotation/notch controls, and compact wrapping HUD layout.
- [x] Replace the hard-to-read world-space property handles with a contextual bottom tray; retain world-space gizmos only for spatial move/resize operations.
- [x] Route mouse wheel, keyboard Q/E/brackets, and controller LT/RT through semantic property actions.
- [x] Keep construction camera framing deterministic and driven by canonical grid coordinates.
- [x] Preserve `aquarium12` as the authored three-tank gallery and add a separate empty `aquarium_builder_lab` test room.
- [x] Add a playable exterior → builder-lab route, with optional north access to the authored gallery and south access back outside; keep both lab doorway lanes excluded from building.
- [x] Verify the lab has no authored models/cutouts, 340 build cells, a walkable centre aisle, and its own map-scoped save identity.
- [x] Keep the oversized overworld screen as an integration shell by moving construction pointer dispatch to `Overworld3DTestScreenAquariumConstructionUi.cpp`.
- [x] Stop at the Milestone 3 review checkpoint; do not start tunnel work.
- [ ] Player manual review: exercise every shape/property with mouse, keyboard, and controller in the Builder Lab.
- [x] Revision: route the normal exterior aquarium entrance directly to the empty Builder Lab while retaining the authored gallery through the lab's north doorway.
- [x] Revision: replace the rejected full-room fit and near-top-down view with a close camera six degrees above the normal room angle, a central dead zone, smooth cursor tracking, persistent mouse-edge scrolling, and an always-visible navigation banner.
- [x] Revision: reserve a safe world viewport outside the left tool rail and contextual property tray so UI clicks never move the cursor or manipulate a tank.
- [x] Revision: replace perspective-projected height/shape controls with direct nonnumeric choices for shape, height, fitted roundness, orientation, and L/U insets.
- [x] Revision: cut away only the tracked section of the near procedural wall during construction so the normal-style camera remains readable without changing authored geometry or shared render state.
- [x] Revision: hide the player and other overworld actors during construction, then return the player to a configured non-buildable doorway cell on exit.
- [x] Revision: make travel topology directional and reversible: resort → lab south door, lab north door ⇄ gallery, and lab south door → resort.
- [x] Usability revision: make framebuffer-scaled HUD drawing and logical-pointer hit testing share one coordinate contract so every button responds inside its visible bounds.
- [x] Usability revision: replace separate player-facing L/U choices and notch controls with rectangle-first cell subtraction; preserve legacy L/U documents and convert them to equivalent cuts only when editing.
- [x] Usability revision: reject enclosed, disconnected, overlapping, duplicate, and out-of-bounds cuts in the shared kernel; make cut commands transactional, cancellable, and undoable.
- [x] Usability revision: replace dot-choice rows with labeled Height and Corners cards plus nonnumeric less/rail/more steppers; keep mouse wheel, keyboard, and controller parity.
- [x] Usability revision: restyle the HUD as a cream/navy/cobalt/aqua game palette with wide labeled controls, clear hover/focus states, and restrained yellow accents.
- [x] Usability revision: inset yellow floor cells to create visible seams and apply the canonical `(+0.5, +0.5)` cell installation transform to grid rendering, picking, gizmos, camera focus, committed meshes, navigation, and Pokémon placement.
- [x] Usability revision: stow the follower during construction and after the exit teleport, then allow the ordinary follower controller to summon it only after the player takes a new step.
- [x] Usability revision: restore flat sand visibility with double-sided substrate/water surfaces and improve tank corners with continuous rails and explicit structure posts.
- [x] Usability revision: replace the Builder Lab's yellow procedural wall trim through map-scoped aquarium configuration without changing shared room materials or authored aquarium rendering.
- [x] Direct-manipulation revision: remove the modal tool/property workflow; one draw/add/subtract/move/resize/property gesture now produces one transactional undoable command.
- [x] Direct-manipulation revision: use left mouse/A for add, right mouse/ZL+A for subtract, L/R for undo/redo, X/contextual trash for delete, Y/checkmark for finish, and B for gesture cancellation.
- [x] Direct-manipulation revision: move construction text and the four remaining icon controls to the sharp 1280×800 presentation canvas; remove the bgfx block-font HUD.
- [x] Direct-manipulation revision: remove yellow-cell borders, retain separated inset fills, and add large screen-space move/resize/height/per-corner-radius knobs.
- [x] Direct-manipulation revision: advance to schema 3/ABI 4 for independently authored corner radii and separate structure, flat sand, water-volume, water-surface, and glass geometry.
- [x] Direct-manipulation revision: match the maker's standard vertical tank profile and smooth normals across rounded arcs; remove the rectangular corner posts that exposed polygon facets.
- [x] Player-review correction: composite the minimal vector HUD in the bgfx backbuffer so plus, minus, undo, redo, delete, and accept remain visible above the Metal view.
- [x] Player-review correction: replace press-drag-release with click-start, pointer-move, click-finish for drawing, painting, movement, resizing, height, and corner roundness.
- [x] Player-review correction: expose four side-only resize handles and dedicated convex-corner radius handles above the flat sand surface.
- [x] Player-review correction: advance to ABI 5, block every occupied footprint cell, and add the standard solid lower plinth beneath the lower rim.
- [x] Player-review correction: let plus/minus gestures arm in empty exterior cells; allow plus paths to grow irregular footprints and merge player tanks, and make minus a reversible rectangular selection that cuts across multiple tanks.
- [x] Player-review correction: represent multi-tank paint as one exact tank-set command so merge and multi-delete preserve IDs/properties and undo/redo atomically; enlarge the visible undo/redo pointer targets.
- [x] Player-review correction: replace the ambiguous history glyphs with heavier directional symbols and keep an explicit red cancel control beside green accept throughout every active draft/review state.
- [x] Player-review correction: accept empty subtract selections as no-ops, auto-dismiss invalid mouse finishes, give subtract mode priority over gizmos, remove the obsolete player-cell blocker, cover half-cell south/east collision overlap, simplify low-resolution icons, and delay camera tracking by roughly two cells.
- [ ] Player visual review: verify icon theme, pointer feel, knob separation, material colors/transparency, and full mouse/controller happy paths in the Builder Lab.

### Milestone 3 direct-manipulation verification

| Command | Result |
|---|---|
| `cmake --build build -j4` | Pass; shipping executable and 74 native targets compile |
| Focused aquarium design/command/runtime/kernel, overlay, input, and headless tests | 7/7 pass |
| `ctest --test-dir build --output-on-failure` | 69/74 pass; exactly the five documented baseline failures remain |
| `npm run check` in aquarium maker | Pass; existing large-chunk warning only |
| `npm run validate:kernel` in aquarium maker | Pass; three native/WASM goldens equal, ABI 5, hash `fnv1a64:536e7ce1f5e725f9`, WASM p95 1.2703 ms |
| `npm run validate:model` in aquarium maker | Pass for standard, tunnels, shaped tanks, independent corners, below-floor tanks, and decor scenarios |

Latest paint-control correction: the native command/runtime tests now cover an
exterior-start L-shaped add, bridging and merging two tanks under the selected
stable ID, exterior-start rectangular subtraction across two tanks, complete
multi-delete, explicit draft cancellation, matching generated collision, and
exact undo/redo restoration. The HUD tests also verify the cancel control and
forgiving undo/redo icon-edge hit targets. No schema,
kernel ABI, maker artifact, shader, or shared render state changed in this
correction.

| Paint-control correction command | Result |
|---|---|
| Shipping executable build | Pass |
| Aquarium design/command/runtime/catalog/kernel, overlay, and input selection | 8/8 pass |
| `title_screen_headless_smoke` | Pass |
| `owmap_overworld_loader_tests` | Reaches only the unchanged ramp-entry baseline assertion after all project/map checks pass |

The SDL game window is not exposed through macOS accessibility, so this pass
could not automate a real pointer walkthrough in the shipping window. The
visual/manual items above remain the explicit player review boundary; tunnels
remain out of scope until that review is accepted.

### Milestone 3 implementation inventory

- `shared/aquarium_geometry/Footprint`: canonical integer occupancy, rotated dimensions, fitted radii, and deterministic boundary tracing.
- `shared/aquarium_geometry/GeometryBuilder`: semantic shaped meshes plus collision and navigation derived from the same footprint.
- `AquariumConstructionSessionProperties`: discrete property drafts and shape-specific notch/opening constraints.
- `AquariumConstructionSessionSubtract`: rectangle-first exterior-connected cell cuts, including legacy L/U conversion and draft validation.
- `AquariumTankEditing`: shape-aware occupancy, selection, centres, movement, and eight-direction resizing.
- `AquariumConstructionVisual` and `AquariumConstructionHudLayout`: spatial move/resize gizmos, safe-view hit testing, contextual property choices, tick/arc feedback, and compact focusable controls without numeric labels.
- `Overworld3DTestScreenAquariumConstructionUi`: construction-only pointer, HUD, gizmo, and semantic property dispatch.
- `aquarium_builder_lab.owmap`: empty procedural construction room; its config owns the 340-cell safe mask and no authored tank catalog entries.
- Aquarium Maker `resortKernel.ts`: strict whole-cell compatible-subset mapper with advanced-feature fallback.

### Milestone 3 deterministic and performance evidence

- Kernel ABI 5 native/WASM parity is exact for all three goldens.
- Rectangle content hash: `fnv1a64:536e7ce1f5e725f9`.
- Native rectangle generation p95: 0.2552 ms.
- Native rounded-U generation p95: 2.1779 ms.
- Maker WASM generation p95: 1.2703 ms.
- All remain far below the 50 ms rectangle and 100 ms rounded-shape budgets.

### Milestone 3 automated evidence

| Command | Result |
|---|---|
| `cmake --build build -j4` | Pass; shipping executable, geometry benchmark, runtime tests, and Map Studio targets build |
| Focused aquarium/design/runtime/input/map/headless selection | All feature tests pass; the OWMAP executable reaches only its documented baseline ramp assertion after the new project/door/lab checks pass |
| `ctest --test-dir build --output-on-failure` | 69/74 pass; exactly the same five documented baseline failures and no Milestone 3 failure |
| `pokemon_resort_map_maker --validate-project` | Pass; five maps and five unique sources, including both aquarium rooms |
| `npm run check` | Pass; existing maker large-chunk warning only |
| `npm run validate:kernel` | Pass; three exact native/WASM goldens, ABI 5, rectangle hash `fnv1a64:536e7ce1f5e725f9` |
| `npm run validate:model` | Pass for the maker's complete existing standard, tunnel, below-floor, shape, passage, and decor matrix |

Revision verification:

| Command | Result |
|---|---|
| `cmake --build build -j4` | Pass; all game, shared renderer, test, and Map Studio targets link after moving the construction visual/layout implementation to its renderer owner |
| Focused aquarium/runtime/input/map/headless tests | Aquarium runtime, geometry, input routing, and headless smoke pass; OWMAP project/door checks pass before its unchanged baseline ramp assertion |
| `ctest --test-dir build --output-on-failure` | 69/74 pass; the same five documented baseline failures and no construction UX failure |
| `pokemon_resort_map_maker --validate-project` | Pass; five maps and five unique sources |

The tracked-camera test verifies a `-61°` normal-style pitch six degrees above
the live `-55°` gameplay camera, readable adjacent-cell separation at the
native 400×250 world resolution, no movement inside the central dead zone,
explicit recomposition when the property tray opens or closes, camera movement
beyond the edge, and bounded room framing. HUD tests verify
that panels are outside the safe world viewport, direct property choices are
mouse-hit-testable, roundness always preserves the exact current value, and
large L/U inset ranges remain a bounded previous/current/next stepper.
Config tests require the safe return cell to be present, outside
the build mask, and paired with a cardinal facing. Runtime rendering suppresses
overworld actors only while construction is active; no shared rendering state
or aquarium presentation material changed.

The five full-suite failures remain the baseline gameplay-to-Resort module
import, two Attend catalog expectations, compact window-width expectation, ramp
entry expectation, and ocean sampling expectations. None of their files were
changed for Milestone 3. The title-screen headless smoke, aquarium presentation,
door resolution, project validation, and all new construction tests pass.

Latest usability-revision verification adds native kernel coverage for simple
exterior-connected cuts, enclosed holes, and disconnected results; runtime
coverage for cut/commit/undo/cancel; schema 1→2 compatibility; and an
offset-sensitive rendered-grid pointer target. Maker `check`, kernel parity,
and the complete model validator pass with ABI 5. Automated macOS visual
control cannot attach to the bare SDL executable, so the player-visible and
controller-only checks remain intentionally open for the review checkpoint.

| Latest revision command | Result |
|---|---|
| `cmake --build build -j4` | Pass; shipping executable and all native targets link |
| Focused aquarium/design/command/runtime/input/headless checks | Pass; OWMAP reaches only its unchanged ramp baseline assertion after builder-lab checks pass |
| `ctest --test-dir build --output-on-failure` | 69/74 pass; exactly the same five baseline failures and no new failure |
| `npm run check` | Pass; existing large-chunk warning only |
| `npm run validate:kernel` | Pass; ABI 5 parity, rectangle hash `fnv1a64:536e7ce1f5e725f9`, WASM p95 1.2703 ms |
| `npm run validate:model` | Pass for the maker's complete model-validation matrix |

### Milestone 3 review boundary

- Height, fitted roundness, rectangle-first subtraction, legacy L/U compatibility, spatial handles, and the separate empty test room are implemented.
- Tunnels remain entirely Milestone 4 work.
- Manual visual and control review in the Builder Lab is intentionally left to the user checkpoint; any corrections stay in Milestone 3 before tunnel work begins.

## Milestone 3.5 — below-floor depth and stocking volume

- [x] Add authored integer `depthSteps` with schema 1/2/3 reads defaulting to zero.
- [x] Generate the maker-compatible below-floor profile around the canonical room datum: lower plinth and sand descend, opaque structural body reaches Y=0, and glass begins at Y=0.
- [x] Derive deterministic water capacity in whole litres from the generated navigation footprint and water-height band; carry it on each runtime tank for future population/stock policy.
- [x] Add a distinct centre depth knob beside the move knob. Mouse uses click–move–click; moving down increases depth. Controller uses ZL + up/down while ZR + up/down remains height.
- [x] Rebuild exact rounded/subtracted floor cutouts only for tanks with positive depth and roll them back if candidate publication is discarded.
- [x] Add a below-floor native/WASM golden and maker compatibility mapping without adding maker dependencies to the game.
- [ ] Player review: verify the depth knob, floor opening, below-floor water/sand visibility, camera readability, mouse flow, and controller-only flow in Builder Lab.
- [x] Verify four exact native/WASM goldens at ABI 6; rectangle hash `fnv1a64:4de4c4c06e5a7006`, WASM p95 0.9590 ms.

Focused verification:

| Command | Result |
|---|---|
| `cmake --build build --target title_screen_demo aquarium_runtime_tests aquarium_geometry_tests -j4` | Pass |
| `aquarium_runtime_tests` and `aquarium_geometry_tests` | Pass, including depth/volume, knob hit target, schema migration, and exact half-cell floor cutout coverage |
| `npm run check` | Pass; existing maker chunk-size warning only |
| `npm run validate:model` | Pass for the complete maker model matrix and the new compatible below-floor mapping |
| `npm run validate:kernel` | Pass; four exact native/WASM goldens, ABI 6, WASM p95 0.9590 ms |

The user authorized Milestone 4 on 2026-09-01. Volume remains derived data and
is never an editable or duplicated numeric field in the authoritative save document.

## Milestone 4 — tunnels

### First playable checkpoint

- [x] Create clean Milestone 4 branches in Resort and Aquarium Maker.
- [x] Advance the shared kernel to ABI 10 and schema 5 after player review separated the walking-cell tunnel lattice from the shifted tank drawing lattice, limited traversable collision to the authored centreline, and aligned tunnel floors with the maker's open-floor/glass-bridge rules.
- [x] Validate ordered whole-cell straight routes and routes with exactly one orthogonal elbow.
- [x] The initial slice required two outward-facing boundary portals and rejected intersections; ABI 14 retains self/segment-overlap protection while adding explicit interior junctions.
- [x] Generate a fixed two-cell-wide, slightly taller arch shell, portal openings with glass infill above the arch, and a room-level dry floor strip from the same centreline.
- [x] Separate conservative shell blockers from the wider visual/nav profile and clear collision only on authored centreline walking cells; shoulder cells remain blocked.
- [x] Generate lower water regions around the dry corridor, full water above the crown, explicit dry-volume metadata, reachable spawns, and reduced derived water capacity.
- [x] Add direct portal sockets and click/A → route → click/A interaction without adding a permanent toolbar mode.
- [x] Keep tunnel drafts cancellable and publish a completed tunnel as one transactional, undoable tank edit.
- [x] Add canonical tunnel serialization, native geometry/runtime coverage, and an exact native/WASM straight-tunnel golden.
- [x] Migrate schema-4 tunnel routes onto the schema-5 walking lattice and require tanks at least three vertical levels tall.
- [ ] Player review: verify portal visibility, pointer selection, controller route laying, arch/floor appearance, walking collision, and Pokémon presentation.
- [x] Add a visible midpoint delete knob for each existing tunnel plus Delete/controller-X cursor deletion; removal is transactional and undoable.
- [x] Allow independent tank-corner rounding whenever the fitted arc and complete tunnel portal opening do not overlap; conflicting corner edits remain visible but invalid.
- [x] Leave the room floor visible through standard tunnels; below-floor tunnels retain flat sand and underwater navigation beneath glass panels with thin frame rails and cell-edge separators.
- [x] Keep legacy player designs loadable while requiring every section affected by a new add/subtract command to remain at least three cells wide.
- [x] Make normal placement a stable click-start/click-finish rectangle gesture; remove implicit hover-paint after tank selection, merge drawn footprints that overlap or share an edge, and keep tanks separate when a full empty-cell gap remains.
- [x] Make below-floor tunnel glass readable as a floor using an ABI-11 cool-grey scaffold: maker-proportioned raised side rails and shallow crossbars divide every transparent walking-cell panel without changing collision or navigation.
- [x] Frame both tunnel portals with the same cool-grey scaffold material so entrances remain legible against glass and water without narrowing the walking opening.
- [x] Preserve every flat-sand region when a tunnel meets a rounded tank end by sanitizing clipped geometry before shared sand triangulation and navigation publication.
- [x] Replace the rejected depth ladder with an x-ray dotted tank wireframe: the actual rounded bottom perimeter and vertical corner guides descend to the authored discrete depth while the compact depth knob remains unobtrusive.
- [x] Replace the height pip column with the same dotted-volume language: draft height shows the actual rounded top perimeter and vertical guides while retaining the direct height knob.
- [x] Feed every committed player tank's derived navigation layers and population-policy output into the existing aquarium simulation; generated swimmers now move with rendered-body clearance through deep/layered water while avoiding tunnel dry regions, and player-tank replacement leaves authored swimmers intact.
- [x] Expand the temporary player-tank testing policy to six schooling Wishiwashi, one stationary bottom-anchored Clamperl, and one floor-wandering Pyukumuku per tank without changing authored aquarium populations.
- [x] Register player-built tank bounds with the existing two-stage aquarium inspection camera after load and every committed edit, using close-focus tuning without altering authored tank camera presets.
- [x] Distinguish the move gizmo from height/depth controls with a compact four-direction planar compass while preserving the established click–move–click interaction and hit target.
- [ ] Route compatible discrete Aquarium Maker passage settings through ABI 10; the maker UI still keeps passages on its advanced legacy geometry path, while the shared JSON golden already proves native/WASM parity.
- [ ] Extend portal cutting and water-region construction to rounded, rotated, L/U, and exterior-subtracted tanks after the rectangular seam is visually accepted.
- [x] Add connected multi-exit tunnel networks: a new route may end on or cross one existing interior junction cell, producing three or four exits under the existing portal-to-route gesture.
- [x] Require one clear walking-grid tile between the glass edges of independent tunnel networks, reject shared segments, and replace intersecting tube walls with a shared transparent junction canopy.

Focused verification:

| Command | Result |
|---|---|
| Resort geometry, design, runtime targets and `title_screen_demo` | Pass |
| `aquarium_geometry_tests`, `aquarium_design_tests`, `aquarium_runtime_tests` | Pass |
| `npm run validate:kernel` | Pass before the floor-scaffold revision; five exact native/WASM goldens, ABI 10/schema 5, rectangle hash `fnv1a64:80593cdcf5817e67`, WASM p95 1.4190 ms |
| `npm run validate:kernel` after floor-scaffold revision | Pass before portal frames; six exact native/WASM goldens, ABI 11/schema 5, rectangle hash `fnv1a64:3f7ec27c7b4f0782`, WASM p95 0.7381 ms |
| `npm run validate:kernel` after portal-frame revision | Pass; six exact native/WASM goldens, ABI 12/schema 5, rectangle hash `fnv1a64:980d524c41647655`, WASM p95 0.7292 ms |
| `npm run validate:kernel` after rounded-tunnel sand fix | Pass; seven exact native/WASM goldens, ABI 13/schema 5, rectangle hash `fnv1a64:f971f058bcfafa00`, WASM p95 0.7314 ms |
| `npm run validate:kernel` after connected tunnel networks | Pass; eight exact native/WASM goldens, ABI 14/schema 5, rectangle hash `fnv1a64:a7c4b0150a3c90e3`, WASM p95 0.7300 ms |
| `npm run check` | Pass; existing maker chunk-size warning only |

Latest navigation-integration verification: `aquarium_tests`,
`aquarium_runtime_tests`, and the shipping `title_screen_demo` target pass. The
architecture documentation freshness check passes; the module-boundary check
still reaches its pre-existing `NpcActorDriver.cpp` gameplay-to-Resort import
failure, which this change does not touch.

Review boundary: this is intentionally the smallest end-to-end tunnel slice.
Do not add shaped/rounded seams or existing-tunnel editing until the player has
checked portal readability, route feel, geometry, and collision in Builder Lab.

## Milestone 5 — recovery, performance, and release hardening

### Implemented

- [x] Create clean matching Milestone 5 branches without disturbing the approved Milestone 4 commits.
- [x] Recover a validated backup, displaced `.previous` primary, or fully written `.tmp` candidate after an interrupted save promotion.
- [x] Treat a newer-schema primary, backup, previous, or temporary artifact as read-only and refuse to overwrite it from an older executable.
- [x] Keep invalid artifacts in place while authored aquarium content remains loadable and construction disables safely.
- [x] Retire replaced player-aquarium GPU generations after three rendered frames; drain active, staged, and retired handles on shutdown.
- [x] Stress 256 resource-generation swaps and prove every fake GPU handle is destroyed exactly once with a bounded retirement queue.
- [x] Log load status, schema, kernel ABI, revision, operation names, geometry/resource counts, water volume, generation/upload budgets, and measured timings.
- [x] Add explicit stale-kernel and newer-design-schema rejection coverage so derived data cannot cross an incompatible version boundary.
- [x] Benchmark rectangle, rounded shape, deep connected tunnels, and the eight-tank room limit after warm-up.
- [x] Keep persistent derived caches disabled for v1: measured generation is already comfortably inside budget, while an extra cache would add another corruption/recovery surface. Any future cache must be disposable and keyed by document content, kernel ABI, schema, material profile, and render format.
- [x] Run the complete Resort native build/test gate and the maker type/build, native/WASM parity, and full model validation gates.
- [x] Add a hot-reloadable aquarium-only building presentation config with direct camera height/distance controls and a scoped room/tank/actor light grade; initialize it slightly lower, closer, and bluer without touching non-aquarium maps.
- [ ] Player live gate: controller-only extended Builder Lab session, mouse/keyboard session, repeated aquarium/overworld transitions, restart/recovery walkthrough, and screenshots of Builder Lab plus authored aquarium12/outdoor scenes.
- [ ] Confirm live commit telemetry meets the 4 ms GPU-upload target and produces no construction-induced frame above 33 ms on the release machine.

### Measured evidence

Native kernel benchmark, 2,000 samples after 100 warmups:

| Fixture | Tanks/sample | p50 | p95 | p99 | Maximum |
|---|---:|---:|---:|---:|---:|
| Rectangle | 1 | 0.1605 ms | 0.2009 ms | 0.2307 ms | 0.3597 ms |
| Rounded U | 1 | 1.2516 ms | 1.3289 ms | 1.3908 ms | 35.9917 ms |
| Deep four-exit tunnel | 1 | 1.7904 ms | 1.8925 ms | 1.9567 ms | 48.0223 ms |
| Eight rounded U tanks | 8 | 10.0738 ms | 10.2240 ms | 10.4655 ms | 55.2164 ms |

The p95 values are below the 50 ms rectangle, 100 ms complex-geometry, and
20 ms eight-tank CPU targets. The isolated maximum spikes did not affect p95;
live frame/upload telemetry remains part of the player gate.

| Verification | Result |
|---|---|
| `cmake --build build -j4` | Pass; all configured native targets and the shipping executable build |
| `ctest --test-dir build --output-on-failure` | 69/74 pass; exactly the five documented pre-construction baseline failures remain |
| Save interruption/newer-version and 256-generation lifecycle tests | Pass in `aquarium_runtime_tests` |
| Kernel ABI/schema invalidation and geometry suite | Pass in `aquarium_geometry_tests` |
| `npm run check` | Pass; existing maker chunk-size warning only |
| `npm run validate:kernel` | Pass; eight exact native/WASM goldens, ABI 14, WASM p95 1.3035 ms |
| `npm run validate:model` | Pass for the maker's complete standard, shaped, tunnel, depth, navigation, and decor matrix |

The unchanged five native-suite failures are the gameplay-to-Resort boundary
import, two Attend catalog fixture expectations, compact window width, early
ramp rise, and RTPKS ocean sampling. No file involved in those failures was
changed by Milestone 5.

### Review boundary

The code and automated release gate are ready. Do not declare v1 released or
begin post-v1 features until the user completes the live visual/controller and
map-transition soak above. Corrections found there remain Milestone 5 work.
