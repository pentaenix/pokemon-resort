# Testing And Seed Data

This page covers backend-specific tests and seed/export workflows. The canonical repository-wide test map is [`/Users/vanta/Desktop/title_screen_demo/tests/README.md`](/Users/vanta/Desktop/title_screen_demo/tests/README.md).

## Native Test Suite

Build and run:

```bash
cd /Users/vanta/Desktop/title_screen_demo/pokemon-resort
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The backend-focused native CTest target is:

```bash
resort_storage_tests
```

It covers storage, import, merge, mirror sessions, export projection, managed return import, rollback, bridge import parsing, and the seed tool.

This is one target within the larger native CTest suite. Do not treat it as the only native test executable.

Bridge tests should be run when backend changes touch bridge import, import-grade JSON, or guarded write-back validation:

```bash
DOTNET_CLI_HOME=/Users/vanta/Desktop/title_screen_demo/.dotnet \
NUGET_PACKAGES=/Users/vanta/Desktop/title_screen_demo/.nuget/packages \
dotnet test /Users/vanta/Desktop/title_screen_demo/tests/unit/pkhex_bridge/PKHeXBridge.UnitTests/PKHeXBridge.UnitTests.csproj --no-restore
```

```bash
DOTNET_CLI_HOME=/Users/vanta/Desktop/title_screen_demo/.dotnet \
NUGET_PACKAGES=/Users/vanta/Desktop/title_screen_demo/.nuget/packages \
dotnet test /Users/vanta/Desktop/title_screen_demo/tests/integration/pkhex_bridge/PKHeXBridge.IntegrationTests/PKHeXBridge.IntegrationTests.csproj --no-restore
```

## Seed Tool

`resort_backend_tool` is a backend-facing command for integration tests.

Create a Pokemon:

```bash
/Users/vanta/Desktop/title_screen_demo/pokemon-resort/build/resort_backend_tool seed \
  --db /tmp/profile.resort.db \
  --species 25 \
  --nickname Toolchu \
  --level 12 \
  --box 0 \
  --slot 0
```

Minimum required options:

- `--db`
- `--species`

Useful optional options:

- `--nickname`
- `--level`
- `--source-game`
- `--format`
- `--pid`
- `--ec`
- `--ot`
- `--tid`
- `--sid`
- `--box`
- `--slot`

The seed command creates import-grade native input, writes a raw snapshot, creates or merges canonical Pokemon through normal import policy, and optionally places it in a box.

Export a projection:

```bash
/Users/vanta/Desktop/title_screen_demo/pokemon-resort/build/resort_backend_tool export \
  --db /tmp/profile.resort.db \
  --pkrid pkr_... \
  --target-game 1 \
  --format pk1 \
  --gen12-beacon true \
  --out /tmp/projection.json
```

The export command writes an export snapshot, opens a mirror session, and optionally writes the synthetic projection payload to a file.

Repair existing Resort Pokemon metadata:

```bash
/Users/vanta/Desktop/title_screen_demo/pokemon-resort/build/resort_backend_tool backfill-warm-metadata \
  --db "/Users/vanta/Library/Application Support/VantaStudio/PokemonResort/profile.resort.db" \
  --project-root /Users/vanta/Desktop/title_screen_demo/pokemon-resort \
  --dry-run true
```

If the dry run reports `failed:0`, run the same command without `--dry-run true`. The command creates
`profile.resort.db.bak.backfill-warm-metadata` by default, then rebuilds warm info-banner metadata from each
Pokemon's latest raw PK snapshot.

## Bridge Commands

For bridge contract details, launch resolution, and JSON examples, use [`../PKHEX_BRIDGE.md`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/docs/PKHEX_BRIDGE.md) as the canonical guide.

Development-time preview of an external save:

```bash
dotnet /Users/vanta/Desktop/title_screen_demo/tools/pkhex_bridge/bin/Debug/net10.0/PKHeXBridge.dll "/absolute/path/to/save.sav"
```

Import-grade read:

```bash
dotnet /Users/vanta/Desktop/title_screen_demo/tools/pkhex_bridge/bin/Debug/net10.0/PKHeXBridge.dll import "/absolute/path/to/save.sav"
```

Guarded write-back validation:

```bash
dotnet /Users/vanta/Desktop/title_screen_demo/tools/pkhex_bridge/bin/Debug/net10.0/PKHeXBridge.dll write-projection "/absolute/path/to/save.sav" "/tmp/projection.json"
```

The write-back command currently validates and refuses mutation with `write_back_not_implemented`.
