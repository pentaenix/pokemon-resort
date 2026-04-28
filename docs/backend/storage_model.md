# Backend Storage Model

Resort profile storage is SQLite-backed. The runtime path is `save_directory / persistence.resort_profile_file_name` from [`title_screen.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/config/title_screen.json) persistence (same `save_directory` as `pokemon_resort.sav`; default file name `profile.resort.db`), opened by `PokemonResortService`.

## Core Tables

- `pokemon` stores canonical hot fields plus warm/cold JSON payloads. This is the long-lived Resort record.
- `boxes` stores box metadata for a profile.
- `box_slots` stores placement independently from canonical Pokemon. A partial unique index on `(profile_id, pkrid)` ensures one Pokemon occupies at most one slot per profile.
- `pokemon_snapshots` stores exact raw imported/exported payload bytes and parsed notes.
- `pokemon_history` stores audit events.
- `mirror_sessions` stores outbound managed projections and return-tracking anchors.

The schema is created in [`Migrations.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/resort/persistence/Migrations.cpp). Current schema version is `1`.

Important indexes and constraints:

- `pokemon.pkrid` is the canonical primary key.
- `boxes` primary key is `(profile_id, box_id)`.
- `box_slots` primary key is `(profile_id, box_id, slot_index)`.
- `idx_box_slots_profile_pkrid` is unique where `pkrid IS NOT NULL`, so one Pokemon can occupy only one slot per profile.
- `pokemon_snapshots.pkrid`, `pokemon_history.pkrid`, and `mirror_sessions.pkrid` reference `pokemon`.
- snapshot/history/mirror foreign keys used by import/export transactions are deferred where snapshot-first or mirror-history ordering requires it.

## Read Models

Use `PokemonSlotView` for box rendering. It is intentionally lightweight and comes from an indexed join through `BoxViewService`.

Use `ResortPokemon` only when a full detail view needs canonical hot/warm/cold fields.

Use `BoxLocation` from `PokemonResortService::getPokemonLocation(profile_id, pkrid)` when the UI needs to locate a canonical Pokemon inside Resort.

Default Resort profile creation uses `BoxRepository::ensureDefaultBoxes(profile_id)` (defaults: **60** boxes × 30 slots via `kDefaultResortPcBoxCount`, idempotent `INSERT OR IGNORE`). External game saves have game-specific box counts and slot counts; do not project those assumptions back onto bridge preview UI.

## Important Invariants

- Canonical Pokemon identity is `pkrid`, not save path, box slot, pointer identity, or source game position.
- Box placement is not stored inside the Pokemon row.
- Snapshots preserve raw bytes for every meaningful lifecycle event.
- Export projections do not delete canonical warm/cold data.
- Merge never clears warm/cold fields just because an older format lacks them.
