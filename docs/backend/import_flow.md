# Import Flow

## External Save Preview

Use the PKHeX bridge probe command to see what is in an external save for preview:

```bash
dotnet /Users/vanta/Desktop/title_screen_demo/pkr-tools/pkhex_bridge/bin/Debug/net10.0/PKHeXBridge.dll "/absolute/path/to/save.sav"
```

This emits `bridge_probe_schema: 5` JSON with trainer, Pokedex, bag, boxes, and preview Pokemon models. These fields are useful for UI display, but they are not canonical Resort data.

## Import-Grade Save Read

Use the bridge import operation for real import:

```bash
dotnet /Users/vanta/Desktop/title_screen_demo/pkr-tools/pkhex_bridge/bin/Debug/net10.0/PKHeXBridge.dll import "/absolute/path/to/save.sav"
```

The output has `bridge_import_schema: 1` and a `pokemon` array. Each Pokemon includes:

- `source_game`
- `format_name`
- `source_location`
- `raw_payload_base64`
- `raw_hash_sha256`
- `hot`
- `warm_json`
- `suspended_json`

Native code consumes this through `BridgeImportAdapter` and `BridgeImportService`; UI code should not parse import-grade JSON directly.

## Native Import

`PokemonResortService::importParsedPokemon(imported, context)` is the service-level entry point.

The transaction order is:

1. validate import-grade raw bytes and hash
2. match against active mirror sessions
3. match against stable identifiers
4. write raw snapshot first
5. create canonical Pokemon or merge into the matched one
6. write history
7. optionally place in a Resort box slot
8. commit

If placement or merge fails, the snapshot, canonical update, mirror close, history, and placement roll back together.

Snapshot kinds:

- First-class external imports use `SnapshotKind::ImportedRaw`.
- Imports that resolve an active managed mirror, a PID transport mapping, or a cross-generation stable identity use `SnapshotKind::ReturnRaw` for the incoming evidence row.
- First-time/full canonical imports write a companion `SnapshotKind::CanonicalCheckpoint` row. Mirror returns do not: returned `.pk*` bytes are evidence from a projection, and must not become the canonical binary base.

## Return Imports

Managed return imports are matched before generic identity. The current supported managed path is beacon-based:

- export opens an active `mirror_sessions` row
- Gen 1/2 managed projections can carry a beacon TID/OT helper
- Gen 3+ projections can carry a temporary PID recorded in `pid_transport_registry`
- returning import with matching target game, beacon TID, and beacon OT resolves to the mirror's `pkrid`
- returning import with a known temporary PID resolves to the mirror's `pkrid` and restores canonical PID/OT/TID/SID/origin fields
- progression anchors reject incompatible returns
- successful return merge closes the mirror as `Returned`

Native Gen 1/2-origin heuristic matching remains conservative. Without a managed mirror, `pk1`/`pk2` imports do not silently claim exact identity.

Mirror-return merge policy copies only allowed mutable gameplay fields. Static identity and provenance fields such as OT, TID/SID, canonical/original PID, language, origin game, met data, ball, shiny, and gender remain Resort-owned unless a later explicit conflict-review flow authorizes a change.

Current mirror-return mutable rules:

- Level, EXP, HP/status, held item, and friendship/progression-style warm data can update.
- Species/form can update when the mirror evolved.
- Move slots and PP come from the returning cart so target-generation move loss or player replacements persist.
- Moves that disappeared during a projection/return are recorded under `warm_json.resort_catalog.auto_removed_moves` for a future reteach mechanic.
- Nickname state is canonical data: Resort stores both nickname text and `is_nicknamed`. Cross-generation mirror returns preserve that canonical state. Same-origin returns may update nickname state only when the target format has an explicit nickname flag, or when an older-format return is inferred by PKHeX to contain a custom nickname. Gen 3 default species-name bytes such as `PIKACHU` are projection bytes, not canonical custom nicknames.
