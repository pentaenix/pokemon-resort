# Pokémon Attend Alola environments

Attend environments are lossless RAE `.glbz` packages, not ordinary one-texture floor models. The runtime verifies the `PRGLBZ01` payload, reads the embedded GLB, and requires `extras.rae.environmentScene` for the Alola catalog.

## Export contract

`environmentScene` schema version 1 contains the semantic scene ID, source GARC and slots, composition ID, centre/outer/self-contained layer roles, the spawn surface anchor, a 30 fps source clock, named time/weather states, and ambient clip references. Each mesh node retains its source slot and composition priority. Each material can retain up to three texture units, PICA TEV stages, sampler state, alpha/blend/depth policy, and independent UV motion in `extras.rae.picaTev` and `extras.rae.mapMaterialMotion`.

Continuous UV curves are sampled from elapsed seconds and interpolated. Visibility and fixed palette states use discrete source frames. Time and weather are independent; unsupported states fall back to `day` and `clear`. An unsupported TEV source or combiner is an import failure, never a flat white fallback.

UV motion applies to both ordinary single-texture materials and full PICA TEV materials. RAE keys visibility frames by exported node name; when GFModel optimization merges logical source shapes, `meshVisibilitySources` records the source-to-exported-node aliases used to preserve the animation.

`environmentScene.renderPasses` identifies base-water providers and water overlays explicitly. Base passes render before foam/current overlays; palette tracks are sampled only through fixed time/weather poses and are excluded from ambient playback. Short authored ambient loops remain active when they target independent materials, preserving sparkles and other detail passes.

## Routing

Each entry in `config/gameplay/pokemon_attend/environments/alola_battle_maps.json` owns an `appearsForPokemon` array. Species IDs listed there route directly to that environment; empty arrays mean the map has no species-specific assignment. Event, tile, pool, and legacy fallback routing remains in `config/gameplay/pokemon_attend/routing/alola_scene_routes.json`. Precedence is:

1. Explicit event route.
2. Exact Pokémon assignment from the environment's `appearsForPokemon` array.
3. Legacy exact Pokémon or Pokémon-group route.
4. `attend.scene.<runtime-id>` or exact tile tag route.
5. A weighted choice from the tile surface pool.
6. `alola_grass_arena`.

Water is one gameplay surface with optional `terrain.sea`, `terrain.freshwater`, and `terrain.river` tags. Untyped water uses the sea pool. `terrain.flower`, `terrain.dirt`, `terrain.rock`, and `terrain.dry-grass` are also canonical. Bathroom, 0086, and 0092 remain manual/debug scenes.

## Overworld handoff

`AttendLaunchContext` snapshots the interacted Pokémon before the overworld renderer closes. It carries actor and optional Resort box/slot identity, species slug, form, shiny state, display name, the Pokémon's standing tile and tags, and optional event/time/weather IDs. Routing always uses the target Pokémon's tile, not the player's tile. Returning to the overworld preserves the same locked actor interaction.
