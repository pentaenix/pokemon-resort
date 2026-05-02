# Backend API Reference

This page lists the backend methods that already exist. Check here before adding another service, repository method, bridge command, or test utility.

## Facade: `PokemonResortService`

Header: [`include/resort/services/PokemonResortService.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/resort/services/PokemonResortService.hpp)

- `PokemonResortService(profile_path)`
  Opens a SQLite Resort profile, runs migrations, constructs repositories/services, and keeps UI code away from SQL.
- Path helpers live in [`include/core/SavePaths.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/core/SavePaths.hpp): `resortProfileDatabasePath(save_directory, persistence)` / `(save_directory, file_name)` and `defaultResortProfilePath(save_directory)`. The player build uses `persistence` from [`title_screen.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/title_screen.json) so the Resort DB sits next to `pokemon_resort.sav`.
- `ensureProfile(profile_id)`
  Idempotently ensures default Resort boxes and empty slots exist for a profile (`INSERT OR IGNORE`). Current defaults are 60 boxes × 30 slots (`BoxRepository::kDefaultResortPcBoxCount`), aligned with transfer UI `game_transfer.json` `resort_pc_box_count`.
- `importParsedPokemon(imported, context)`
  Imports one already parsed import-grade Pokemon. Validates raw bytes/hash, matches active mirrors first, then stable identifiers, writes an imported snapshot, creates or merges canonical state, writes history, optionally places in a slot, and commits atomically.
- `exportPokemon(pkrid, context)`
  Creates a backend projection snapshot, opens a mirror session, writes export/mirror history, and returns projection bytes plus `snapshot_id` and `mirror_session_id`. It does not mutate canonical Pokemon fields. Current implementation always opens a mirror; `ExportContext::managed_mirror` is serialized in projection metadata and is not a switch for disabling mirror creation.
- `getPokemonById(pkrid)`
  Loads the full canonical `ResortPokemon`.
- `pokemonExists(pkrid)`
  Checks canonical row existence.
- `getBoxSlotViews(profile_id, box_id)`
  Returns lightweight `PokemonSlotView` rows for occupied slots in slot order.
- `getPokemonLocation(profile_id, pkrid)`
  Returns the current Resort `BoxLocation` for a Pokemon if placed.
- `openMirrorSession(pkrid, target_game, context)`
  Opens a managed mirror and writes history. Use `exportPokemon` for normal export because it also snapshots the projection.
- `getMirrorSession(mirror_session_id)`
  Loads a mirror session by ID.
- `getActiveMirrorForPokemon(pkrid)`
  Loads the newest active mirror for a canonical Pokemon.
- `closeMirrorSessionReturned(mirror_session_id)`
  Closes a mirror as `Returned` and writes history. Return imports normally close mirrors through `importParsedPokemon`.

## Import Services

Headers:

- [`include/resort/services/BridgeImportService.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/resort/services/BridgeImportService.hpp)
- [`include/resort/integration/BridgeImportAdapter.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/resort/integration/BridgeImportAdapter.hpp)
- [`include/resort/services/PokemonImportService.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/resort/services/PokemonImportService.hpp)

- `BridgeImportService::importSave(save_path, options)`
  Calls the PKHeX bridge `import` command, parses import-grade JSON, and imports Pokemon sequentially into Resort slots.
- `parseBridgeImportPayload(json_text)`
  Parses `bridge_import_schema: 1` JSON into `ImportedPokemon`. Rejects transfer-ticket summaries because canonical imports require raw bytes and hashes.
- `PokemonImportService::importParsedPokemon(imported, context)`
  Lower-level import orchestration used by the facade.

Current snapshot kind note: return imports that match an active mirror are persisted with `SnapshotKind::ImportedRaw`; the `SnapshotKind::ReturnRaw` enum exists but is not emitted yet.

## Matching And Merge

Headers:

- [`include/resort/services/PokemonMatcher.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/resort/services/PokemonMatcher.hpp)
- [`include/resort/services/PokemonMergeService.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/resort/services/PokemonMergeService.hpp)

- `PokemonMatcher::findBestMatch(imported)`
  Match order is active beacon mirror, `home_tracker`, `pid + encryption_constant + TID/SID + OT`, then `pid + TID/SID + OT`. Native `pk1`/`pk2` without a managed mirror intentionally returns no exact match.
