# Aquarium Construction Implementation Checklist

Updated: 2026-09-12

## Kyogre side-to-side banking correction — 2026-09-12

- [x] Trace reported lean oscillation to instantaneous yaw-rate-driven roll.
  Add `AquariumCruiseBank` in the motion module: suppress small/brief corrections,
  require one-second sustained turn, six-degree limit, 2.5s easing and 2deg/s cap.
  Unwind before opposite banking; preserve forward travel and escort priority.
- [x] Simulation fits reduced bank to available body clearance near glass,
  instead of an all-or-nothing reset to zero.
- [x] Add pure alternating/sustained/reversed steering and return-to-level tests.
- [x] Build and focused motion regression pass. Full native suite: 73/79,
  same six known failures (architecture rules, Attend config, config loader,
  authored aquarium fixture, ramp, ocean tile).
- [ ] Player review: Kyogre leans only in deliberate long turns, not frequent rocking.

Logs: `/tmp/aquarium-bank-build.log`, `/tmp/aquarium-bank-focused.log`,
`/tmp/aquarium-bank-suite.log`. Runtime-only; no saves, species size or shader edits.

## Large-room editing and massive-species sizing — 2026-09-12

- [x] Replace per-cell committed footprint regeneration with derived session
  indices in `AquariumConstructionCellIndex.cpp`; rebuild on configure, room-zone
  change and publish, including undo/redo. Index placement validation too.
- [x] Index floor heights once in `AquariumConstructionVisual.cpp` for large
  selected/draft overlays, preserving first-match and missing-cell behavior.
- [x] Eight largest baked aquarium models: 0.9 catalogue scale for Gyarados,
  Suicune, Lugia, Wailord, Milotic, Kyogre, Palkia, Dhelmise. Preserve capacity
  masks, curated animation, models, other species and player documents.
- [x] Added 64x64-room/60x60-tank test in `aquarium_large_room_tests.cpp`:
  occupancy correctness, configure/resize/delete/undo/redo invalidation, preview
  indices, and catalogue-to-physical-size consistency.
- [x] Same-machine microbenchmarks (seven samples, worst sample reported as p95):
  4,096 occupancy probes: 1,156.03ms → 1.66ms; selected world preview:
  83.92ms → 13.63ms. These isolate CPU operations, not end-to-end frame time.
- [x] Native envelope baker verified unchanged bounds/pose counts for all eight;
  refreshed only their scale-dependent source signatures, catalogue revision 182.
- [x] Final build and focused stocking/runtime/motion tests pass (3/3).
  Full native suite: 73/79; same six baseline failures (architecture rules,
  Attend config, config loader, authored aquarium fixture, ramp, ocean tile).
- [ ] Player review: edit/pan/resize large tanks; re-enter room to see scale changes.

Logs: `/tmp/aquarium-large-baseline.log`, `/tmp/aquarium-preview-baseline.log`,
`/tmp/aquarium-large-final-build.log`, `/tmp/aquarium-large-focused.log`,
`/tmp/aquarium-large-suite.log`.

## Kyogre steering stability and escort priority — 2026-09-11

- [x] Runtime-only change: own escorts cannot steer, stop, or push their host.
  Escort-side avoidance and tank containment remain active; no save/config edits.
- [x] Ease small cruise yaw corrections using the same law for actual movement
  and predictive arc validation (`AquariumMotion.cpp`).
- [x] `AquariumSimulation.cpp`: asymmetric crowd gates and penetration correction.
- [x] `aquarium_motion_tests.cpp`: three-minute actual-envelope Kyogre fixture
  asserts identical position/yaw/bank with four Remoraid versus alone; retains
  depth exploration, forward movement, and no-freeze checks. Small yaw easing test.
- [x] Build and focused motion/runtime/school tests pass (3/3).
- [x] Full native suite: 73/79, same six baseline failures (architecture rules,
  Attend config, config loader, authored aquarium fixture, ramp, ocean tile).
- [ ] Player review: smooth cruising and escort spacing in the saved tank.

Logs: `/tmp/aquarium-cruise-stability-build.log`,
`/tmp/aquarium-cruise-stability-focused.log`,
`/tmp/aquarium-cruise-stability-suite.log`.

## Remoraid host restoration, Kyogre pace and radial glow — 2026-09-11

- [x] Restore curated Remoraid escort profile. Stocked population resolves an
  explicit same-tank host before simulation, preferring Kyogre; inherit host speed.
  Hostless escorts retain existing same-species schooling fallback.
- [x] Kyogre cruise speed 0.45 → 0.585m/s. Narrow-tank/escort test uses new speed.
- [x] Dedicated Kyogre overlay fragment shader: dark 3s, outward light 1.2s,
  lit 2s, outward darkness 1.2s. Bounds derive from model-local stripe geometry.
  Same pulse in normal/post-fog passes; Lanturn emission and world shader unchanged.
- [x] Program/uniform cleanup follows existing renderer owners; extracted pose/
  pulse-bound helpers keep aquarium Pokémon renderer below 500 lines.
- [x] Build/shader compilation and focused host/speed/motion/emission checks pass.
- [ ] Player review: Remoraid group follows Kyogre, faster cruise still feels
  smooth, centre-outward pulse is readable at gameplay scale and through fog.

Logs: `/tmp/aquarium-pulse-build.log`, `/tmp/aquarium-pulse-focused.log`,
`/tmp/aquarium-pulse-suite.log`. No player saves or model assets rewritten.
Final native suite: 73/79; same six baseline failures, with runtime, motion,
schooling and emission tests passing. Shader compilation succeeded.

## Faster internal doors and narrow-tank cruiser recovery — 2026-09-11

- [x] Aquarium-only room-transfer script omits invisible tile-animation wait;
  0.14s close + 0.18s open + 0.12s forward step. Script action durations override
  only their own transition. Exterior/Attend defaults and doorway links unchanged.
