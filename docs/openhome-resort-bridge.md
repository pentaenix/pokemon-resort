# OpenHome Resort Bridge

## Status

- `observed`: Sources under `tools/OpenHome` are **vendored** in this repository (plain directories in git, not a submodule or nested remote checkout).
- `observed`: OpenHome can now be built locally from `tools/OpenHome`.
- `observed`: The Resort bridge command is `tools/OpenHome/dist/resort-bridge/resortBridge.mjs`.
- `observed`: The bridge successfully moved a Pokemon from an Emerald test save into OpenHome storage, moved that same `openhomeId` into a Black test save, then recognized and pulled it back from Black with the same `openhomeId`.
- `recommended`: Resort UI should call this bridge for OpenHome-owned movement instead of PKHeX write-back.

## Build

```bash
cd pokemon-resort/tools/OpenHome
env PATH=/opt/homebrew/opt/rustup/bin:/opt/homebrew/opt/node@22/bin:/opt/homebrew/bin:/usr/bin:/bin pnpm run resort-bridge:build
```

This builds:

```text
pokemon-resort/tools/OpenHome/pkm_rs/pkg/
pokemon-resort/tools/OpenHome/dist/resort-bridge/resortBridge.mjs
```

## Commands

Detect a save:

```bash
node pokemon-resort/tools/OpenHome/dist/resort-bridge/resortBridge.mjs \
  detect-save \
  --save /path/to/game.sav
```

Move a Pokemon from a game save slot into Resort/OpenHome storage:

```bash
node pokemon-resort/tools/OpenHome/dist/resort-bridge/resortBridge.mjs \
  pull-to-home \
  --storage-root /path/to/resort-openhome-storage \
  --save /path/to/game.sav \
  --box 0 \
  --slot 0 \
  --home-bank 0 \
  --home-box 0 \
  --home-slot 0
```

Move a stored OpenHome Pokemon into a target game save:

```bash
node pokemon-resort/tools/OpenHome/dist/resort-bridge/resortBridge.mjs \
  push-to-game \
  --storage-root /path/to/resort-openhome-storage \
  --openhome-id 0360-4dc361de-0425b28e-03 \
  --save /path/to/target-game.sav \
  --box 0 \
  --slot 0
```

Sync a save with existing tracked OpenHome Pokemon:

```bash
node pokemon-resort/tools/OpenHome/dist/resort-bridge/resortBridge.mjs \
  sync-save \
  --storage-root /path/to/resort-openhome-storage \
  --save /path/to/game.sav
```

Use the direct `node .../resortBridge.mjs` form for Resort integration. `pnpm run resort-bridge -- ...` is convenient for humans, but pnpm prints script headers around stdout.

## Storage

The bridge writes OpenHome-compatible storage:

```text
storage-root/
  mons_v2/
    <openhomeId>.ohpkm
  banks.json
  gen12_lookup.json
  gen345_lookup.json
```

`banks.json` box slots store `openhomeId` values. The canonical Pokemon payload is the `.ohpkm` file, not `.pk3`, `.pk4`, `.pk5`, or any PKHeX projection.

## Movement Semantics

- `pull-to-home` loads the save with OpenHome save classes, syncs already-tracked Pokemon, creates or loads an `OHPKM`, places its `openhomeId` into Home storage, and writes the source save through OpenHome.
- `push-to-game` loads the target save with OpenHome save classes, loads OHPKM by `openhomeId`, applies OpenHome trade/handler/conversion logic, writes the target save through OpenHome, and clears the Home placement when the Pokemon leaves storage.
- If a target Home slot or game slot is occupied, the bridge follows OpenHome UI displacement behavior rather than deleting data. **Resort must avoid relying on displacement** by selecting a guaranteed empty Home slot when pulling (see below).

## Resort Integration Rules

Resort keeps two identities with separate responsibilities:

- `pkrid`: Resort record id. All Resort-owned data (boxes, memories, accessories, travel history, jobs, friendship, etc.) keys off this.
- `openhomeId`: OpenHome canonical Pokémon payload + cross-save movement id (OHPKM storage and conversions).

Resort should:

- allocate/keep a `pkrid` row for Resort-owned metadata,
- link that `pkrid` to exactly one current `openhomeId`,
- track where the Pokémon currently “lives” via placement state (`RESORT_BOX`, `IN_GAME_SAVE`, `HOME_BANK`) rather than mirror sessions.

### Pull safety: never “swap-displace” by accident

When calling `pull-to-home`, Resort must pick a Home destination that is guaranteed empty (do not derive `home_bank/box/slot` from UI indices).

If Resort accidentally targets an occupied Home slot, OpenHome may perform a swap/displacement rather than a clear → place, which can leave the source game slot non-empty and create a player-visible duplication on next open.

## Related docs

- `docs/openhome-first-mirror-retirement.md`
- `docs/transfer_system/SAVE_EXIT_SAFETY.md`

## Tested Copy-Save Loop

```text
Emerald box 0 slot 0
  -> OHPKM 0360-4dc361de-0425b28e-03 in OpenHome storage
  -> Black box 0 slot 0
  -> back to OpenHome storage with the same openhomeId
```

## Next Wiring

- Route Resort's visible box import/move actions to `pull-to-home` and `push-to-game`.
- Store Resort memories/history/relationships in Resort-owned tables keyed by `openhomeId`.
- Treat PKHeX save writing as legacy for the old path only. For OpenHome-owned movement, OpenHome must write saves.
