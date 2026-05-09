# Save+Exit Safety (No Dup / No Drop)

This document defines the safety invariants and guardrails for the transfer system’s mixed environment:

- The transfer UI allows **temporary in-memory moves** (game panel boxes + resort panel boxes) while the screen is open.
- **Persisted side effects** (writing a real save file, mutating Resort SQLite, mutating OpenHome storage) only happen during **Save+Exit**.

If you change transfer tools (basic / multi / swap / item / box-space) or Save+Exit orchestration, keep this document accurate.

## Core product promise

- **Never duplicate** a Pokémon (player-visible “copy exists in both places”).
- **Never delete** a Pokémon (player-visible “it vanished”).
- When something is uncertain, **fail closed**: abort Save+Exit and keep the real save file unchanged.

## Identity model used by safety checks

The transfer UI has multiple representations of “the same Pokémon”:

- PKHeX import-grade encrypted PC bytes (hash available as `bridge_box_payload_hash_sha256`)
- OpenHome tracking (`home_tracker` / `openhomeId`)
- Resort record identity (`resort_pkrid`)

For safety checks we treat the Pokémon’s identity as:

1. `bridge_box_payload_hash_sha256` when present (stable per encrypted slot payload),
2. else `home_tracker` / OpenHome id when present,
3. else `resort_pkrid` when present,
4. else a best-effort fallback signature (only for UI-only fixtures/tests).

Important: **identity must be location-independent**. Moving a Pokémon between panels must not look like creation/deletion.

## Lightweight UI conservation guard (always-on)

Every tool action that changes the in-memory PC state must preserve the multiset of Pokémon identities across:

- game box slots (`game_pc_boxes_`)
- resort box slots (`resort_pc_boxes_`)
- held single Pokémon (`pokemon_move_`)
- held multi group (`multi_pokemon_move_`)

If a tool action would change that multiset, the action must:

- abort immediately,
- leave the UI in a safe state (no partial placement),
- provide immediate player feedback (error SFX),
- log a high-signal diff of identity counts for debugging.

This is intentionally lightweight (no disk IO, no large logging unless failing).

## Save+Exit staging + verification (disk-side safety)

When Save+Exit performs OpenHome side effects, it must:

- write to a **staged save path** (not the player’s real save) for any operation that can affect a `.sav`,
- validate staged output before replacing the real save,
- only replace the real save file when every commit step succeeds.

### OpenHome pull-to-home (Game → Resort)

Requirements:

- Destination in Home storage must be a **guaranteed empty Home slot** (do not rely on UI box/slot mirroring).
- After `pullPokemonToHome`, the staged save must be re-imported and checked so every source slot intended to be cleared is **actually empty**.
  - If any source slot still contains a Pokémon, abort Save+Exit (prevents accidental “swap/displacement” semantics from duplicating).

### OpenHome push-to-game (Resort → Game)

Requirements:

- Target game slot must be treated as a real destination with explicit overwrite rules.
- Placement must be recorded as `IN_GAME_SAVE` only on success.

## Placement state (replacement for “mirrors”)

The intended long-term replacement for mirror sessions is placement state:

- `RESORT_BOX`
- `IN_GAME_SAVE`
- `HOME_BANK`

Placement is keyed by Resort `pkrid` but tracks the linked `openhomeId` and its current “where is it now?” state.

## “Do not regress” checklist for future changes

If you change any transfer tool or any Save+Exit step, ensure:

- UI conservation guard wraps the mutation and still passes for all happy-path moves.
- Save+Exit uses staged writes for real-save mutations.
- OpenHome pulls verify source slots are empty post-pull on the staged save.
- OpenHome-backed slots are never overwritten by PKHeX projection (preserve markers remain intact).
- Docs that describe movement behavior are updated:
  - `docs/openhome-first-mirror-retirement.md`
  - `docs/openhome-persistent-identity-integration.md`
  - this document

