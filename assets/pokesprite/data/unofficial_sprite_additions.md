# Unofficial Sprite Additions

This directory also includes a machine-readable manifest at `unofficial_sprite_additions.json`.

Use that manifest to track:

- which sprite files were copied into `assets/pokesprite/pokemon-gen8`
- which existing files were overwritten
- which `pokemon.json` entries were added or updated
- which `gen-8` form keys were merged for each entry

Replacement rule:

- If you later replace an unofficial sprite with a preferred version, keep the same relative file path whenever possible so `pokemon.json` does not need to change.
- If a replacement uses a different form name, update both the sprite path and the matching `gen-8.forms` key in `pokemon.json`.
- Oinkologne female uses the standard `female/` folder layout rather than a `-f` filename suffix.
