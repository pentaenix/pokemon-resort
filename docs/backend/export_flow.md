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

Current implementation always opens a mirror session for export. `ExportContext::managed_mirror` is currently metadata in the projection payload, not a way to disable mirror creation.

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

The bridge exposes a **guarded** write command:

```bash
dotnet /Users/vanta/Desktop/title_screen_demo/tools/pkhex_bridge/bin/Debug/net10.0/PKHeXBridge.dll write-projection "/absolute/path/to/save.sav" "/absolute/path/to/projection.json"
```

What it does today:

- Validates paths and projection size caps (see [`PKHEX_BRIDGE.md`](../PKHEX_BRIDGE.md)).
- Loads the save with PKHeX and writes immutable `.initbak` plus a rolling `.bak` under `<projection-dir>/transfer_write_backups/`.
- Applies supported projection domains:
  - `projection_schema` **1** — PC box names only.
  - `projection_schema` **2** — full PC snapshot: `box_names` plus `pc_boxes` with import-grade **encrypted** PKM payloads per slot (`raw_payload_base64` + `raw_hash_sha256`), validated before `SetBoxSlotAtIndex`.
- On success, atomically replaces the live `.sav` after byte-for-byte verification; on failure it may restore from the rolling backup.

What is still **not** the full product story:

- Party, bag, and other save regions are not in the projection schema yet.
- **Cross-generation PKM bytes** for a Resort-driven send are produced by the separate bridge `project` command (PKHeX `EntityConverter`), not by hoping an old cached `.pk` stays valid — see [`MIRROR_PROJECTION_ARCHITECTURE.md`](../transfer_system/MIRROR_PROJECTION_ARCHITECTURE.md).

Do not add unsafe in-place save mutation around this boundary; extend `WriteBack/` appliers and schema versions instead of ad hoc edits.
