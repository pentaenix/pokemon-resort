# OpenHome Cross-Generation Lessons for Pokemon Resort

## Executive Summary

OpenHome's main architectural lesson is that cross-generation transfer is safer when a Pokemon has a rich canonical record and every game file is only a projection of that record. Its `OHPKM` / `OhpkmV2` model stores normal gameplay fields, identity fields, generation-specific side sections, original/unconverted PKM bytes, recent-save provenance, and past-handler data. Conversion code then filters or regenerates only what the target generation can represent, and return sync merges gameplay changes back into the canonical record without letting a lossy cart projection erase newer-only data.

Pokemon Resort already has several matching ideas: raw snapshot preservation, `PokemonHot`, warm/cold JSON, mirror sessions, PID transport mapping, static-vs-mutable mirror return policy, nickname repair, ribbon gain-only merge, and bridge-side PID/nature/shiny finalization. The biggest remaining gap is that many important fields are canonical only by convention inside raw bytes or warm JSON, not by a typed Resort model. That makes projection and return merge easier to regress, especially for ribbons, Pokerus, contest stats, friendship, memories, handlers, marks, and PID-derived constraints.

This report is reference-only. It recommends adopting OpenHome's architectural lessons, not copying code.

## Current Verification Notes - 2026-05-04

This corrected document was not present in `pokemon-resort/docs/` at session start, so the reviewed OpenHome lessons document was copied here first and then re-verified against the current code.

Implemented and verified in current Resort:

- `MirrorProjectionService` emits canonical ribbon catalog data from `resort_catalog.ribbons` and `resort_catalog.ribbon_flags` into bridge `pre_save_review.ribbon_flags`.
- `MirrorProjectionService` emits canonical Pokerus strain/days from `resort_catalog.pokerus` into bridge `pre_save_review`.
- `BridgeProjectReconcile` no longer uses `PidWithNature`; Gen 3/4 projection uses a final PID solver plus validation for nature, shiny, gender, form, and ability slot.
- Bridge import reads true ribbon flags, numeric ribbon counters, contest fields, memory fields, markings, and Pokerus detail into warm `resort_catalog`.
- Mirror return merge gain-only unions ribbon flags/catalogs and protects static hot identity fields.
- Mirror return merge now preserves canonical Pokerus on absent/zero-only incoming data and accepts non-zero incoming infection/cure evidence.
- Bridge Pokerus projection/import now prefers PKHeX's precise `PokerusStrain` / `PokerusDays` properties, with older property-name fallbacks.

Still planned or incomplete:

- Contest stats are imported into warm JSON but are not yet emitted as typed projection inputs by `MirrorProjectionService`.
- Friendship is projected from canonical warm JSON, but mirror return still strips incoming friendship wholesale pending a trainer/handler-aware mutable policy.
- Ribbons are replayed by bridge reflection with aliases and gain-only merge tests, but Resort still lacks a first-class typed ribbon/mark catalog and full real-save old-gen earned-ribbon round-trip fixtures.
- Pokerus remains warm-catalog based rather than a typed canonical field.
- The OpenHome-style canonical model for IVs/EVs/nature/stat nature/memories/handlers/geography/future-only side data remains a planned hardening step.

## What OpenHome Does Well

OpenHome uses `OHPKM` in [OpenHome/src/core/pkm/OHPKM.ts](/Users/vanta/Desktop/title_screen_demo/OpenHome/src/core/pkm/OHPKM.ts) and a sectioned Rust `OhpkmV2` in [OpenHome/pkm_rs/src/ohpkm/v2.rs](/Users/vanta/Desktop/title_screen_demo/OpenHome/pkm_rs/src/ohpkm/v2.rs). `MainDataV2` stores PID/personality, encryption constant, species/form, item, OT/TID/SID, exp, ability index/number, markings, nature/mint nature, fateful flag, gender, EVs, contest stats, Pokerus byte, memory ribbon counts, ribbons, sociability, size/scale, moves/PP/PP Ups, nickname/isNicknamed, relearn moves, IVs, egg state, handler fields, origin game, language, form argument, affixed ribbon, met/egg data, ball, hyper training, home tracker, OT friendship/memory/affection, and tracking timestamp in [OpenHome/pkm_rs/src/ohpkm/v2_sections.rs](/Users/vanta/Desktop/title_screen_demo/OpenHome/pkm_rs/src/ohpkm/v2_sections.rs).

