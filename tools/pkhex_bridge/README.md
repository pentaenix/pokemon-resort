# PKHeX Bridge

This helper is the integration boundary between the native SDL2 app and `PKHeX.Core`.

The canonical bridge contract lives in [`../pokemon-resort/docs/PKHEX_BRIDGE.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/PKHEX_BRIDGE.md). Keep this README as a short local build/run guide.

## Purpose

- keep PKHeX usage in a .NET process
- expose a tiny CLI contract the native app can call
- avoid pretending `PKHeX.Core` is a native C++ library

## CLI contract

The helper writes one JSON object to stdout. It has three operations:

```bash
PKHeXBridge <save-path>
PKHeXBridge import <save-path>
PKHeXBridge write-projection <save-path> <projection-json-path>
```

Probe/preview output includes top-level legacy transfer-summary fields so the native ticket UI can keep working while newer screens move to the richer models:

- `bridge_probe_schema`: probe JSON schema version expected by native transfer UI
- `success`: whether the save could be parsed
- `game_id`, `player_name`, `play_time`, `pokedex_count`, `badges`: ticket-summary data
- `party`: species slugs for party Pokemon
- `box_1`: species slugs for the first box using the save file's real slot count, with empty strings for empty slots

The expanded reader fields are the preferred foundation for new UI and future editing support:

- `trainer`: game identity, save type, generation, trainer IDs, money, play time, badges, and checksum state
- `pokedex`: game-specific seen/caught progress and per-species entries exposed by PKHeX
- `all_pokemon`: all present party and boxed Pokemon with location metadata
- `boxes`: every box, every slot, lock/overwrite metadata, and each present Pokemon's location
- `bag`: bag pockets and item stacks exposed by PKHeX inventory pouches

Box and bag data intentionally preserve generation-specific shape. For example, older games may have 20-slot boxes while later games normally have 30-slot boxes.

Import-grade output is emitted by `import <save-path>`. It includes `bridge_import_schema: 1` and one entry per present Pokemon with exact raw payload bytes, SHA-256 hash, format name, source game, hot fields, warm JSON, and suspended JSON.

`write-projection <save-path> <projection-json-path>` validates the save, writes immutable `.initbak` and rolling `.bak` copies under `<projection-dir>/transfer_write_backups/` (not beside the `.sav`), then applies `projection_schema` **1** (box names) and/or **2** (full PC box slot snapshot using import-grade encrypted PKM payloads). See [`PKHEX_BRIDGE.md`](../pokemon-resort/docs/PKHEX_BRIDGE.md).

### Write-back modules (`WriteBack/`)

Orchestration stays in `BridgeWriteBack.cs` (keep under ~500 lines). Domain-specific pieces live next to it:

- `WriteBack/SaveFileAtomicWriter.cs` — durable temp write, byte-identical verify, atomic replace, revert-on-failure
- `WriteBack/TransferWriteBackupPaths.cs` — backup directory + stable hash filenames
- `WriteBack/PcBoxProjectionApplier.cs` — PC box slots (`projection_schema` 2)
- `WriteBack/BoxNameProjectionApplier.cs` — box names
- `WriteBack/WriteBackInputValidation.cs` — path/input bounds

Add future sections (items, bag, …) as new `*ProjectionApplier.cs` files and call them from `BridgeWriteBack.WriteProjection`.

## Build on macOS

Install the .NET SDK first, then run:

```bash
cd /Users/vanta/Desktop/title_screen_demo/pokemon-resort/tools/pkhex_bridge
dotnet restore
dotnet build
```

The current bridge targets `.NET 10` so it can run on a machine with the .NET 10 SDK/runtime while still consuming the current `PKHeX.Core` package.

## Publish for shipping

For a standalone macOS helper:

```bash
/bin/zsh /Users/vanta/Desktop/title_screen_demo/pokemon-resort/tools/pkhex_bridge/publish-macos.sh
```

This publishes a self-contained executable to:

```text
/Users/vanta/Desktop/title_screen_demo/pokemon-resort/tools/pkhex_bridge/publish/osx-arm64/
```

That published executable is the preferred artifact to bundle with the native app.

## Run manually

```bash
dotnet run --project /Users/vanta/Desktop/title_screen_demo/pokemon-resort/tools/pkhex_bridge/PKHeXBridge.csproj -- "/absolute/path/to/save.sav"
```

Import-grade read:

```bash
dotnet run --project /Users/vanta/Desktop/title_screen_demo/pokemon-resort/tools/pkhex_bridge/PKHeXBridge.csproj -- import "/absolute/path/to/save.sav"
```

Guarded write-back validation:

```bash
dotnet run --project /Users/vanta/Desktop/title_screen_demo/pokemon-resort/tools/pkhex_bridge/PKHeXBridge.csproj -- write-projection "/absolute/path/to/save.sav" "/absolute/path/to/projection.json"
```

Example success output:

```json
{"bridge_probe_schema":5,"success":true,"game_id":"pokemon_ruby","player_name":"BRENDAN","party":["swampert"],"box_1":["zangoose","",""],"play_time":"12:34","pokedex_count":42,"badges":8,"trainer":{"Name":"BRENDAN","Generation":3},"pokedex":{"Supported":true},"all_pokemon":[],"boxes":[{"Index":0,"SlotCount":30,"Slots":[]}],"bag":{"Supported":true,"Pockets":[]},"status":"ok","saveType":"SAV3RS","game":"R","trainerName":"BRENDAN"}
```

Example failure output:

```json
{"bridge_probe_schema":5,"success":false,"error":"unsupported_save","details":"/absolute/path/to/save.sav"}
```

The process exits with code `0` on success and non-zero on failure.