- [x] Added catalog/order/timing checks alongside all-wall three-cell travel tests.
- [x] Inspect saved room_1 document read-only: Kyogre + four Remoraid in 24×19 tank.
  Add narrow-tank, actual-body-envelope population regression; allow slower
  collision-checked cruise arcs and preserve speed through the steering cache.
- [x] Cruiser recovery now selects a fresh destination, never a sub-metre target
  requiring a pivot. Body size, glass containment and forward-only turning remain.
- [x] Three simulated minutes with four escorts: 50.99m travel, 6.89m vertical
  range, 12-degree peak bank; no two-second freeze or stationary pivot.
- [ ] Player review: actual room transition latency and Kyogre's motion after
  re-entering/restarting. Saved data unchanged; loading cost itself is not reduced.

Logs: `/tmp/aquarium-room-speed-build.log`, `/tmp/aquarium-room-speed-suite.log`,
`/tmp/aquarium-escort-check.log`. Scripted-delay comparison is not a wall-clock
room-loading measurement.

## Tunnel navigation continuity and Kyogre habitat tour — 2026-09-11

- [x] Close the thin all-room navigation gap at below-floor tunnel glass level,
  retaining dry tunnel exclusion. Shared kernel ABI 16 invalidates older derived
  output; update the version-dependent rectangle hash, not its mesh contract.
- [x] Test the generated deep-tunnel geometry through runtime body navigation:
  side-water vertical travel, deep-to-upper routing and dry-corridor exclusion.
- [x] Kyogre tours low/high/middle water and banks up to 12 degrees when clear.
  Cruise arrival no longer demands hitting an exact point; goals level off with
  depth, including during obstacle avoidance. No recovery pivot or teleport.
- [x] Finer reversing-arc prediction prevents coarse integration from accepting
  impossibly tight turns. Cache predictions at 0.20–0.26s; validate actual steps.
- [x] Seeded actual-Kyogre-envelope 180-second fixture: depth range 8.09m,
  travel 54.32m, maximum bank 12 degrees. These are simulated distances, not live
  frame-rate measurements. Motion and geometry checks pass.
- [ ] Player checkpoint: the reported tunnel tank, mixed fish navigation, Kyogre
  surface/deep excursions, visible banks beside glass, and runtime responsiveness.
- [ ] Rebuild external maker WASM distribution against ABI 16 before maker use;
  this pass changes shared source and native game, not sibling tool artifacts.

Logs: `/tmp/aquarium-tunnel-tour-build.log`, `/tmp/aquarium-tunnel-tour-suite.log`.
Full native build passed; suite 73/79 with the same six documented baseline
failures. Motion, schooling, emission and shared geometry tests pass.

## Kyogre red-line emission — 2026-09-11

- [x] Inspect actual GLBZ line textures; scope additive self-lit emission to
  Kyogre's two red overlay materials. Reuse haloBrightness/fogRetention settings.
- [x] Preserve body/eyes, other species, model assets and shared shaders.
- [x] Build and focused emission test pass, including actual Kyogre material
  isolation and unrelated-species guard. Logs: `/tmp/kyogre-emission-*.log`.
- [ ] Player review: red markings visible in dim/murky water without lighting
  the whole body. This is emission, not a dynamic light or bloom pass.

## Large-room drawing and continuous cruiser turns — 2026-09-11

- [x] Replace the fixed 21×17 working patch (also used for mouse picking) with
  padded viewport-projected cell visibility. Preserve room wall/door clearance.
- [x] Large-cruiser profiles, including Kyogre, maintain forward travel during
  turns with body-scaled arcs, gentle vertical motion and swept-path look-ahead.
  Blocked translation cannot publish rotation, including during recovery.
- [x] Build and focused runtime/room/motion tests passed. Added a distant visible
  drawing-cell check and rearward-target cruising/blocked-pose checks.
- [ ] Player review: long Kyogre tank drawing at different zooms and wall edges;
  observe cruising arcs, wall approaches and ascent/descent in the actual tank.
  Live performance/visual quality remain unmeasured; no save/model/shader changes.

Logs: `/tmp/aquarium-cruise-build.log`, `/tmp/aquarium-cruise-focused.log`,
`/tmp/aquarium-cruise-suite.log`.
Full native suite: 73/79; the same six baseline failures remain (architecture,
Attend config, config loader, legacy aquarium mask, OWMAP ramp, RTPKS ocean).

## Four wall knobs and connected-room handles — 2026-09-11

- [x] Replace the directional toolbar with four projected wall knobs; support
  drag/release and click/move/click, plus keyboard/controller focus and gestures.
- [x] Green plus handles on doorless walls position new linked-room entrances;
  receiving doors are centred on opposite walls. Check/Y commits the whole draft.
- [x] Signed north/west resizing preserves stable tank and tunnel coordinates.
  Tank document envelope v6 records roomFrame on later tank saves; kernel stays v5.
- [x] Stage generated scenes and save one building document transactionally;
  maintain per-room stocking documents and preserve the south overworld exit.
- [x] Cache occupancy during gestures, revalidate at commit, and reject wall/tank
  conflicts without stuck input. No shader, aquarium AI or shared palette changes.
- [x] Native build and focused design/runtime/room tests. Added all-wall,
  three-cell bidirectional door travel and north/west transform coverage.
- [ ] Player checkpoint: wall knob readability, drag and click-click cancellation,
  controller-only flow, room creation/travel/restart, tank/tunnel alignment after
  origin changes. Measure live rebuild latency; no performance claim yet.
- [ ] Follow-ups: existing-door sliding and building undo/history. Tank history
  currently resets on origin-changing room commits, but is retained otherwise.

Primary files: AquariumBuildingScene, AquariumRoomRuntime, AquariumDesign,
Overworld3DTestScreenAquariumRooms/RoomHandles, construction HUD/visual adapters,
and room/design/runtime tests. Existing unrelated dirty changes remain preserved.

