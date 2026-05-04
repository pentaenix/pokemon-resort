# Integration Tests

Use this folder for tests that cross module or process boundaries, including PKHeX-backed save probing.

**Bridge (PKHeX):**

- **Probe** — `BridgeProbeIntegrationTests` uses only committed files under [`../test-data/saves`](../test-data/saves) (see that README). No dependency on the top-level `saves/` folder.
- **Write-back** — `BridgeWriteBackIntegrationTests` when `tests/test-data/pkhex_bridge/writeback_fixture.sav` exists (or `PKHEX_WRITEBACK_FIXTURE_PATH`).
- **Held-item patch** — `HeldItemTransferIntegrationTests` and `tests/test-data/pkhex_bridge/item_transfer.sav`.

The raw write-back fixture was provided as a hand-verified baseline with play time 1:01:01. The current bridge contract exposes hours and minutes only, so tests assert `1:01` until bridge output intentionally adds seconds.
