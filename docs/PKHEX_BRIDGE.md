# PKHeX Bridge Integration

This document describes how Pokemon Resort reads external Pokemon save files through `PKHeX.Core`.
It is the contributor-facing guide for the transfer save-reading layer, box visualizer work, bag visualizer work, and future save editing.

## Architecture Summary

Pokemon Resort does not link `PKHeX.Core` into the native C++ executable.
The integration is intentionally process-based:

1. Native C++ scans likely save files in [`saves`](/Users/vanta/Desktop/title_screen_demo/saves).
2. [`SaveLibrary.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/SaveLibrary.cpp) hashes candidates and checks the transfer probe cache.
3. [`SaveBridgeClient.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/SaveBridgeClient.cpp) launches the .NET helper under [`tools/pkhex_bridge`](/Users/vanta/Desktop/title_screen_demo/tools/pkhex_bridge).
4. The helper loads the save with `PKHeX.Core`.
5. The helper writes one JSON object to stdout.
6. Native C++ parses the subset it needs today and can be expanded to consume the richer model fields.

This keeps `PKHeX.Core` behind a small CLI boundary and prevents the native app from depending on .NET runtime types or PKHeX internals.

## Source Files

- [`tools/pkhex_bridge/PKHeXBridge.csproj`](/Users/vanta/Desktop/title_screen_demo/tools/pkhex_bridge/PKHeXBridge.csproj)
  owns the .NET target and `PKHeX.Core` package reference.
- [`tools/pkhex_bridge/Program.cs`](/Users/vanta/Desktop/title_screen_demo/tools/pkhex_bridge/Program.cs)
  forwards CLI arguments to `BridgeConsole`.
- [`tools/pkhex_bridge/BridgeConsole.cs`](/Users/vanta/Desktop/title_screen_demo/tools/pkhex_bridge/BridgeConsole.cs)
  defines the stdout JSON shape emitted to native code.
- [`tools/pkhex_bridge/BridgeProbe.cs`](/Users/vanta/Desktop/title_screen_demo/tools/pkhex_bridge/BridgeProbe.cs)
  owns save loading, `SaveReader`, and the DTOs for trainer, Pokedex, Pokemon, boxes, and bag data.
- [`tools/pkhex_bridge/BridgeImport.cs`](/Users/vanta/Desktop/title_screen_demo/tools/pkhex_bridge/BridgeImport.cs)
  owns import-grade per-Pokemon reads, including raw payload bytes and hashes.
- [`tools/pkhex_bridge/BridgeWriteBack.cs`](/Users/vanta/Desktop/title_screen_demo/tools/pkhex_bridge/BridgeWriteBack.cs)
  owns guarded projection write-back validation. It does not mutate saves yet.
- [`pokemon-resort/src/core/SaveBridgeClient.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/SaveBridgeClient.cpp)
  resolves and launches the helper process for probe, import, and write-projection validation.
- [`pokemon-resort/src/core/SaveLibrary.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/SaveLibrary.cpp)
  scans, probes, caches, parses the light ticket summary, and parses the deeper per-slot transfer box model used by `TransferSystemScreen`.
- [`pokemon-resort/src/resort/integration/BridgeImportAdapter.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/resort/integration/BridgeImportAdapter.cpp)
  parses import-grade bridge output into native `ImportedPokemon`.
- [`pokemon-resort/src/resort/services/BridgeImportService.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/resort/services/BridgeImportService.cpp)
  imports bridge-grade Pokemon into canonical Resort storage through `PokemonResortService`.

## Launch Contract

The bridge accepts these operations:

Preview/probe:

```bash
dotnet run --project /Users/vanta/Desktop/title_screen_demo/tools/pkhex_bridge/PKHeXBridge.csproj -- "/absolute/path/to/save.sav"
```

Import-grade read:

```bash
dotnet run --project /Users/vanta/Desktop/title_screen_demo/tools/pkhex_bridge/PKHeXBridge.csproj -- import "/absolute/path/to/save.sav"
```

Guarded projection write-back validation:

```bash
dotnet run --project /Users/vanta/Desktop/title_screen_demo/tools/pkhex_bridge/PKHeXBridge.csproj -- write-projection "/absolute/path/to/save.sav" "/absolute/path/to/projection.json"
```

On success, it exits with code `0` and writes one JSON object.
On failure, it exits non-zero and still writes one JSON object with `success: false`, `error`, and `details`.