It then stores generation-specific sections: `GameboyData` for DVs and Gen 1/2 EVs, `Gen45Data` for encounter/performance/shiny leaves/PokeStar/NS Pokemon, `Gen67Data` for super training, geography, resort event status, and AVs, `SwordShieldData`, `BdspData`, `LegendsArceusData`, `ScarletVioletData`, `PastHandlerDataV2`, `MostRecentSave`, plugin data, tags, original backup bytes, and unconverted PKM bytes. This gives newer-only fields somewhere to live while a Pokemon visits an older game.

Projection is format-owned. Each save class delegates to `PK*.fromOhpkm`, for example `G3SAV.convertOhpkm` and `G5SAV.convertOhpkm`; individual PK classes filter fields for their target generation. `PK3` filters ribbons to Gen 3 contest/standard sets, writes Pokerus byte, contest stats, EVs, IVs, moves, met data, nickname, and regenerates a PID through `generatePersonalityValuePreservingAttributes` in [OpenHome/packages/pokemon-files/src/pkm/PK3.ts](/Users/vanta/Desktop/title_screen_demo/OpenHome/packages/pokemon-files/src/pkm/PK3.ts). `PK4` and `PK5` do similar projection with Gen 4 ribbon sets and format-specific met/nickname handling.

Backward movement is explicitly lossy but reversible. `generatePersonalityValuePreservingAttributes` in [OpenHome/packages/pokemon-files/src/util/util.ts](/Users/vanta/Desktop/title_screen_demo/OpenHome/packages/pokemon-files/src/util/util.ts) searches for a PID preserving nature, gender, shiny state, and Unown letter where applicable. Unsupported moves are filtered and replaced by level-up moves when needed. Unsupported ribbons stay canonical in OHPKM and are simply omitted from the older projection.

Return sync is deliberately merge-oriented. `OHPKM.syncWithGameData` updates exp, moves, evolution/form, nickname only when not a truncation/default-name artifact, held item, mutable ability cases, EVs, AVs, hyper training, contest stats, markings, ribbons by `unique([...])`, Pokerus only when incoming is non-zero, recent save, trainer/handler friendship and memories according to original trainer identity, shiny leaves, performance, fame, super training, geography, stat nature, battle origin, tera data, and TM/TR flags.

Identity tracking uses multiple keys. `getHomeIdentifier` uses base species, TID/SID, PID, and origin game; `getMonGen345Identifier` computes the PK3-compatible generated PID for OHPKM before looking up Gen 3/4/5 projections; `getMonGen12Identifier` uses DVs and trainer/name constraints in [OpenHome/src/core/pkm/Lookup.ts](/Users/vanta/Desktop/title_screen_demo/OpenHome/src/core/pkm/Lookup.ts). This is conceptually similar to Resort's mirror sessions plus PID transport registry.

OpenHome tests mirror the architecture. `pk3.test.ts` verifies Gen 3 EVs, ribbons, contest stats, OHPKM round-trip bytes, Gen345 lookup keys, and nickname conversion. `Ohpkm.test.ts` checks format/evolution persistence and move filtering. Rust tests in [OpenHome/pkm_rs/src/tests.rs](/Users/vanta/Desktop/title_screen_demo/OpenHome/pkm_rs/src/tests.rs) run `PKM -> OHPKM -> PKM` and `OHPKM -> PKM -> OHPKM -> PKM` byte comparisons across fixtures.

## What Pokemon Resort Currently Does Differently

Resort's canonical shape is smaller and split across [pokemon-resort/include/resort/domain/ResortTypes.hpp](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/resort/domain/ResortTypes.hpp): `PokemonHot` contains visible state and identity fields; `PokemonWarm` is JSON; `PokemonCold` is suspended JSON; `PokemonSnapshot` stores raw imported/projected bytes; `MirrorSession` stores target game, active state, beacon TID/OT, sent species/form/level/exp/DV, canonical PID, transport PID, and projection JSON.

