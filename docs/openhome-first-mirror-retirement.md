# OpenHome-First Mirror Retirement Notes

This note tracks the implementation migration from Resort mirrors to OpenHome-owned movement.

## Current Save+Exit Dependencies

`src/ui/transfer_system/TransferSystemScreenExitSaveModal.cpp` still contains the mixed path:

- `preparePendingResortMirrorPayloadsForSave()` builds PKHeX-compatible raw payloads for Resort-to-game moves that are not already OpenHome-backed.
- `commitPendingOpenHomeMovementBeforeSave()` calls `OpenHomeCliMovementBridge::pullPokemonToHome()` and `pushPokemonToGame()` for OpenHome-backed slots, using staged save writes.
- `commitPendingGameToResortImportsBeforeSave()` links OpenHome payloads to `pkrid` and records placement, while legacy PKHeX import-grade slot payloads may still exist for read/probe and for non-OpenHome-backed slots.
- `commitPendingResortStorageChangesAfterSave()` still calls `commitPreparedMirrorExport()` for prepared mirror exports.
- `writeProjectionWithBridge()` still writes non-OpenHome PC projection payloads through PKHeX.

### OpenHome-backed slots must be preserved during PKHeX projection

OpenHome-backed UI slots do not have PKHeX import-grade bytes. When writing a PKHeX `projection_schema: 2` payload:

- OpenHome-backed slots must emit `{"preserve_box_slot": true}` so PKHeX does not overwrite them.
- The PKHeX write-back applier must recognize `preserve_box_slot` and skip applying changes to that slot.

## Target Replacement

- Game-to-Resort movement should call OpenHome pull/sync first, persist the returned OHPKM payload, link the returned `openhomeId` to the Resort `pkrid`, then place that `pkrid` in Resort storage.
- Resort-to-Game movement should require a linked `openhomeId`, call OpenHome push-to-game, persist the returned OHPKM payload, remove the `pkrid` from Resort box placement, and record an `IN_GAME_SAVE` placement.
- Mirror sessions should not be created for migrated OpenHome flows.
- PKHeX write projection should not write moved Pokemon once both sides of the flow are OpenHome-backed.

## Compatibility Boundary

During migration, PKHeX import parsing can still populate display/cache fields for newly created Resort rows. That data is not the canonical Pokemon payload; OpenHome's OHPKM store is canonical for cross-game identity and movement.

## Safety hardening (no dup / no drop)

Two layers protect the player promise:

- **UI conservation guard**: tool actions (single/multi/swap/box-space/item) must not duplicate or delete a Pokémon in memory while the screen is open.
- **Staged Save+Exit verification**: after `pull-to-home`, the staged save is re-imported to verify every pulled source slot is empty. If not, Save+Exit aborts and the real save is not replaced.

See `docs/transfer_system/SAVE_EXIT_SAFETY.md` for the invariant definitions.
