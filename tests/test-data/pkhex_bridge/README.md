# PKHeX integration fixtures

## Write-back

Place a single supported external save here as:

`writeback_fixture.sav`

Any generation PKHeX can load works (Gen 1–9). The integration tests copy this file to a **temporary directory** (they never mutate this original).

**Optional:** keep a personal copy outside the repo; only commit the fixture if your team agrees to ship a binary test asset.

**Quick setup**

```bash
cp "/path/to/your/game.sav" "tests/test-data/pkhex_bridge/writeback_fixture.sav"
```

The repo may ship an example copy from `saves/` (e.g. Emerald) for local runs; replace it per title when you add **one save per supported game**. The rename integration test uses an 8-character alphanumeric marker (`WBOK1234`) because some cartridges truncate names or reject certain symbols.

Or point tests at any path:

```bash
export PKHEX_WRITEBACK_FIXTURE_PATH="/path/to/your/game.sav"
dotnet test tests/integration/pkhex_bridge/PKHeXBridge.IntegrationTests/PKHeXBridge.IntegrationTests.csproj -c Release --filter "FullyQualifiedName~BridgeWriteBackIntegrationTests"
```

For broader compatibility, `BridgeWriteBackIntegrationTests.WriteBack_Schema2_RoundTripsAllCompatibilityFixtures`
enumerates every committed binary under `tests/test-data/saves/` (plus `writeback_fixture.sav` / `PKHEX_WRITEBACK_FIXTURE_PATH` when present) and exercises PC payload write-back on **temporary copies** only — it does not read the mutable top-level `saves/` folder.
Committed Gen 1–9 fixtures live there as `1_Blue.sav` … `9_Vi` (see `tests/test-data/saves/README.md`).
To override or add extra paths without committing files:

```bash
export PKHEX_WRITEBACK_COMPAT_FIXTURES="/path/gen1.sav:/path/gen2.sav:/path/gen3.sav:/path/gen4.sav:/path/gen5.sav:/path/gen6.sav:/path/gen7.sav:/path/gen8.sav:/path/gen9.sav"
dotnet test tests/integration/pkhex_bridge/PKHeXBridge.IntegrationTests/PKHeXBridge.IntegrationTests.csproj -c Release --filter "FullyQualifiedName~WriteBack_Schema2_RoundTripsAllCompatibilityFixtures"
```

The test writes only temporary copies and should pass for each fixture before claiming that generation's save write-back is ready.

## Held-item patch (`item_transfer.sav`)

Committed fixture for `HeldItemTransferIntegrationTests`: **Box 1** (first PC box, index `0`), **slots 1–2** (indices `0` and `1`) — Bulbasaur holding a **Poké Ball** (Gen III item id `4`), Charmander with **no** held item.

Replace only if you intentionally change the scenario; keep species, slots, and ball id aligned with the test or update `HeldItemTransferIntegrationTests.cs`.