Verification: build passed; full native suite 73/79, retaining the six known
baseline failures (architecture rules, Attend config, config loader, aquarium
legacy mask, OWMAP ramp, RTPKS ocean). Design, runtime and room-layout tests pass.
Logs: `/tmp/aquarium-wall-handles-build.log`, `/tmp/aquarium-wall-handles-tests.log`.

## Room width/depth and wall clearance — 2026-09-11

- [x] Separate room draft, circular room action and four directional resize
  controls. R/right-stick opens; mouse or navigation adjusts; check/A/Y commits;
  cancel/B discards. Dotted outline is yellow when valid and red when blocked.
- [x] Profile-local `.room.json` uses versioned building DTOs, durable atomic
  promotion, validated backup recovery, and newer-version protection. No source
  OWMAP, tank document, or current player save was edited during development.
- [x] Runtime projection preserves tank coordinates and south-door horizontal
  offset; all three triggers and arrival anchor move with the south wall.
- [x] Build mask uses full half-cell-shifted footprints and inner wall clearance.
  Partial wall strips are not drawable; unchanged legacy edge tanks are preserved.
- [x] Shrink candidates include full tank occupancy and protected doorway lanes.
  Room commit reuses the map-transition renderer owner and keeps tank history.
- Changed modules: `AquariumRoomRuntime`, `AquariumRoomStore`, room screen adapter,
  construction HUD/preview, load/placement seams, and focused room/runtime tests.
- [ ] Player review: open Room mode; grow/shrink both dimensions; verify invalid
  red preview near tanks; cancel; confirm; walk the expanded edges; use all three
  south exit cells; re-enter/restart and verify tank stock and room persistence.
- [ ] Measure live room-rebuild hitch. CPU commit timing is logged as
  `[AquariumRoom] event=resized ... cpuUs=...`; GPU upload still follows normal
  map entry. No frame-budget claim is made for this first resize integration.
- Scope checkpoint: north-west anchored resizing only. Four-wall dragging,
  independent door sliding/creation, and room-command undo are not implemented.
- Verification: shipping build passes. Focused room/runtime tests pass (0.68 s);
  full native suite **73/79** in 40.94 s, with the same six baseline failing
  targets recorded below. Logs: `/tmp/aquarium-room-resize-build.log` and
  `/tmp/aquarium-room-resize-tests.log`. `git diff --check` passes. Live viewport,
  controller ergonomics and renderer-rebuild performance remain user checks.

## Gallery retirement — 2026-09-10

- [x] Remove `aquarium12` from runtime map and aquarium catalogs; retire its
  OWMAP and gallery-only GLB/navigation asset directories.
- [x] Close the Builder Lab's **north** opening and remove only its gallery
  anchor/link/three triggers. Preserve the south overworld exit, dimensions,
  terrain, and all player save documents.
- [x] Preserve legacy GLB/navigation/OWMAP test coverage under
  `tests/fixtures/legacy_aquarium`; one GLB remains test-only, not a game asset.
- Recovery copies: `/tmp/pokemon-resort-gallery-retirement.zYXgig` (temporary
  local backup); tracked originals also remain recoverable through Git.
- [ ] Player check after restart: north wall closed; all three south exit
  cells still transfer outside; saved tanks unchanged.
- This cleanup does not enable live door/room creation; that remains the next
  room-integration checkpoint below.
- Verification: full build passes; asset catalog and GLB regression tests pass;
  runtime map-project checks pass (sealed north boundary, three south triggers,
  resolvable destinations). Full native suite: **73/79**, matching the six
  previously recorded failing targets: architecture module rules, Attend config,
  config loader, aquarium catalog dimensions, OWMAP ramp entry, and ocean tiles.
  Logs: `/tmp/pokemon-resort-gallery-retirement.zYXgig/tests-final.log`.

## Working-state guardrails

- Pokémon Resort branch: `codex/aquarium-construction-milestone-5` from the approved Milestone 4 checkpoint.
- Aquarium maker branch: `codex/aquarium-construction-milestone-5` from the approved Milestone 4 checkpoint.
- Pre-Milestone 1 aquarium work was integrated into both repositories before these branches were created.
- Kernel ABI 14 is consumed by the maker's browser-only WASM adapter; Milestone 5 changes the Resort runtime and shared verification only.
- No reset, clean, checkout, destructive operation, or broad reformat was performed.
- Feature commits stage explicit Milestone 5 paths only.

## Independent rooms — foundation checkpoint, 2026-09-10

- [x] Separate `aquarium_room_layout` contract library; no live map/UI/save writes.
  Files: `rooms/AquariumBuildingLayout.hpp`, `AquariumBuildingLayout.cpp`,
  `AquariumBuildingLayoutJson.cpp`, CMake registration, and
  `tests/native/world3d/aquarium_room_layout_tests.cpp`.
- [x] Independent room-local signed bounds and stable door-ID connections.
  New receiving doors use opposite walls and exact whole-cell centring;
  default proposals are 17×13 (13×17 for side entrances). Existing rooms are
  never resized to those defaults. Door moves cannot change their wall or peer.
- [x] Pure candidate creation/move/resize, monotonic revisions, JSON round trips,
  malformed/newer-version rejection, full-cell occupancy checks, three-wide
  two-deep entrance protection, and door-to-door circulation validation.
- [x] Focused contract checks and full native build passed. No authored maps,
  tank saves, stocking, rendering, or external assets changed in this checkpoint.
  Full CTest: 73/79 passed, retaining the same six baseline failures (module
  boundary, Attend catalog, window config, aquarium layout, ramp and ocean UV).
  `git diff --check` passed.
- [ ] Live adapter: import existing dimensions/doors/anchors without changing
  links; project signed room coordinates consistently into floor, wall, tank,
  collision, camera, construction mask and destination-anchor coordinates.
- [ ] Profile store: transactional save/recovery and staged publication of
  scene resources; keep unknown/newer documents recoverable. No building file
  has been written yet; serialization tests are not save-recovery verification.
