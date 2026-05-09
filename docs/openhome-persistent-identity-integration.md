# OpenHome Persistent Identity Integration

## Observed

- OpenHome's durable Pokemon model is `OHPKM` in `pokemon-resort/tools/OpenHome/src/core/pkm/OHPKM.ts`. It extends the Rust/WASM `OhpkmV2` type and can be constructed from either serialized bytes or a game-format Pokemon.
- OpenHome's stable home key is `openhomeId`. The UI store persists `Record<string, OHPKM>` keyed by that value.
- `getHomeIdentifier` builds an identifier from base species, TID/SID, personality value, and origin game.
- `getMonGen345Identifier` computes a Gen 3/4/5-compatible identifier and, for `OHPKM`, uses `generatePersonalityValuePreservingAttributes` before lookup.
- `getMonGen12Identifier` uses base species, trainer constraints, and DVs, with special handling when an `OHPKM` did not originate in Game Boy saves.
- `useOhpkmStore.loadIfTracked` recognizes returning Pokemon through generation-specific lookup tables first, then loads the existing `OHPKM`.
- `useOhpkmStore.startTrackingNewMon` creates an `OHPKM`, records the tracking timestamp, updates lookup tables for destination saves, and inserts it into the store.
- `useOhpkmStore.updateAndConvertForSave` stores the `OHPKM`, updates destination lookup keys, then delegates projection to `save.convertOhpkm`.
- `OHPKM.fromMonInSave` creates a canonical `OHPKM` and immediately calls `syncWithGameData`.
- `OHPKM.syncWithGameData` merges mutable gameplay data back into the canonical object. It updates EXP, moves, evolution/form changes, held item, some ability changes, EVs/AVs, hyper training, contest data, markings, ribbons by union, Pokerus only when incoming is non-zero, recent save, trainer/handler friendship and memories, geography, stat nature, and modern side data.
- Save classes expose `convertOhpkm(ohpkm, strategy)`, and target PK classes implement `fromOhpkm`, so game files remain projections of `OHPKM`.
- OpenHome stores opaque `.ohpkm` fixtures under `pokemon-resort/tools/OpenHome/test-files/pkm-files/ohpkm/`.
- OpenHome's UI movement orchestration lives in `pokemon-resort/tools/OpenHome/src/ui/state/saves/useSaves.ts` and `pokemon-resort/tools/OpenHome/src/ui/state/saves/SavesProvider.tsx`.
- When OpenHome opens a save, `addSave` scans save Pokemon, calls `ohpkmStore.loadIfTracked(mon)`, and calls `trackedData.syncWithGameData(mon, save)` for tracked Pokemon before updating the OHPKM store.
- Game-to-Home movement calls `ohpkmStore.loadIfTracked(mon) ?? ohpkmStore.startTrackingNewMon(mon, sourceSave, undefined)`, stores `ohpkm.openhomeId` in the Home bank/box slot, clears the source save slot, and saves through OpenHome's writer path.
- Home-to-Game movement loads the OHPKM by `openhomeId`, calls `ohpkmStore.updateAndConvertForSave(ohpkm, save)`, writes the converted Pokemon to the destination save slot, and saves through OpenHome's writer path.
- Game-to-Game movement creates or loads the OHPKM from the source save, converts that OHPKM for the destination save, clears the source slot, writes the destination slot, and updates the OHPKM store.
- Before writing saves, OpenHome calls `trackedData.tradeToSave(save)`, converts through `save.convertOhpkm`, calls `save.prepareWriter()`, and writes every changed save through the backend. This means OpenHome, not PKHeX, is the correct save writer for OpenHome-owned movement.
- Resort now has a headless OpenHome bridge at `pokemon-resort/tools/OpenHome/src/cli/resortBridge.ts`, built to `pokemon-resort/tools/OpenHome/dist/resort-bridge/resortBridge.mjs`.
- The bridge has been exercised on copied test saves: Emerald box 0 slot 0 -> OpenHome `.ohpkm` storage -> Black box 0 slot 0 -> back to OpenHome storage with the same `openhomeId`.
- Resort already keeps PKHeX behind `tools/pkhex_bridge` and already has canonical rows, snapshots, mirror sessions, PID transport mappings, and box unplacement for active mirrors.

## Inferred

- `OHPKM`/`OhpkmV2` is the closest OpenHome equivalent to the desired persistent Pokemon payload.
- `openhomeId` is the practical identity key Resort should use for new Resort/OpenHome storage.
- Resort should not directly link OpenHome TypeScript/WASM into the native app in the first migration. Opaque `.ohpkm` bytes plus `openhomeId` are a safer boundary until a dedicated tool/bridge exists.
- OpenHome replaces large parts of Resort's fragile identity/projection/return model, but Resort still needs a durable local record key:
  - **Keep `pkrid`** as the Resort record identity for Resort-owned data (memories, accessories, travel history, jobs, friendship, UI/cache fields).
  - **Use `openhomeId`** as the canonical Pokémon payload + cross-save movement identity (OHPKM storage and conversions).
- Resort's existing active mirror unplacement is already the correct first implementation of `AWAY_IN_GAME` visibility: normal box queries read `box_slots`, while `pokemon` and `mirror_sessions` keep internal lookup alive.
- PKHeX may remain useful for read-only inspection during the transition, but PKHeX save writing becomes redundant for the OpenHome movement path once Resort can invoke OpenHome headlessly. Mixing PKHeX write-back with OpenHome movement would risk bypassing `OHPKM.syncWithGameData`, lookup-map updates, handler updates, and `save.convertOhpkm`.

