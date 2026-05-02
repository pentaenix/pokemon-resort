# Frontend Integration Guide

This guide is for UI that reads or mutates canonical Resort storage. The current transfer system screen still uses parsed external-save models plus temporary in-memory slot arrays for Pokemon/item movement. Replacing that prototype state with backend-backed Resort storage is deferred and should be done as an explicit integration project, not as incidental UI cleanup.

## External Save UI

For "what is in this external game save?" screens, use existing save discovery and bridge probe data. Reliable display fields include:

- trainer name and game ID
- box count and slot count
- slot occupied/empty state
- preview species/nickname/level where present
- bag and Pokedex display data where supported

This is preview/import-source data, not canonical Resort data.

Current transfer-ticket and transfer-system external game views are in this category. They should continue to use `SaveLibrary` / `TransferSaveSelection` data until a flow intentionally imports into or syncs with canonical Resort storage.

## Resort Box UI

Use:

- `PokemonResortService::getBoxSlotViews(profile_id, box_id)` for box rendering
- `PokemonResortService::getPokemonById(pkrid)` for full detail panels
- `PokemonResortService::getPokemonLocation(profile_id, pkrid)` for placement lookup

Render boxes from `PokemonSlotView`, not from snapshots, bridge DTOs, or transfer tickets.

When replacing the transfer screen's in-memory Resort-side slots, this is the target read model. Keep the transition one-way at first: render Resort boxes from service read models, then add explicit backend service calls for move/import/export operations instead of mutating UI arrays and hoping persistence catches up later.

## Import UI

The frontend should call a backend orchestration path equivalent to `BridgeImportService::importSave`.

UI should provide:

- save path selected by the user
- target Resort profile
- target placement start or explicit slot
- placement policy

UI must not parse bridge import JSON directly.

Existing method to reuse:

- `BridgeImportService::importSave(save_path, options)`
- `PokemonResortService::importParsedPokemon(imported, context)` only when the caller already has validated import-grade native data

## Export UI

The frontend should call `PokemonResortService::exportPokemon` or a higher-level backend wrapper.

Display the returned `mirror_session_id` if debugging, but do not treat it as Pokemon identity. Canonical identity remains `pkrid`.

Existing method to reuse:

- `PokemonResortService::exportPokemon(pkrid, context)`
- `PokemonResortService::getActiveMirrorForPokemon(pkrid)` for backend/debug state

## Do Not Depend On

- `box_1` legacy probe field
- sprite slugs as durable identity
- source save path as identity
- external game slot as identity
- warm/cold JSON keys directly in UI
- synthetic projection payload as a final PKM format

## Stable Enough For UI Now

- `pkrid`
- `PokemonSlotView`
- `BoxLocation`
- `ResortPokemon.hot`
- import result `created`/`merged`/`match_reason`
- export result `snapshot_id` and `mirror_session_id`

See [api_reference.md](api_reference.md) for the current method list before adding another frontend-facing backend wrapper.

Deferred UI-facing work includes richer conflict prompts, native Gen 1/2 ambiguous matching UX, real write-back completion, and advanced warm/cold detail rendering.

## Transition plan: transfer screen as Resort source of truth

The transfer system still mixes **bridge preview models** with **in-memory** game/resort slot arrays. Cross-generational travel requires the architecture in [`../transfer_system/MIRROR_PROJECTION_ARCHITECTURE.md`](../transfer_system/MIRROR_PROJECTION_ARCHITECTURE.md): Resort holds canonical state; the bridge `project` command produces target-generation PC payloads; write-back uses import-grade encrypted slots.

Concrete integration order:

1. **Read path** — Render Resort boxes from `PokemonResortService::getBoxSlotViews` / `getPokemonById` only; stop treating UI slot vectors as durable for the Resort column.
2. **Import path** — Route “drop into Resort” through `PokemonResortService::importParsedPokemon` (already the law for real imports); keep `BridgeImportService` for batch save imports.
3. **Export / send** — For a target game, load the latest checkpoint snapshot, call `MirrorProjectionService::projectLatestSnapshotToTarget` (wraps `projectPokemonWithBridge`), then stage `ExportResult.raw_payload` for `write-projection` or an explicit “send to game” handoff. Do not cache a static `.pk` file across trips; always re-project from the current checkpoint.
4. **Return** — On import, `ReturnRaw` + `CanonicalCheckpoint` rows and `MirrorReturnAnalysis` (pre-merge flags) feed future conflict UI; surface `SaveBridgeProjectResult` loss fields when sending (loss manifest from bridge `project` JSON).
5. **Player prompts** — Add lossy-projection and quarantine-review modals driven by bridge metadata, not hardcoded generation lists.

Until those steps land, the lower banner and box UI may show **preview** data for the external column and **read-model** data for Resort only where already wired.