- [ ] Room-mode UI: wall/door handles, reliable click-to-start/click-to-finish,
  controller/keyboard equivalents, preview, cancellation, durable undo/redo.
- [ ] New-room wall ＋ actions and room-specific tank/save integration.
- Review checkpoint: foundation ready for review, **not a playable room editor**.
  Live manual acceptance will require moving each endpoint independently,
  traveling both ways, shrinking against a tank/entrance, cancel/undo/redo,
  and restarting without tank or doorway displacement.
  Documentation: `docs/gameplay/aquarium-rooms.md`.

## Aquarium AI follow-up — 2026-09-09

Approved sequence: A motion reliability → B living schools → C profile audit →
D replace the old gallery with a second construction room. Each checkpoint
requires user feedback before the next starts.

- A: implemented; automated checks recorded below; manual saved-tank observation
  and user approval pending. Not marked fully accepted without that review.
  New modules: `AquariumMotion`, `AquariumBodyNavigation`,
  `AquariumSimulationMotion`; runtime-only progress and routing diagnostics.
  Full-body swept containment, lazy shared clearance graphs, incremental
  round-robin routing, two-second progress recovery, stable-ID yielding, and
  upright hover locomotion. Kingdra selects the existing hover profile.
- B: implemented for visual review (2026-09-10); not fully accepted yet.
  Removed phase-offset school orbits. Added `AquariumSchoolSimulation`,
  `AquariumSimulationSchool`, and `AquariumConvexWater`; focused wiring in the
  simulation/header, native/map-maker source lists and test harness.
  Group keys use tank/species/form rather than individual stocking IDs.
  Snapshot-based local alignment/separation/cohesion, comfort zones, shared
  travel direction, bounded catch-up, independent animation phases, stable
  stocking order, and lone/orphan-escort fallback preserve curated content.
  Cached exact convex checks and conservative crowd broad-phase skips reduce
  repeated geometry work without removing swept-body containment.
  Focused checks pass: cylinder centroid travel 19.46 m over the sampled run,
  lateral RMS 1.31 m, vertical RMS 0.52 m, average member radius 1.03 m,
  heading coherence 0.918. Rectangle/L/tunnel minimum member travel over 20 s:
  13.04/9.40/11.95 m, with containment and step-size assertions.
  Debug AI-step p95 (32/64/128 fish): 0.89/2.60/8.40 ms. The 128-resident
  <=4 ms target remains unmet; optimized-build measurement and further crowd
  optimization remain open, not silently waived. No GPU/render/save changes.
  Verification: full native build passed; `--simulation-only` passed, including
  existing escort/activity regressions; full CTest 72/78 with the same six
  baseline failures (module boundary, Attend catalog, compact-window config,
  aquarium room-layout fixture, OWMAP ramp rise, and ocean UV fixtures).
  Independent `aquarium_school_tests` passed in that suite; final Debug p95
  was 0.80/2.65/8.46 ms. `git diff --check` passed.
  Manual checkpoint: restart and observe the saved Wishiwashi cylinder; look
  for a traveling cloud, side-by-side fish, smooth turns and rejoining after
  obstacles. Also check a lone fish and explicit Remoraid escorts. Real-model
  visual acceptance, complex-water regrouping quality and user approval pending.
- C/D: not started. Other profile tuning, gallery maps and gallery assets
  remain untouched this checkpoint.
- Baseline: `codex/aquarium-construction-milestone-5`, pre-existing changes in
  CMake, aquarium config/catalog, simulation, construction, rendering, UI,
  documentation, tests, and the builder map. No resets, commits, asset removal,
  save writes, or unrelated reformatting performed.
- Touched integration: simulation/header, player population hover mapping,
  Kingdra catalog profile/revision, native/map-maker source lists, aquarium
  test harness, architecture documentation and this checklist.
- Tests: new independent `aquarium_motion_tests` CTest registration;
  `aquarium_tests --simulation-only` runs the existing behavior fixtures without
  the stale authored-room layout assertions. Normal `aquarium_tests` still
  retains those assertions; failures must not be silently suppressed.
- Known baseline mismatch: existing aquarium scene test expects builder return
  cell `(12,16)` and the compact room; current untouched room config uses
  `(27,31)`. Room cleanup remains D, not part of the AI patch.
- Performance adaptation: cold fixture routing cost about 58 ms synchronously;
  runtime searches now advance in bounded batches instead. No per-frame full
  route scan for direct travel; each actual movement still validates swept
  body containment. Final measurements recorded below after verification.

### A verification and review checkpoint

- Shipping build: passes (`cmake --build build -j 4`).
- Focused motion and existing simulation fixtures: pass. A seeded baked-Kingdra
  fixture traverses 4.12 cells vertically in 30 simulated seconds, stays upright,
  and has at most 0.167 seconds without 0.05 m displacement. Identical seeds
  replay identical positions. A 90-second forward-cruiser fixture follows a
  route around a concave obstacle without teleportation or containment failure.
- Pure fixtures cover middle-band dry holes, swept full-body clearance,
  over-tunnel layered routing, quarter-cell narrow-passage refinement,
  disconnected volumes, visual/locomotion pitch separation, and zero-step
  progress detection. Population contract checks hover mapping without changing
  curated animation or scale.
- Cold obstacle fixture: 787 expanded nodes; 24-visit batch p95 about 1.5 ms.
  Two 30-second Kingdra replay simulations run in about 24–25 ms total after
  caching direct-path visibility (not an in-game render-frame measurement).
  Larger 32/64/128-resident AI benchmarks and neighbor indexing remain B/C.
- Mutation check: temporarily increasing the watchdog from 2 to 20 seconds
  fails with `valid zero displacement did not trigger the two-second progress
  watchdog`; restored immediately and rebuilt.
- Full native suite: 70/76 pass. Five previously documented failures remain:
  module boundary, Attend scene fixture, app window fixture, OWMAP ramp, RTPKS
  ocean sampling. The sixth is the untouched builder-room size/return-cell
  mismatch described above. No unrelated fixes or weakened assertions.
