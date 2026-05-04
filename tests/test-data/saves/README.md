# Committed `.sav` fixtures for automated tests

Binary saves live **here** under version control (or team policy). Tests resolve paths such as:

`tests/test-data/saves/<filename>`

Do **not** point tests at the top-level [`saves`](../../../saves) folder — that directory is for local reference samples and may be absent in CI.

Current copies used by integration / native tests:

- **`1_Blue.sav` … `9_Vi`** — committed Gen 1 (Blue) through Gen 9 (Violet) saves for `Probe_NumberedGenerationFixture_Loads`, `WriteBack_Schema2_RoundTrips_NumberedGen1Through9Fixtures`, and the schema-2 compatibility sweep (`6_X`, `7_UM`, `8_Sw`, `9_Vi` use extensionless names like the originals).
- `pokemon blue - ASH.sav` — Gen 1 probe + resort bridge-import test  
- `Pokemonheartgold.sav` — HGSS probe theory  
- `A_pkm_US_Trade` — transfer markings probe  

Add new `.sav` files here when introducing additional bridge probe coverage, then reference them via `CommittedSaveFixture(...)` in `BridgeProbeIntegrationTests` / the same path layout under `tests/test-data/saves/` in C++.
