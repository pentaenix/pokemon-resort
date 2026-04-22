# Pokesprite Asset Subsystem

## Purpose

`assets/pokesprite` is now the source of truth for:

- transfer ticket party Pokemon sprites
- transfer game PC box Pokemon sprites
- future item icon rendering

UI code should not build Pokemon filenames or item icon paths manually.

## Owning Files

- [`PokeSpriteAssets.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/core/PokeSpriteAssets.hpp)
- [`PokeSpriteAssets.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/PokeSpriteAssets.cpp)
- [`PcSlotSpecies.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/core/PcSlotSpecies.hpp)
- [`SaveLibrary.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/SaveLibrary.cpp)

Current UI consumers:

- [`TransferTicketScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TransferTicketScreen.cpp)
- [`TransferSystemScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TransferSystemScreen.cpp)

## Public API

`PokeSpriteAssets` exposes:

- `create(project_root)`
- `pokemonStyleDirectory()`
- `resolvePokemon(PokemonSpriteRequest)`
- `resolvePokemon(PcSlotSpecies)`
- `resolveItemIcon(item_id, usage)`
- `loadPokemonTexture(renderer, request_or_slot)`
- `loadItemTexture(renderer, item_id, usage)`

Resolved Pokemon data includes:

- stable cache key
- relative asset path
- normalized species slug
- resolved form key
- whether a female variant was used
- whether fallback art was used

Resolved item data includes:

- stable cache key
- relative icon path
- item key (`item_####`)
- whether fallback art was used

## Pokemon Resolution Rules

Input comes from parsed native transfer data, not raw JSON:

- `species_id`
- `slug`
- `form_key`
- `gender`
- `is_shiny`

Metadata source:

- [`assets/pokesprite/data/pokemon.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/assets/pokesprite/data/pokemon.json)

Directory source:

- `assets/pokesprite/pokemon-genX/regular`
- `assets/pokesprite/pokemon-genX/shiny`

Resolution flow:

1. Prefer the parsed bridge slug.
2. Fall back to `pokemon.json` slug by species ID.
3. Normalize `form_key`.
4. Use metadata form aliases like `midday -> $`.
5. If the requested form has `has_female` and the Pokemon is female, try `slug-form-f` first.
6. Try `slug-form`, then `slug`.
7. Choose `shiny` or `regular` directory from `is_shiny`.
8. If no concrete sprite exists, use `pokemon-genX/unknown.png`.

This means shiny, form, and female variants resolve through one place for both ticket and box rendering.

## Item Resolution Rules

Metadata source:

- [`assets/pokesprite/data/item-map.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/assets/pokesprite/data/item-map.json)

Directory source:

- `assets/pokesprite/items`

Resolution flow:

1. Convert item ID to `item_####`.
2. Look up the mapped icon path from `item-map.json`.
3. Build `assets/pokesprite/items/<mapped>.png`.
4. If `ItemIconUsage::Bag` is requested and the mapped icon ends in `--held`, swap to `--bag`.
5. If no icon exists, use `assets/pokesprite/items/etc/unknown-item.png`.

The item side is ready for future bag UI work, but no current screen consumes it yet.

## Loading and Caching

`PokeSpriteAssets` owns:

- JSON metadata parsing
- path resolution
- SDL texture caching by stable cache key

Screens should:

- keep parsed `PcSlotSpecies` vectors
- ask `PokeSpriteAssets` for textures
- never parse `pokemon.json` or `item-map.json` directly

Current behavior:

- `App.cpp` creates one shared `PokeSpriteAssets`
- `TransferTicketScreen` and `TransferSystemScreen` both use that shared instance
- box entry prewarming happens on `TransferSystemScreen::enter`

## Save/Bridge Data Required

For correct form and shiny rendering, native transfer parsing now depends on these bridge probe fields when available:

- `SpeciesId`
- `SpeciesSlug`
- `Form`
- `FormKey`
- `Gender`
- `IsShiny`

`SaveLibrary` parses those once into `PcSlotSpecies`.

## Future Expansion

### Adding future Pokemon sprite generations

If you add a new directory like `assets/pokesprite/pokemon-gen9`, the resolver will automatically prefer the highest discovered `pokemon-genN` directory.

To keep that safe:

- mirror the existing `regular` / `shiny` layout
- keep using `pokemon.json` slugs and form keys
- keep filenames slug/form based

If future assets need new metadata rules, extend `PokeSpriteAssets`, not the screens.

### Adding future Pokemon/forms

If new files follow the same slug/form naming and `pokemon.json` is updated, no screen rewrite should be needed.

### Adding future item icons

Add the PNGs under `assets/pokesprite/items/...` and update `item-map.json`.

## Old `assets/sprites` Folder Status

Current native Pokemon rendering no longer depends on `assets/sprites`.

That means:

- transfer ticket party Pokemon sprites now go through `PokeSpriteAssets`
- transfer game box Pokemon sprites now go through `PokeSpriteAssets`

From the current C++ app code, the old Pokemon sprite folder/references are replaceable.

Before deleting the old folder, still do one final project-wide check for:

- external tools
- design docs
- scripts outside the C++ app

As of this pass, there are no remaining native C++ runtime references to `assets/sprites` for Pokemon rendering.