- Manual review: restart the game, observe Kingdra ascending/descending in its
  actual saved tank; inspect hoverers and forward swimmers near curves/tunnels,
  bottom rests and explicit escorts. Existing Wishiwashi orbit remains until B.
  No saved tanks, old gallery assets, shaders, or rendering configuration changed.

## Original construction milestone status

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
- `AquariumPlayerRuntime`: rebuildable kernel geometry/navigation/collision plus replaceable population policy; the current test policy supplies two deterministic Dewgong in the first player tank and one Kyogre with four Remoraid escorts in the second.
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
- `aquarium_builder_lab.owmap`: empty 24×18 procedural construction room; its config owns the 340-cell safe mask and no authored tank catalog entries.
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
- [x] Replace the earlier stress-test population with two idle-swimming Dewgong in the first player tank and one idle-animated Kyogre on a continuous collision-checked roaming loop in the second; remove Wishiwashi, Clamperl, and Pyukumuku from player tanks without changing authored aquarium populations.
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
- [x] Harden linked-room renderer ownership by retiring the source room before destination aquarium lighting, actors, floor cutouts, and player-tank meshes are published.
- [x] Raise both aquarium rooms' side/back walls from four to eight tiles, cap openings above three-tile door clearance, and add an aquarium-only black lower facade to hide deep-tank geometry below the front floor edge.
- [x] Expand each aquarium doorway to three movement triggers and move its activation row one cell south; halo-row south exits use the no-prestep transfer script while anchors remain centred and stable.
- [x] Separate dim aquarium-room ambience from bright exhibit lighting, apply the exhibit grade to authored tanks, player tanks, and Pokémon, and add configurable soft floor spill around tank footprints without global bloom or shared render-state changes.
- [x] Keep school and wander navigation moving around concave walls and dry tunnel volumes by abandoning blocked steering targets for deterministic validated detours.
- [x] Carry player-tank corner radius into the exhibit-light footprint so curved glass corners do not leave unlit floor wedges.
- [x] Add aquarium-only runtime LOD for player-tank Pokémon, preserving the original skeleton, animations, materials, UVs, and textures without creating duplicate model assets.
- [x] Expand the empty Builder Lab from 24×18 to 54×33 cells, recenter both three-cell doorways, and grow its doorway-safe construction mask from 340 to 1,600 cells without changing the authored gallery.
- [x] Keep the enlarged room responsive by skipping all construction-mask work while dormant and rendering only a camera-local 21×17-cell working window while editing.
- [x] Raise maximum player-tank height by five world tiles and maximum below-floor depth by three world tiles through shared ABI-15 limits.
- [x] Set the temporary two-tank population to two idle-swimming Dewgong in the first tank and one faster, vertically expressive circling Kyogre in the second.
- [x] Smooth Kyogre's travel vector and slow its yaw/pitch response, then add four slower collision-checked Remoraid escorts that steer only along their rendered forward axis into stable slots around its measured body and fall back from walk to idle animation through the existing Attend resolver.
- [x] Give the enlarged Builder Lab a map-local 128-tile camera far plane so long tanks are not clipped, without changing authored aquarium or outdoor cameras.
- [x] Add a player-water-only absorption shader with JSON-configurable intensity that darkens deep, long, and camera-distant water without extra draw calls, textures, geometry, or shared render-state changes.
- [x] Polish the aquarium view/editor: apply an aquarium-only 0.5-tile near plane so close swimmers remain visible above tunnels, remove the attenuation intensity ceiling for murky-water experiments, add bounded mouse-wheel construction zoom, and keep paired height/depth knobs at floor level inside tall tanks.
- [x] Reuse the exterior Black 2 sand tile (RTPKS tile 103/material 8) for player-tank substrate with per-cell repetition and a less-saturated tint, and reshape high-intensity attenuation so the water continues to evolve visibly with depth.
- [x] Tune the default attenuation to 2.4, expose aquarium-local `sandDarkening` at 0.18, desynchronize escort animation phases/rates, and derive escort cruise speed from the followed actor with bounded recovery acceleration.
- [x] Replace escort point pursuit with a dedicated fixed-step formation simulation: inherit measured attachment-region velocity through climbs/dives/turns, use attached/recovering interception without counter-swimming, calculate mesh-envelope Reynolds separation from an immutable simultaneous snapshot, retain persistent local obstacle detours, and isolate runtime LOD pose buffers per actor.
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
| Aquarium doorway/wall follow-up | Shipping target, `aquarium_tests`, `door_travel_tests`, `map_metadata_editing_tests`, project validation, headless startup, and 12-frame Metal map-maker smoke pass; the existing early-ramp-rise failure still prevents the combined OWMAP loader executable from reaching its later current-map assertions |

The unchanged five native-suite failures are the gameplay-to-Resort boundary
import, two Attend catalog fixture expectations, compact window width, early
ramp rise, and RTPKS ocean sampling. No file involved in those failures was
changed by Milestone 5.

### Review boundary

The code and automated release gate are ready. Do not declare v1 released or
begin post-v1 features until the user completes the live visual/controller and
map-transition soak above. Corrections found there remain Milestone 5 work.

## Post-Milestone 5 — player stocking vertical slice

