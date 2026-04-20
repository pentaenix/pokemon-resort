# Backend Storage Model

Resort profile storage is SQLite-backed. The default runtime path is `profile.resort.db` under the app save directory, opened by `PokemonResortService`.

## Core Tables

- `pokemon` stores canonical hot fields plus warm/cold JSON payloads. This is the long-lived Resort record.
- `boxes` stores box metadata for a profile.
- `box_slots` stores placement independently from canonical Pokemon. A partial unique index on `(profile_id, pkrid)` ensures one Pokemon occupies at most one slot per profile.
- `pokemon_snapshots` stores exact raw imported/exported payload bytes and parsed notes.
- `pokemon_history` stores audit events.
- `mirror_sessions` stores outbound managed projections and return-tracking anchors.

## Read Models

Use `PokemonSlotView` for box rendering. It is intentionally lightweight and comes from an indexed join through `BoxViewService`.

Use `ResortPokemon` only when a full detail view needs canonical hot/warm/cold fields.

Use `BoxLocation` from `PokemonResortService::getPokemonLocation(profile_id, pkrid)` when the UI needs to locate a canonical Pokemon inside Resort.

## Important Invariants

- Canonical Pokemon identity is `pkrid`, not save path, box slot, pointer identity, or source game position.
- Box placement is not stored inside the Pokemon row.
- Snapshots preserve raw bytes for every meaningful lifecycle event.
- Export projections do not delete canonical warm/cold data.
- Merge never clears warm/cold fields just because an older format lacks them.