Projection starts from a stored raw snapshot, not from a fully typed canonical Pokemon. [MirrorProjectionService.cpp](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/resort/services/MirrorProjectionService.cpp) chooses a target-format snapshot, falls back to the most advanced snapshot, sends raw bytes to the PKHeX bridge, and overlays warm/hot canonical data through `pre_save_review`, `hot_mutable_overlay`, `move_reconciliation`, and ribbon catalog replay.

The bridge does the heavy projection work in [BridgeProject.cs](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tools/pkhex_bridge/BridgeProject.cs). [BridgeProjectPastProjection.cs](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tools/pkhex_bridge/BridgeProjectPastProjection.cs) manually builds older-generation targets when PKHeX conversion is not enough. [BridgeProjectReconcile.cs](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tools/pkhex_bridge/BridgeProjectReconcile.cs) applies friendship, Pokerus strain/days, static fields, nickname, hot mutable overlay, ribbon catalog replay, and final Gen 3/4 PID-derived identity validation.

Return merge is policy-based. [PokemonMergeFieldPolicy.hpp](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/resort/domain/PokemonMergeFieldPolicy.hpp) documents static fields that mirror returns must not overwrite and mutable fields that may update. [PokemonMergeService.cpp](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/resort/services/PokemonMergeService.cpp) preserves original PID, overlays level/exp/HP/status/held item/moves, updates species/form/gender/ability only on evolution, strips incoming cart static warm keys, remembers removed moves, gain-only merges ribbon catalog maps, and does not promote return raw bytes to canonical checkpoints.

Import data comes from [BridgeImport.cs](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tools/pkhex_bridge/BridgeImport.cs), which reads hot fields, raw bytes/hash, warm JSON, `resort_catalog.friendship`, `pokerus`, `static_fields`, `ribbons`, `ribbon_flags`, `memory_fields`, `contest_fields`, and markings. [BridgeImportAdapter.cpp](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/resort/integration/BridgeImportAdapter.cpp) parses only a subset into typed `PokemonHot`; the rest remains in warm JSON.

## Data Model Gaps in Pokemon Resort

Compared to OpenHome, Resort does not yet have typed canonical fields for IVs, EVs, nature/stat nature, contest stats, Pokerus byte/strain/days/status, ribbons/marks as first-class sets, friendship triplets, trainer/handler memories, handler identity, markings, relearn moves, TM/TR/tutor flags, hyper training, super training, geography, egg data, fateful flag, affixed ribbon, scale/height/weight, form argument, language-specific nickname normalization state, and generation-specific side data. Some are preserved in raw snapshots or warm JSON, but they are not first-class policy inputs.

Resort does store things OpenHome does not expose in the same way: `pkrid`, `origin_fingerprint`, revision/history tables, `mirror_sessions`, PID transport registry, `SnapshotKind`, raw hash provenance, `pid_history_json`, and app-specific warm/cold catalogs. Those are useful and should stay.

Fields to promote or strongly structure in Resort canonical data:

- Identity/projection: original PID, current/transport PID history, encryption constant, TID/SID/TID32, OT, language, origin game, met data, ball, fateful flag, home tracker when available, Gen 1/2 DV16.
- Gameplay mutable: nature/stat nature, ability id plus abilit[... ELLIPSIZATION ...]chema, not only `projection_json`.
- Add lookup tests for deterministic projected PID returns, active mirror beacon ambiguity, and return after evolution/form change.
- Keep `original_pid` immutable and treat transport PID as a per-leg alias only.

## Specific Lessons for Ribbons

OpenHome stores ribbons canonically as OHPKM ribbons plus memory ribbon counts. Projection filters ribbons by target set: Gen 3 uses `Gen3ContestRibbons` and `Gen3StandardRibbons`; Gen 4/5 use `Gen4Ribbons`; modern formats use modern ribbon sets. Gen 3 contest ribbons are tier/count encoded by taking the maximum tier per contest category in `gen3ContestRibbonsToBuffer` / `gen3ContestRibbonsToBytes`.

OpenHome reads ribbons back and `syncWithGameData` unions them, then recomputes `contestMemoryCount` and `battleMemoryCount` as max/count-derived values. This treats ribbons as gain-only.