- [x] Load the curator's versioned catalogue natively and expose approved entries only.
- [x] Store per-tank stable species IDs and counts in the authoritative aquarium design.
- [x] Add an undoable population command that uses the existing transactional save/publication path.
- [x] Add a selected-tank fish action and a sharp presentation-canvas stocking overlay using the shared Pokémon sprite assets.
- [x] Support keyboard/mouse and controller navigation, add, remove, and close actions.
- [x] Derive a depth-aware abstract capacity budget from tank footprint and vertical extent.
- [x] Remove the temporary tank-index population entirely; unstocked and intentionally empty player tanks remain empty.
- [x] Move the selected-tank Stock action to a circular top-left icon with a forgiving edge hit target.
- [x] Prefer visible tank-volume picking over floor-grid drawing when clicking an existing tank, preventing a missed selection from latching an accidental create gesture.
- [x] Keep the top-left fish action visible in both Browse and Selected states, disabled until a tank is selected, so stocking is discoverable and its availability is unambiguous.
- [x] Move the fish action and complete stocking panel onto the Metal-visible bgfx presentation path; cache the sharp SDL-rasterized panel by content so sprites, text, layout, and hit boxes agree without a per-frame texture rebuild or a hidden SDL duplicate.
- [x] Present the approved catalogue through the reusable transfer-style 6×5 Pokémon box viewport, with 30 slots per aquarium-only box, mouse/keyboard/controller box navigation, fit-disabled slots, and no dependency on the player's owned Pokémon or save boxes.
- [x] Add a Pokémon/Exhibit pill using aquarium-blue transfer chrome; Pokémon retains stocking behavior while Exhibit replaces the two boxes with four full-area water-presentation presets.
- [x] Match the Transfer screen's 1280×800 composition: one 560×577 catalogue box at left, one equally sized tank board at right, the tab toggle above the destination, the exit/basic-tool controls at upper left, and the full-width lower summary banner.
- [x] Make the basic red tool the default interaction: selecting a catalogue Pokémon picks up its sprite, pointer/controller movement carries it between the box and tank, dropping on the tank stocks it, and cancel returns it without changing the design. Controller focus crosses directly from the last occupied slot of a partial box into the tank target.
- [x] Expand the tank capacity view to the complete right-hand destination area with readable cells and independent mouse/controller vertical panning instead of shrinking large boards.
- [x] Persist River, Swamp, Open Ocean, and Depths per tank through the normal undoable transaction; keep those presets responsible for water/interior color while independently authored brightness and murkiness control illumination and absorption without changing authored tanks or global rendering state.
- [x] Increase visual separation between non-River color presets: Swamp is strongly green, Open Ocean is saturated blue, and Depths uses near-abyssal water with a restrained deep-blue spill. Selecting Depths initializes brightness at its minimum tick, while saved manual brightness remains authoritative when reopening a tank.
- [x] Grade the complete player-tank interior from its independent color and brightness settings: tint substrate and residents with the chosen environment, compensate resident intensity against the darker floor base, retain neutral River, and keep authored tanks, room lighting, and global rendering unchanged.
- [x] Replace the oversized Exhibit cards with a compact four-color row, independent discrete brightness and murkiness sliders, and Sand/Gravel/Moss/Dirt substrate cards. Persist every choice on the tank; render repeatable Black 2 floor materials from the RTPKS pack, including a runtime-composited authentic dirt/grass swamp floor; and make the final murkiness tick read as dense fog.
- [x] Make Exhibit input deterministic: hover is visual-only, hidden Pokémon-box hit targets cannot intercept Exhibit clicks, sliders track discrete ticks while held and commit once on release, and pointer movement does not rebuild the cached overlay unless visible content changes.
- [x] Replace the ineffective transparent-water approximation with a bounded scene-color/depth composite: rasterize each generated water silhouette, reconstruct the visible opaque surface, and measure only its camera-ray segment inside the tank. Use non-clamping exponential extinction and path absorption so sand near the glass stays light while sand seen through more water darkens progressively before drawing surface/glass. Accumulate premultiplied tank contributions instead of letting a later tank restore the shared pre-fog scene. Keep it at the DS-native world resolution with no ray marching or global fog.
- [x] Retain nine murkiness controls while resampling them across the useful original 0–5 response, with the final tick exactly matching former level 5; replace the 35% path-transmission floor with a gentler unclamped exponential and retain tank brightness as a floating-point draw multiplier instead of saturating it into 8-bit mesh colors.
- [x] Give player-built glass a dedicated low-cost material: clear face-on panes and soft silver-blue room reflections. Player review removed the waterline/animated bands and then broadened the angle response so flat panes retain a visible reflection (maximum opacity 18%). Uses the existing glass draw. Build verification completed; revised appearance awaits in-game review.
- [x] Scope minus-paint commands to the selected target tank. Crossing a neighboring tank no longer subtracts or deletes it; plus-paint retains intentional edge-touch merging.
- [x] Reuse Black 2 ocean tile 3287's translucent upper material for player-built surfaces only, preserving its 240-sample world-continuous UV track; expose aquarium-local playback speed and cross it with a softer reverse-moving copy after bounded fog without importing the opaque ocean base.
- [x] Install aquarium residents with deterministic model-envelope separation before their first visible frame; seed school members around an existing group and install configured followers after—and near—their leader regardless of authored roster order. If no separated valid position exists, omit the actor instead of spawning an overlap.
- [x] Preserve separation after spawn with nearest-neighbour predictive steering, catalogue-sized soft body cores, deep-core rejection, and navigation-safe post-step correction; keep full baked envelopes authoritative for glass/fit/spawn clearance. Add a slow, local, vertically biased `jelly-drift` profile for Tentacool/Tentacruel and Frillish/Jellicent.
- [x] Prevent collision deadlocks from producing in-place spinning by retaining a stable avoidance direction before selecting one new detour; make physical depth increase water absorption and ensure the Depths preset attenuates toward near-black rather than the brighter shared water hue.
- [x] Pack each resident's authored capacity mask into the tank-capacity grid and draw its Pokémon sprite over the occupied cells, so capacity is spatially legible instead of being only a numeric budget.
- [x] Apply data-driven stocking clearance to approved species: one extra column/row for the smallest normal masks and two for larger masks, retaining compact exceptions for Wishiwashi, Pyukumuku, and Barboach plus the already-maximum Wailord footprint. Golisopod consequently requires at least a four-cell-wide packing row rather than fitting in a 3×3 tank.
- [x] Anchor unpositioned crawler models by their rendered lower extent at the lowest player-tank water floor instead of inheriting the volume midpoint.
- [x] Use measured rendered horizontal bounds plus a player-tank glass comfort margin for every generated swimmer, including crawlers; authored shallow-pool overhang behavior remains unchanged.
- [x] Feed the exact generated tank boundary into exhibit-light spill rendering, so independently rounded and flat corners no longer share a maximum-radius light silhouette.
- [x] Add a catalogue-driven timid reef movement profile: Corsola receives randomized valid floor spawns, long idle intervals, very short local moves, and navigation-validated fleeing from nearby Mareanie or Toxapex.
- [x] Add catalogue-driven activity cadence: bottom residents alternate movement and idle animations, Shellder/Cloyster make rare short relocation bursts, Pyukumuku rests often, and benthic swimmers can leave the floor for a lower-water excursion before returning to rest.
- [x] Resolve approved catalogue model paths through the project root before runtime publication.
- [x] Bake versioned per-form physical envelopes with the native Attend loader across configured movement/rest animations; stale profiles fail closed without stocking-panel model loads.
- [x] Gate stocking through actual layered navigation, model height, glass clearance, and large-cruiser turning/travel space, then reuse the same envelope while spawning.
- [x] Keep capacity boards rectangular and automatically packed, preserve readable cells through independent scrolling, and communicate width/height/turning/capacity failures with contextual pictograms instead of text modals.
- [x] Restore the Builder Lab to its proven compact 24×18 layout and 340-cell doorway-safe mask after live testing showed the 54×33 expansion was too large; preserve the expanded room as the OWMAP recovery backup.
- [ ] Player review: verify panel layout, sprite readability, capacity feedback, repeated add/remove responsiveness, controller-only flow, save/reload, and undo/redo.