`write-projection` currently validates the save and projection schema, then returns `write_back_not_implemented`. This is intentional: real save mutation requires target-format PKM conversion and per-save safe slot write rules.

The native launcher resolves bridge candidates in this order:

- `PKHEX_BRIDGE_EXECUTABLE` environment override
- helper bundled next to the native executable
- helper bundled under macOS app `Contents/Resources`
- debug or release build outputs under `tools/pkhex_bridge/bin`
- published helper under `tools/pkhex_bridge/publish`
- development fallback using `dotnet run --project`

For shipping, prefer a published self-contained helper rather than the development fallback.

## JSON Contract

The helper emits both legacy summary fields and expanded reader fields.

### Legacy Summary Fields

These fields exist so the current transfer ticket UI can keep working:

- `success`
- `game_id`
- `player_name`
- `party`
- `box_1`
- `play_time`
- `pokedex_count`
- `badges`
- `status`
- `saveType`
- `game`
- `trainerName`
- `error`
- `details`

Do not add new UI features against `box_1`.
It is a compatibility shim for old ticket code.
New box UI should use `boxes`.

### Expanded Reader Fields

Use these fields for new features:

- `trainer`
  Game identity, save type, generation, trainer IDs, money, play time, badges, and checksum state.
- `pokedex`
  Whether Pokedex data is supported, max species ID, seen/caught counts, completion percentages, and per-species seen/caught entries exposed by PKHeX.
- `all_pokemon`
  Every present party and boxed Pokemon with location metadata.
- `boxes`
  Every box, every slot, each slot's global index, lock/overwrite metadata, and present Pokemon.
- `bag`
  Bag support flag, pockets, pocket type/name/capacity, and item stacks.

### Shape Notes

- Box slot counts are game-specific. Do not assume 30 slots. Gen 1 saves can expose 20-slot boxes.
- Empty box slots are represented as slots with `pokemon: null`.
- `all_pokemon` is an index-friendly convenience view. `boxes` remains the source of truth for visualizing box layout.
- Bag pocket names and capacities are whatever PKHeX exposes for that game. Do not hardcode pocket order in UI.
- Item names can be unknown or placeholder-like for some IDs. Always use `item_id` as the stable identity.

## Reader Models

The bridge models live in [`BridgeProbe.cs`](/Users/vanta/Desktop/title_screen_demo/tools/pkhex_bridge/BridgeProbe.cs).
They are designed to be useful for reads now and for the guarded write-back work that will eventually fill in `write-projection`.

### Trainer

`SaveTrainerData` includes:

- trainer name
- normalized `game_id`
- raw PKHeX `game`
- save class name
- generation and context
- play time
- gender and language
- `ID32`, `TID16`, `SID16`, display TID, display SID
- money, coins, badges
- checksum validity and checksum message

Use this for trainer cards, save headers, and validation warnings.

### Pokedex

`SavePokedexData` includes:

- `supported`
- max species ID
- seen and caught counts
- seen and caught percentages
- entries only for species that are seen or caught

Entries include species ID, display name, slug, seen, and caught.
The model intentionally avoids inventing data if `PKHeX.Core` does not expose Pokedex support for a save type.

### Pokemon

`SavePokemonSummary` includes:

- location
- PKM format class
- species ID, species name, and sprite slug
- nickname, form, gender, level
- egg and shiny state
- held item ID/name
- nature, ability ID
- moves with move ID/name/current PP/PP Ups
- PKM checksum validity

Locations use:

- `area: "party"` with `box: -1`
- `area: "box"` with `box` and `slot`

For future editing, keep location data stable and explicit.
Do not rely on display order alone to identify a Pokemon.

### Boxes

`SaveBoxData` includes:

- zero-based box index
- display name
- slot count
- slots

`SaveBoxSlotData` includes:

- zero-based slot
- global index
- whether the slot is locked
- whether the slot is overwrite-protected
- optional Pokemon summary

For a box visualizer, render from `boxes[index].slots`.
Do not infer layout from `all_pokemon`.

### Bag

`SaveBagData` includes:

- support flag
- pockets

`SaveBagPocket` includes:

- zero-based pocket index
- raw PKHeX pocket type
- humanized pocket name
- capacity
- max item count
- item stacks

`SaveBagItem` includes:

- slot
- item ID
- item name
- slug
- count

For a bag visualizer, item ID is the stable key.
Names and slugs are display helpers.