Resort already imports ribbon properties into `resort_catalog.ribbons` and `ribbon_flags`, gain-only merges them in `mergeRibbonCatalogMapsGainOnly`, strips incoming false values, and replays canonical ribbon catalog in `ApplyCanonicalRibbonCatalog`. The likely gaps are property coverage, target-generation filtering, and typed count semantics. A false/missing property should never clear a ribbon; numeric counters should use max; target-incompatible ribbons should remain canonical but absent from projection.

## Specific Lessons for Pokerus

OpenHome stores `pokerusByte` canonically. PK3/PK4/PK5 constructors copy it into target payloads, and `syncWithGameData` only updates canonical Pokerus when the incoming byte is meaningful/non-zero. That avoids clearing infection/cured data when a projection lacks the exact representation.

Resort imports Pokerus into warm JSON as `strain_or_state`, `days`, and status. Projection sends strain/days through `pre_save_review`. Mirror-return warm merge now preserves existing non-zero canonical state on absent/zero-only imports and accepts non-zero incoming infection/cure evidence. The remaining gap is model shape: Pokerus is still warm JSON rather than a typed canonical field, and full real-save round-trip coverage should prove the exact PKHeX strain/day bytes across Gen 3/4/5.

## Proposed Patch Plan

### Phase 1 - Observability and tests

- Files likely involved: [PokemonMergeService.cpp](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/resort/services/PokemonMergeService.cpp), [MirrorProjectionService.cpp](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/resort/services/MirrorProjectionService.cpp), [BridgeProjectReconcile.cs](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tools/pkhex_bridge/BridgeProjectReconcile.cs), [mutable_merge_policy_tests.cpp](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tests/native/resort/mutable_merge_policy_tests.cpp), [BridgeProjectConversionTests.cs](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tests/unit/pkhex_bridge/PKHeXBridge.UnitTests/BridgeProjectConversionTests.cs), and new integration tests using `saves/`.
- Exact behavior to change: add diagnostic notes/log fields for projected canonical ribbon count, Pokerus source/decision, PID constraint inputs/outputs, and return merge decisions. Add failing tests before behavior changes. Partially complete: Pokerus projection/import/merge boundary tests and existing ribbon/PID/nickname tests now cover the highest-risk boundaries.
- Why it matters: transfer bugs are hard to see because raw bytes can look valid while canonical state was silently skipped.
- Risk level: low.
- Suggested tests: Gen 5 -> Gen 3 -> Gen 5 preserves future-only ribbon plus newly earned Gen 3 ribbon; Gen 3 -> Gen 5 -> Gen 3 preserves Pokerus byte; PID/nature/shiny/gender/form/ability slot all validate after projection; default nickname flag survives Gen 3 round trip; returning false ribbon cannot clear true ribbon.

### Phase 2 - Canonical data model hardening

- Files likely involved: [ResortTypes.hpp](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/resort/domain/ResortTypes.hpp), persistence migrations, [BridgeImportAdapter.cpp](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/resort/integration/BridgeImportAdapter.cpp), [BridgeImport.cs](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tools/pkhex_bridge/BridgeImport.cs), storage docs.
- Exact behavior to change: promote canonical `nature/stat_nature`, IVs, EVs, contest stats, Pokerus detail, ribbon/mark catalog, friendship triplets, memory fields, markings, fateful flag, and generation-specific side data from loose warm JSON into typed or schema-versioned canonical structures.
- Why it matters: projection should not depend on whichever raw snapshot happens to contain a field.
- Risk level: medium-high because storage migrations and backfill are involved.
- Suggested tests: import existing saves and assert typed fields match bridge warm catalog; old DB rows without typed fields still project from warm/raw fallback; no raw snapshot data is discarded.

### Phase 3 - Projection hardening