Focused verification: `aquarium_stocking_tests`, `aquarium_design_tests`,
`aquarium_command_tests`, `aquarium_runtime_tests`, and the shipping
`title_screen_demo` target pass. The shipped catalogue test confirms every
approved entry references an existing model. This is intentionally the smallest
stocking slice; mask packing is visible but direct mask dragging, catalogue filters, habitat
compatibility feedback, and richer per-profile locomotion remain follow-ups
after live UX review.
# Decoration UI simplification — 2026-09-12

2026-09-13 contextual visitor dialogue: 92 authored templates in a dedicated JSON
catalogue; watched-tank species/decorations/tunnels gate eligible lines. Room runtime
population supplies species names, not unspawnable stock selections. Shared recent
template history and least-recent reuse reduce repetition. Exactly two initiated
exchanges per visitor per building visit; exhausted visitors remain present but
cannot be targeted for dialogue. Outside-map entry resets allowances. Added catalogue
variety, context filtering, substitution, repetition and exchange-limit tests. Live
dialogue, second/third interaction and linked-room/exit reset review pending.
Verification completed 2026-09-14: shipping build and two focused visitor/dialogue
tests pass (0.38 s); native suite 76/82 with the same six baseline failures
(47.33 s). Whitespace check clean. No in-game visual QA claimed.

2026-09-13 viewing-time tuning: shipped visitor/follower viewing stops reduced from
15–120 seconds to 5–20 seconds through aquarium_visitors.json. No movement speed or
fish AI changes. Restart the scene to load the new session settings.
Verification: focused visitor test passes (0.50 s); native suite 76/82 with the
same six baseline failures (46.88 s). Whitespace check clean.

2026-09-13 snake-follow correction: retained last real movement target rather than
recomputing behind the owner's facing after a stop. Direction-only input no longer
cancels an exhibit visit. Normal following prefers the recorded player trail,
preserving corners; off-trail catch-up remains collision-aware. Companion watching
stays a separate idle action. Added stationary-target and corner-trail contracts;
live turn-in-place and companion-to-follow transition review pending.
Verification: shipping build and focused visitor/follower contracts pass (0.47 s);
native suite 76/82, unchanged six baseline failures (45.49 s). Whitespace check clean.

2026-09-13 trailing/companion fix: normal aquarium following now targets the owner's
previous movement cell or the cell behind their stopped facing, never any available
neighbor. Side-by-side viewing is an explicit idle opportunity requiring a matching
tank ID/facing and free side spot. Moving overrides it. Inspection passes original
world-facing to the follower. Tests cover all rear directions, blocked rear cells,
same-exhibit joining, moving priority and corner/trail following. Live following
and companion-view timing review pending.
Verification: shipping build and two focused camera/visitor tests pass (0.50 s);
native suite 76/82 with the same six baseline failures (46.44 s). Whitespace check clean.

2026-09-13 follower interest: camera-relative walk/idle row selection shared with
visitors. Aquarium-only follower planner reuses their collision-derived watch spots,
NPC reservations and wait/speed config, preferring new tanks with occasional owner
returns. Replaces nature idle/replay in this room only; movement cancellation uses
room-local walking paths, never idle-exit teleport or doorway travel. Added tests
for exhibit preference, occupied targets, owner returns, infeasible routes and
package-specific sprite rows. Live inspection facing, watch timing, owner resume,
NPC circulation and exterior follower regression checks remain pending.
Verification: shipping build and focused visitor/follower-planner contracts pass
(0.40 s); native suite 76/82, same six baseline failures (44.88 s). Whitespace check clean.

2026-09-13 visitor presentation follow-up: shuffled round-robin appearances replace
independent random draws; adult speed defaults to 0.55× ordinary walking via JSON.
Walking and idle visitor sprite rows now follow camera orientation without changing
simulation facing or resetting animation. Added cycle, speed-config and cardinal
camera-facing contracts. Live four-sided tank inspection and pacing review pending.
Verification: shipping build and visitor tests pass (0.44 s); full suite 76/82
with the six baseline failures unchanged (46.91 s). Diff whitespace check clean.