- `PokemonMergeService::mergeImported(canonical, imported, updated_at_unix)`
  Updates mutable hot fields, preserves immutable identity/timestamps, replaces optional fields only when incoming data provides them, deep-merges warm/cold JSON, unions arrays, increments revision, and returns a history diff JSON.

## Export And Mirror Services

Headers:

- [`include/resort/services/PokemonExportService.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/resort/services/PokemonExportService.hpp)
- [`include/resort/services/MirrorSessionService.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/resort/services/MirrorSessionService.hpp)

- `PokemonExportService::exportPokemon(pkrid, context)`
  Lower-level export orchestration used by the facade.
- `MirrorSessionService::openMirrorSession(pkrid, target_game, context)`
  Persists sent-state anchors and optional Gen 1/2 beacon helpers.
- `MirrorSessionService::getById(mirror_session_id)`
  Loads a mirror by ID.
- `MirrorSessionService::getActiveForPokemon(pkrid)`
  Loads the newest active mirror for a Pokemon.
- `MirrorSessionService::findActiveByBeacon(target_game, tid16, ot_name)`
  Used by matcher for managed return imports.
- `MirrorSessionService::closeReturned(mirror_session_id, returned_at_unix)`
  Marks a mirror returned and writes audit history.

## Repositories

Repositories own SQL only. Do not put identity, merge, projection, or UI logic here.

- `PokemonRepository`
  `insert`, `updateAfterMerge`, `exists`, `findById`, `findByHomeTracker`, `findByPidEcTidSidOt`, `findByPidTidSidOt`.
- `BoxRepository`
  `ensureDefaultBoxes`, `placePokemon`, `removePokemon`, `findPokemonLocation`, `getBoxSlotViews`.
- `SnapshotRepository`
  `insert`.
- `HistoryRepository`
  `insert`.
- `MirrorSessionRepository`
  `insert`, `update`, `findById`, `findActiveForPokemon`, `findActiveByBeacon`.

## Bridge Commands

Implemented in [`tools/pkhex_bridge`](/Users/vanta/Desktop/title_screen_demo/tools/pkhex_bridge):

- `PKHeXBridge <save-path>`
  Probe/preview command. Emits `bridge_probe_schema: 5` plus legacy ticket fields.
- `PKHeXBridge import <save-path>`
  Import-grade read. Emits `bridge_import_schema: 1`, including exact per-Pokemon raw payload bytes and SHA-256 hashes.
- `PKHeXBridge write-projection <save-path> <projection-json-path>`
  Applies a projection JSON to the save (after snapshots under `<projection-dir>/transfer_write_backups/`). Supports `projection_schema` 1 (box names) and 2 (full PC snapshot + names). See [`PKHEX_BRIDGE.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/PKHEX_BRIDGE.md).

Native process launcher methods are in [`include/core/SaveBridgeClient.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/core/SaveBridgeClient.hpp):

- `probeSaveWithBridge`
- `importSaveWithBridge`
- `writeProjectionWithBridge`

## Backend Tool

Executable target: `resort_backend_tool`

Source: [`tools/resort_backend_tool.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tools/resort_backend_tool.cpp)

- `resort_backend_tool seed --db <profile.resort.db> --species <id> [options]`
  Creates import-grade native input and imports through normal Resort policy.
- `resort_backend_tool export --db <profile.resort.db> --pkrid <pkrid> --target-game <id> [options]`
  Runs normal export projection and can write projection bytes to `--out`.
- `resort_backend_tool recover --db <profile.resort.db> --pkrid <pkrid> [--profile default]`
  Emergency recovery for an existing canonical Pokemon. Places it in the first available Resort slot using reject-if-occupied placement and closes any stale active mirror.
- `resort_backend_tool reset --db <profile.resort.db> [--profile default] [--backup <path>] --confirm RESET`
  **DANGEROUS**: wipes the Resort profile to an empty state. Deletes all canonical Pokémon (cascading snapshots/history/mirrors) and clears `box_slots`. Requires explicit `--confirm RESET`. Use `--backup` to copy the DB file before modifying it.

## Not Implemented Yet

- Real PKM conversion from canonical Resort data.
- Real safe save slot write-back.
- Native Gen 1/2 ambiguous identity heuristic beyond managed beacon returns.
- UI integration with canonical Resort storage.
