# Export, Projection, And Write-Back Flow

## Native Projection

Use `PokemonResortService::exportPokemon(pkrid, context)`.

Export:

1. loads canonical `ResortPokemon`
2. builds a backend projection payload
3. writes an `ExportProjection` snapshot
4. opens an active mirror session
5. writes `Exported` and `MirrorOpened` history
6. returns `ExportResult` with `snapshot_id`, `mirror_session_id`, format, hash, and raw payload bytes

Export does not mutate canonical Pokemon fields. It only records projection/audit state and opens a mirror.

## Gen 1/2 Managed Beacon

Set `ExportContext::use_gen12_beacon = true` for managed Gen 1/2 helper projections. The backend records:

- beacon TID
- beacon OT
- sent species/form/lineage/level/exp anchors
- original OT/TID/SID/game restoration fields

The beacon is a helper for managed mirror rediscovery. It is not the canonical identity model.

## Current Projection Payload

The native projection payload is currently a synthetic JSON payload with `projection_schema: 1`.

This is intentional. It lets backend/frontend integration test the full lifecycle without pretending PKM conversion is implemented natively.

Real target-game PKM conversion should be added behind the bridge/integration layer, not in UI code and not in `App.cpp`.

## Bridge Write-Back Boundary

The bridge has a guarded command:

```bash
dotnet /Users/vanta/Desktop/title_screen_demo/tools/pkhex_bridge/bin/Debug/net10.0/PKHeXBridge.dll write-projection "/absolute/path/to/save.sav" "/absolute/path/to/projection.json"
```

It currently validates:

- save file exists and loads through PKHeX
- projection JSON exists
- `projection_schema` is supported

It then returns `write_back_not_implemented` instead of mutating the save. This is deliberate until PKM conversion and safe slot write-back rules are implemented and tested per format.

Do not add unsafe in-place save mutation around this boundary.
