# Import Flow

## External Save Preview

Use the PKHeX bridge probe command to see what is in an external save for preview:

```bash
dotnet /Users/vanta/Desktop/title_screen_demo/tools/pkhex_bridge/bin/Debug/net10.0/PKHeXBridge.dll "/absolute/path/to/save.sav"
```

This emits `bridge_probe_schema: 2` JSON with trainer, Pokedex, bag, boxes, and preview Pokemon models. These fields are useful for UI display, but they are not canonical Resort data.

## Import-Grade Save Read

Use the bridge import operation for real import:

```bash
dotnet /Users/vanta/Desktop/title_screen_demo/tools/pkhex_bridge/bin/Debug/net10.0/PKHeXBridge.dll import "/absolute/path/to/save.sav"
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

## Return Imports

Managed return imports are matched before generic identity. The current supported managed path is beacon-based:

- export opens an active `mirror_sessions` row
- Gen 1/2 managed projections can carry a beacon TID/OT helper
- returning import with matching target game, beacon TID, and beacon OT resolves to the mirror's `pkrid`
- progression anchors reject incompatible returns
- successful return merge closes the mirror as `Returned`

Native Gen 1/2-origin heuristic matching remains conservative. Without a managed mirror, `pk1`/`pk2` imports do not silently claim exact identity.