- Files likely involved: [MirrorProjectionService.cpp](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/resort/services/MirrorProjectionService.cpp), [BridgeProject.cs](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tools/pkhex_bridge/BridgeProject.cs), [BridgeProjectPastProjection.cs](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tools/pkhex_bridge/BridgeProjectPastProjection.cs), [BridgeProjectReconcile.cs](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tools/pkhex_bridge/BridgeProjectReconcile.cs).
- Exact behavior to change: send typed canonical ribbons, Pokerus, contest stats, friendship, nature/stat nature, PID constraints, fateful flag, and nickname/default-state into the bridge. Filter only fields unsupported by target generation. Never mutate canonical state from projection output except for mirror session transport aliases and explicit export snapshots.
- Why it matters: bridge projection should be a pure view of canonical Resort state, like OpenHome's `fromOhpkm`.
- Risk level: medium.
- Suggested tests: bridge projection replays all compatible ribbons; unsupported ribbons remain in canonical but absent in PK3; Pokerus strain/days survives PK5 -> PK3 projection; contest stats survive PK3 projection; Gen 3 PID solves nature/shiny/gender/form/ability together.

### Phase 4 - Return merge hardening

- Files likely involved: [PokemonMergeFieldPolicy.hpp](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/resort/domain/PokemonMergeFieldPolicy.hpp), [PokemonMergeFieldPolicy.cpp](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/resort/domain/PokemonMergeFieldPolicy.cpp), [PokemonMergeService.cpp](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/resort/services/PokemonMergeService.cpp), [ResortRibbonCatalogMerge.cpp](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/resort/domain/ResortRibbonCatalogMerge.cpp).
- Exact behavior to change: add typed merge rules for Pokerus, contest stats, friendship, memories, markings, ribbons/marks, and handler data. Preserve static fields. Merge gain-only fields by union/max. Allow legitimate mutable updates when source trainer/handler identity matches. Partially complete: Pokerus warm-catalog merge now uses non-zero evidence and monotonic days; broader typed policies are still planned.
- Why it matters: current mirror merge protects too much in a few areas, especially Pokerus and friendship, because the data is not typed enough to distinguish meaningful updates from projection loss.
- Risk level: medium.
- Suggested tests: incoming non-zero Pokerus updates canonical; incoming zero Pokerus does not clear canonical; incoming contest ribbon tier maxes existing tier; handler memory updates only for matching handler; static warm fields remain untouched.

### Phase 5 - Regression suite

- Files likely involved: [tests/native/resort/storage_import_tests.cpp](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tests/native/resort/storage_import_tests.cpp), [tests/native/resort/mutable_merge_policy_tests.cpp](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tests/native/resort/mutable_merge_policy_tests.cpp), [BridgeProjectConversionTests.cs](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tests/unit/pkhex_bridge/PKHeXBridge.UnitTests/BridgeProjectConversionTests.cs), [PKHeXBridge.IntegrationTests](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tests/integration/pkhex_bridge/PKHeXBridge.IntegrationTests).
- Exact behavior to change: add full round-trip fixtures for Gen 5 -> Gen 3 -> Gen 5, Gen 3 -> Gen 5 -> Gen 3, ribbons earned in old generation, Pokerus preserved and gained, PID/nature/shiny/form stability, nickname stability, and active mirror identity resolution.
- Why it matters: OpenHome's byte round-trip tests catch whole-class regressions; Resort needs equivalent fixture-driven tests around bridge/raw/canonical boundaries.
- Risk level: low-medium, mostly fixture maintenance.
- Suggested tests: use real sample saves in `saves/` plus synthetic bridge unit payloads where save editing is too expensive.

## Risks and Things Not to Copy

Do not copy OpenHome source. Use the design lesson: canonical record plus target-specific projection plus merge policy. Also do not assume OpenHome's exact field choices or legacy conversion behavior are legally or functionally correct for Resort's PKHeX-based bridge.

Avoid turning warm JSON into an unbounded shadow database. If a field affects projection or merge policy, give it a typed home or a strict schema. Avoid broad reflection-only ribbon/Pokerus behavior without tests for real PKHeX formats. Avoid treating a projected older-generation PKM as a canonical checkpoint.

## Credits and License Note

OpenHome should be credited as an architectural/reference influence for the canonical Pokemon representation, target-format projection model, generated backwards-compatible identity keys, gain-only ribbon merge concept, and round-trip test strategy. This report is based on reading OpenHome as reference material only; proposed Pokemon Resort changes should be independently implemented in Resort's own style and under Resort's licensing.