2026-09-13 visitor first slice: session-local room populations using existing human
NPC appearances, collision/motors/rendering and direct dialogue. Added configurable
area-based capacity, 15–120-second stops, visit history, route reservations,
linked-door transfers, exit despawning and entrance arrivals. Off-screen populations
pause; construction revalidates placement/capacity afterward. No persisted schemas,
assets, population of aquarium Pokémon, or exterior population rules changed.
Pure route/config tests added; pathfinding limited to one resident search per frame.
Measured standalone 128×128 route p95: 4.20 ms on this development build (not total
visitor frame time). Live first-entry distribution, 2-minute observation, dialogue,
moving/stationary collision, tunnel circulation, room travel and construction
revalidation remain manual review checkpoints; no visual QA claimed.
Verification: shipping build and three focused visitor/interaction/door tests pass
(0.13 s). Full native suite: 76/82, same six baseline failures (45.25 s).
Diff whitespace check clean. Await player feedback before further visitor features.

2026-09-13 first-view rear-tank fix: exclude other tanks whose footprint contains
the camera even when the eye is above their water surface. Projected foreground
obstructions no longer require their center to be in front of the camera; large
tanks can straddle that plane. Added elevated first-view coverage preserving
same-depth peers. Screenshot reproduction still needs in-game confirmation.
Verification: shipping build and inspection test pass (0.27 s); native suite
75/81 with the same six baseline failures (46.80 s); diff whitespace check clean.

2026-09-13 close-up correction: supersedes the stationary resident-focus follow-up
below. Camera travel stops outside the approached glass; optical zoom completes
small/distant resident framing. Overview/exit restore FOV. Visibility now also
hides nearer tanks overlapping the selected exhibit's projected bounds, keeping
same-depth neighbors and non-obstructing side tanks. No save or simulation changes.
Shipping build and focused inspection contracts pass (0.26 s); full native suite
75/81, same six baseline failures (45.85 s). Live small/deep resident focus and
foreground culling review remain pending.

Inspection follow-up: replaced selected-tank-only rendering with a camera-relative
hidden-ID list shared by meshes/fog/actors/light spill. Preserve same-depth peers
and tanks partially in front; hide fully rearward and overhead/overlapping tanks.
Resident focus now holds camera position outside the glass while changing aim.
Dedicated tests cover peer/rear/overhead visibility and stationary fish-focus camera.
Verification: shipping build and focused inspection test pass (0.29 s); full
native suite remains 75/81 with the six baseline failures (47.45 s). Visual review pending.

2026-09-13 inspection revision: forward-facing first view; whole-tank second view
with player visible; bounded mouse look; resident click-focus and stepwise exit.
Temporary selected-tank render filtering covers geometry, water/fog, residents,
decorations and light spill; the camera-side wall uses wall-local plane clipping.
No save/schema or simulation changes. Dedicated camera state/framing tests added;
live review of long/deep tanks, all four approaches, picking and restoration pending.
Verification: shipping build and new inspection contracts pass (including small
tanks always zooming out on second accept). Full native suite: 75/81, same six
baseline failures, 47.49 s. Final standoff adjustment rebuilt/focused-tested.

2026-09-13 tray click follow-up: click/release now places at the valid tank center
and keeps selection; an eight-logical-pixel threshold distinguishes dragging.
Dragging back to the tray still cancels. Placement validation/history are unchanged.
Build and focused decoration test pass; full suite retains the six baseline
failures (74/80, 45.32 s). In-game click/drag review pending.

2026-09-13 Black prop rendering follow-up: decoration instances now use clockwise
backface culling for the overworld camera basis, rather than copying Attend's
opposite-basis setting. Single-sided Black GLBs retain their exteriors; double-sided
Marine Park materials still bypass culling. No asset rebaking, normal inversion,
Pokemon cull changes or global render-state changes. Tests cover actual Black/MPE
material flags, projected triangle winding, and resident-policy isolation.
Live screenshot comparison remains pending player review.
Verification: shipping build passes; decoration regression passes (0.46 s).
Full native suite remains 74/80 with the six known baseline failures (46.29 s).

Default tuning follow-up: every general editor entry resets to maximum zoom-out
(1.8); newly placed decorations start at six scale ticks rather than four, without
changing existing saved sizes. Category glyphs now use filled, colored silhouettes.
Shipping build and focused runtime/decoration tests pass (1.58 s); full suite
remains 74/80 with the six baseline failures (46.64 s). Icon capture inspected.

Follow-up: corrected raw-event interception which consumed mouse callbacks before
InputRouter could dispatch them (also preserve mapped keyboard/controller actions).
Added real InputRouter pointer-sequence coverage, selected-tank camera centering,
four icon-only catalogue categories, and removed dotted capacity/height guides.
No persisted decoration format or fish behavior changed.
Follow-up verification: shipping build and focused decoration/input-router tests
pass (0.47 s); full native suite remains 74/80 with the same six baseline failures
(46.80 s). SDL category-tray capture inspected; live interaction review pending.

- [x] Keep normal Z-editor camera pose/angle; remove top-down/tool-driven tilting.
- [x] Reuse construction accept/cancel, trash and undo/redo glyphs and hit layout.
- [x] Preview-only lower catalogue, without asset-name labels; wheel pages tray.
- [x] Drag from tray to place; move/height/size/rotation knobs finish on release.
- [x] Preserve height on lateral moves; invalid drops and focus loss cancel drafts.
- [x] Keep 25-object limit and transactional arrangement save; no fish AI changes.
- [x] Focused decoration test protects transforms, history, capacity, icon hit
  layout, transparent HUD area, and repeated-UV plant previews.
- [ ] Player checkpoint: test tray drag/drop, all knobs, history and finish/cancel
  in-game before adding further decoration features.

Verification: shipping build passes; focused `aquarium_decoration_tests` passes
(0.43 s). Full native suite: 74/80, the same six baseline failures in architecture
rules, Attend config, config loader, legacy aquarium layout, OWMAP and ocean tile
tests. Final thumbnail-only adjustment was rebuilt and focused-tested afterward.
Inspected the SDL tray/knob capture; live bgfx interaction remains player review.