## Extension Rules

When adding new read data:

- Prefer `SaveFile` and `PKM` public APIs.
- Use reflection only when PKHeX exposes a concept differently across generations.
- Preserve game-specific shapes instead of normalizing away important differences.
- Add fields to explicit DTOs before adding ad hoc JSON properties.
- Keep legacy fields stable until native C++ no longer consumes them.
- Update integration tests against real sample saves.
- Update this document when the bridge contract changes.

When completing write/edit support:

- Use the existing `write-projection` operation instead of overloading probe behavior.
- Require source path, operation name, and payload.
- Always write to a new output path or create a backup before replacing the input.
- Reopen the written save with PKHeX and validate checksums before reporting success.
- Use PKHeX slot APIs like `SetBoxSlotAtIndex` rather than writing raw bytes directly.
- Respect `IsBoxSlotLocked` and `IsBoxSlotOverwriteProtected`.
- Keep source-of-truth identifiers stable: game ID, source path, box, slot, and global slot index.

## Current Native Consumption

Native C++ consumes the bridge in three places:

- [`SaveLibrary.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/SaveLibrary.cpp) consumes the ticket summary fields and the current transfer-slot read model used by the transfer box UI.
- [`BridgeImportAdapter.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/resort/integration/BridgeImportAdapter.cpp) parses `bridge_import_schema: 1` import-grade Pokemon output.
- [`SaveBridgeClient.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/SaveBridgeClient.cpp) exposes `writeProjectionWithBridge` for guarded write-back validation.

The current transfer ticket UI summary path parses:

- `game_id`
- `player_name`
- `party`
- `play_time`
- `pokedex_count`
- `badges`
- `status`
- `error`

The current transfer box-detail path parses one native per-slot struct from `boxes[].slots[].pokemon` and `all_pokemon` fallback, including:

- slot occupancy and slot metadata
- `area`, `box`, `slot`, and `global_index`
- `format`
- species ID, species name, sprite slug, nickname, form, gender, level
- shiny and egg flags
- held item ID and held item name
- nature and ability ID
- up to four moves with move ID, name, current PP, and PP Ups
- checksum validity

When bridge fields are missing for older or legacy probe payloads, native code keeps explicit defaults rather than re-querying JSON during rendering.

Canonical Resort import parses:

- `source_game`
- `format_name`
- `raw_payload_base64`
- `raw_hash_sha256`
- `hot`
- `warm_json`
- `suspended_json`

The ticket list still does not consume `trainer`, `pokedex`, or `bag`.
The transfer box UI now consumes parsed slot data derived from `boxes` and `all_pokemon`, and canonical Resort import continues to consume the separate import-grade payload.

## Testing

Run unit tests:

```bash
DOTNET_CLI_HOME=/Users/vanta/Desktop/title_screen_demo/.dotnet \
NUGET_PACKAGES=/Users/vanta/Desktop/title_screen_demo/.nuget/packages \
dotnet test /Users/vanta/Desktop/title_screen_demo/tests/unit/pkhex_bridge/PKHeXBridge.UnitTests/PKHeXBridge.UnitTests.csproj --no-restore
```

Run integration tests:

```bash
DOTNET_CLI_HOME=/Users/vanta/Desktop/title_screen_demo/.dotnet \
NUGET_PACKAGES=/Users/vanta/Desktop/title_screen_demo/.nuget/packages \
dotnet test /Users/vanta/Desktop/title_screen_demo/tests/integration/pkhex_bridge/PKHeXBridge.IntegrationTests/PKHeXBridge.IntegrationTests.csproj --no-restore
```

Integration tests use real save files from [`saves`](/Users/vanta/Desktop/title_screen_demo/saves).
When changing box, bag, Pokedex, trainer, or Pokemon models, add tests that prove the behavior on at least one real save.

## Things To Watch

- PKHeX package upgrades can change public API shape and game behavior. Treat upgrades as migrations with tests.
- Some games expose fewer concepts than others. Return `supported: false` or empty arrays rather than fabricating data.
- Some item/species/move names can be blank or placeholder-like. Keep numeric IDs in all visualizer models.
- The bridge is process-based by design. Do not link `PKHeX.Core` into native C++ unless the architecture is intentionally changed.
- The top-level [`saves`](/Users/vanta/Desktop/title_screen_demo/saves) directory is reference/sample data, not app source code.