## Unknown

- The exact stable binary/version compatibility contract for `OhpkmV2` bytes across OpenHome releases.
- Whether OpenHome exposes an upstream-supported headless CLI/API that Resort can call without maintaining a local bridge.
- The best place to persist OpenHome payload bytes in Resort's SQLite schema: a dedicated table, a payload column, or a versioned canonical-payload table.
- How OpenHome licensing should be represented if Resort ships code or binary artifacts derived from the local reference copy.
- How to reconcile OpenHome's `openhomeId` with Resort's existing `origin_fingerprint`, PID transport registry, and mirror session IDs during partial migration.

## Blocked

- Direct native use of OpenHome types is still blocked by the TS/Rust/WASM packaging boundary; Resort should call the headless bridge process for now.
- Full in-app transfer correctness tests are blocked until Resort UI/backend actions are routed to the headless OpenHome bridge.
- Persisting OpenHome payloads in Resort is now supported via a dedicated DB table keyed by `pkrid` + linked `openhomeId`. The canonical byte source remains OpenHome storage (`mons_v2/<openhomeId>.ohpkm`); Resort stores a copy for audit/repair and to keep stable links across UI sessions.

## Recommended

- Keep the current PKHeX bridge for legacy read/import paths and existing projection paths while adding OpenHome as the persistent canonical payload. Do not use PKHeX as the writer for the new OpenHome movement path once the OpenHome bridge can run.
- Store `openhomeId + serialized OHPKM bytes + Resort metadata + presence` as the next durable Resort record shape.
- Mirror OpenHome's home box model: boxes contain OpenHome IDs, while the OHPKM byte store is keyed by the same OpenHome IDs.
- Add a dedicated OpenHome adapter/bridge that can perform:
  - open a save with OpenHome save classes,
  - create/start tracking from a Pokemon slot using `startTrackingNewMon`,
  - recognize returning Pokemon,
  - sync returned game data into `OHPKM`,
  - convert/project `OHPKM` for a target save,
  - write changed saves through `prepareWriter`.
- Keep `AWAY_IN_GAME` Pokemon out of normal box view queries. Keep them queryable by `openhomeId` and active mirror/session services.
- Do not promote returned older-generation projection bytes to canonical truth. Let OpenHome merge mutable data into OHPKM and let Resort keep return raw as evidence.
- Next migration should route one narrow import/export/return test through an OpenHome command-line adapter. Resort's new bridge should speak `openhomeId`, OHPKM bytes, banks, boxes, and generation lookup maps rather than PKR IDs.

## Product Identity Boundary

The OpenHome migration keeps two identifiers with deliberately separate jobs:

- `pkrid` is the local Resort record id. Resort-owned features such as box placement, memories, accessories, travel history, jobs, resort friendship, and UI/cache data should key off `pkrid`.
- `openhomeId` is the canonical Pokemon payload/movement id. It identifies the OHPKM bytes that OpenHome uses for generation conversion, save writes, and `syncWithGameData` updates when a tracked Pokemon levels up, evolves, learns moves, gains ribbons, or otherwise changes in a game.

`pkrid` should own or reference exactly one current `openhomeId` after migration. New Resort gameplay systems should not read or mutate OHPKM internals directly; if a Resort feature needs to affect exported game data, it should request an explicit OpenHome operation. Conversely, OpenHome should not own Resort gameplay memories or UI layout. This boundary prevents the old mirror/snapshot model from being recreated under new names.

The intended replacement for mirrors is placement state:

- `RESORT_BOX`: the `pkrid` is visible in Resort storage.
- `IN_GAME_SAVE`: the OHPKM is currently projected into a specific save/box/slot.
- `HOME_BANK`: the OHPKM is stored in OpenHome bank storage.

OpenHome updates the OHPKM payload on save open or movement by loading tracked Pokemon, calling `syncWithGameData(mon, save)`, and writing the updated OHPKM bytes. Resort can then append travel/history events by `pkrid` without becoming the canonical Pokemon byte store.

## Recommended Resort Bridge Contract

- `observed`: OpenHome UI treats Home boxes as OpenHome ID placements and `.ohpkm` files as the durable Pokemon records.
- `recommended`: Resort's native bridge boundary should call an OpenHome headless adapter with operations equivalent to `syncOpenSave`, `pullPokemonToHome`, `pushPokemonToGame`, and `movePokemonBetweenGames`.
- `recommended`: `pullPokemonToHome` should load the source save, sync any already-tracked Pokemon, load-or-start tracking the source slot, clear the source slot, upsert `.ohpkm`, place `openhomeId` into `banks.json`, then write the source save through OpenHome.
- `recommended`: `pushPokemonToGame` should load the target save, load OHPKM by `openhomeId`, call OpenHome's conversion/update path for that save, write the target slot, upsert updated OHPKM, then write the target save through OpenHome.
- `recommended`: `movePokemonBetweenGames` should create or load OHPKM from the source save and project that OHPKM into the destination save. The `.pk3`, `.pk4`, `.pk5`, etc. projection must remain temporary save data, not Resort's canonical record.
- `observed`: The executable OpenHome adapter now exists and can perform copied-save movement tests. The next integration step is invoking it from Resort's move/import actions.
