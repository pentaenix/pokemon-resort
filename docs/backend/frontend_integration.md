# Frontend Integration Guide

## External Save UI

For "what is in this external game save?" screens, use existing save discovery and bridge probe data. Reliable display fields include:

- trainer name and game ID
- box count and slot count
- slot occupied/empty state
- preview species/nickname/level where present
- bag and Pokedex display data where supported

This is preview/import-source data, not canonical Resort data.

## Resort Box UI

Use:

- `PokemonResortService::getBoxSlotViews(profile_id, box_id)` for box rendering
- `PokemonResortService::getPokemonById(pkrid)` for full detail panels
- `PokemonResortService::getPokemonLocation(profile_id, pkrid)` for placement lookup

Render boxes from `PokemonSlotView`, not from snapshots, bridge DTOs, or transfer tickets.

## Import UI

The frontend should call a backend orchestration path equivalent to `BridgeImportService::importSave`.

UI should provide:

- save path selected by the user
- target Resort profile
- target placement start or explicit slot
- placement policy

UI must not parse bridge import JSON directly.

Existing method to reuse:

- `BridgeImportService::importSave(save_path, options)`
- `PokemonResortService::importParsedPokemon(imported, context)` only when the caller already has validated import-grade native data

## Export UI

The frontend should call `PokemonResortService::exportPokemon` or a higher-level backend wrapper.

Display the returned `mirror_session_id` if debugging, but do not treat it as Pokemon identity. Canonical identity remains `pkrid`.

Existing method to reuse:

- `PokemonResortService::exportPokemon(pkrid, context)`
- `PokemonResortService::getActiveMirrorForPokemon(pkrid)` for backend/debug state

## Do Not Depend On

- `box_1` legacy probe field
- sprite slugs as durable identity
- source save path as identity
- external game slot as identity
- warm/cold JSON keys directly in UI
- synthetic projection payload as a final PKM format

## Stable Enough For UI Now

- `pkrid`
- `PokemonSlotView`
- `BoxLocation`
- `ResortPokemon.hot`
- import result `created`/`merged`/`match_reason`
- export result `snapshot_id` and `mirror_session_id`

See [api_reference.md](api_reference.md) for the current method list before adding another frontend-facing backend wrapper.

Deferred UI-facing work includes richer conflict prompts, native Gen 1/2 ambiguous matching UX, real write-back completion, and advanced warm/cold detail rendering.
