# PokeSprite Asset Subsystem

`PokeSpriteAssets` is the source of truth for resolving and caching PokeSprite-backed Pokemon sprites, item icons, and misc icons used by transfer UI. UI code should not build Pokemon filenames, item icon paths, or misc icon paths manually.

## Owning Files

- [`include/core/assets/PokeSpriteAssets.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/core/assets/PokeSpriteAssets.hpp)
- [`src/core/assets/PokeSpriteAssets.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/assets/PokeSpriteAssets.cpp)
- [`include/core/domain/PcSlotSpecies.hpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/include/core/domain/PcSlotSpecies.hpp)
- [`src/core/save/SaveLibrary.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/core/save/SaveLibrary.cpp)

Current UI consumers:

- [`TransferTicketScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TransferTicketScreen.cpp): party sprites on transfer tickets.
- [`TransferSystemScreen.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/TransferSystemScreen.cpp): game/resort box sprites, held-item overlays, texture prewarming.
- [`TransferSystemRenderer.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/TransferSystemRenderer.cpp): held-object drawing, including held items.
- [`TransferInfoBannerRenderer.cpp`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/src/ui/transfer_system/TransferInfoBannerRenderer.cpp): banner item and misc icons.

## Public API

`PokeSpriteAssets` exposes:

- `create(project_root)`
- `pokemonStyleDirectory()`
- `resolvePokemon(PokemonSpriteRequest)`
- `resolvePokemon(PcSlotSpecies)`
- `resolveItemIcon(item_id, usage)`
- `resolveMiscIcon(category, icon_key)`
- `loadPokemonTexture(renderer, request_or_slot)`
- `loadItemTexture(renderer, item_id, usage)`
- `loadMiscTexture(renderer, category, icon_key)`

Resolved Pokemon data includes a stable cache key, relative asset path, normalized species slug, resolved form key, female-variant flag, and fallback flag.

Resolved item data includes a stable cache key, relative icon path, normalized item key, and fallback flag.

Resolved misc data includes a stable cache key, relative icon path, category, icon key, and fallback flag.

## Pokemon Resolution

Input comes from parsed native transfer data, not raw bridge JSON:

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
8. If no concrete sprite exists, use the highest discovered generation's `unknown.png`.

This keeps shiny, form, female, ticket, and box rendering rules in one place.

## Item Icon Resolution

Metadata source:

- [`assets/pokesprite/data/item-map.json`](/Users/vanta/Desktop/title_screen_demo/pokemon-resort/assets/pokesprite/data/item-map.json)

Directory source:

- `assets/pokesprite/items`

Resolution flow:

1. Convert item ID to `item_####`.
2. Look up the mapped icon path from `item-map.json`.
3. Build `assets/pokesprite/items/<mapped>.png`.
4. If `ItemIconUsage::Bag` is requested and the mapped icon ends in `--held`, prefer the matching `--bag` variant.
5. If no icon exists, use `assets/pokesprite/items/etc/unknown-item.png`.

Current uses:

- transfer box held-item overlays
- held item drawing while an item is in hand
- lower info-banner held item icons

Future bag/deposit UI should reuse this API instead of adding a separate item resolver.

## Misc Icon Resolution

Misc icons are used for banner/status-style imagery that is not a Pokemon sprite and not a held item, such as type/status/marking-style groups.

Resolution is category plus icon key:

- `resolveMiscIcon(category, icon_key)`
- `loadMiscTexture(renderer, category, icon_key)`

Current banner code passes misc groups through `TransferInfoBannerRenderer`. If a new banner icon source is needed, extend the presenter/renderer plus `PokeSpriteAssets` rather than manually composing paths in `TransferSystemScreen`.

## Loading And Caching

`PokeSpriteAssets` owns:

- JSON metadata parsing
- path resolution
- SDL texture caching by stable cache key

Screens should:

- keep parsed `PcSlotSpecies` vectors
- ask `PokeSpriteAssets` for textures
- never parse `pokemon.json` or `item-map.json` directly
- never hand-build PokeSprite filenames

Current behavior:

- `App.cpp` creates one shared `PokeSpriteAssets`.
- `TransferTicketScreen` and `TransferSystemScreen` use that shared instance.
- Transfer-system box entry and viewport refresh paths prewarm relevant Pokemon/item textures.

## Save And Bridge Data Required

`SaveLibrary` parses bridge output into `PcSlotSpecies` once. Rendering should use that native model.

Important fields include:

- species ID/name/slug
- form and `form_key`
- gender
- shiny and egg state
- ball ID
- held item ID/name
- level and nickname
- types, Pokerus, markings, nature, ability, origin, met location, and move data for banner contexts

If bridge data is missing, native parsing should provide explicit defaults. Rendering code should not go back to raw bridge JSON.

## Expansion Rules

### Adding Pokemon Sprite Generations

If you add a directory like `assets/pokesprite/pokemon-gen9`, the resolver will automatically prefer the highest discovered `pokemon-genN` directory.

Keep the existing layout:

- `regular`
- `shiny`
- slug/form-based filenames
- metadata-driven aliases in `pokemon.json`

### Adding Pokemon Or Forms

If new files follow the same slug/form naming and `pokemon.json` is updated, screens should not need to change.

### Adding Item Icons

Add PNGs under `assets/pokesprite/items/...` and update `item-map.json`.

Use `ItemIconUsage` when the same item has different held/bag presentation.

### Adding Misc Icons

Prefer a stable category plus key. Update the banner presenter/renderer if the new icon represents a new field or context.

## Tests

Update [`pokesprite_assets_tests.cpp`](/Users/vanta/Desktop/title_screen_demo/tests/native/core/pokesprite_assets_tests.cpp) when changing resolver behavior, fallback rules, metadata parsing, item usage variants, or misc icon lookup.

Update the relevant transfer-system harness only when a sprite/icon change affects player-visible screen wiring.
