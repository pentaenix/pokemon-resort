<!-- Canonical source: spmk/docs/CHARBIN_SCHEMA.md — synced by SPMK. Do not edit here. -->

# `.charbin` character package schema

**Canonical reference for SPMK static character packages.**  
When you change `spmk_app/character_package.py`, `spmk_app/charbin_io.py`, `spmk_app/data/sprite_profiles.json`, or package UI save rules, **update this file in the same change**, then run SPMK (or open Characters) so `assets/characters/CHARBIN_SCHEMA.md` is re-synced for C++.

| Item | Value |
|------|--------|
| Package JSON `schemaVersion` | `1` |
| Binary `formatVersion` | `1` |
| Magic bytes | `SPMKCHAR` (8 bytes) |
| Default library folder | `pokemon-resort/assets/characters/` (monorepo) or `spmk/assets/characters/` |
| Schema copy (for C++) | `assets/characters/CHARBIN_SCHEMA.md` (synced from `spmk/docs/CHARBIN_SCHEMA.md` by SPMK) |
| On-disk layout | `{playable\|npc\|pokemon}/{id}.charbin` under the library root |
| On-disk filename | `{id}.charbin` inside the type folder |

---

## Library folders

Under the configured library root (default `pokemon-resort/assets/characters/`):

| Folder | `metadata.characterType` | Examples |
|--------|--------------------------|----------|
| `playable/` | `player` (legacy `playable`) | Haru |
| `npc/` | `npc` | Cynthia, shop clerk |
| `pokemon/` | `pokemon` | Psyduck, Garchomp |
| `objects/` | `object` | Signs, PCs, Poké Balls, props (item PokéAPI metadata) |

SPMK saves and moves files when the type changes. Flat `*.charbin` at the library root are migrated into subfolders on scan.

---

## Purpose

A `.charbin` file is a **portable, static character definition** for the Pokémon-style C++ game:

- Identity (id, display name, internal name)
- Story / NPC metadata (when applicable)
- Embedded sprite sheet PNGs
- Logical **actions** (idle, walk, …) pointing at sheets + profile animation names
- Dialogue lines

It is **not** a save file. These fields must **never** appear in a package:

`mapId`, `map`, `position`, `worldPosition`, `spawn`, `spawnPoint`, `currentDirection`, `currentMap`, `partyState`, `runtimeState`, `eventState`, `x`, `y`, `z`, `tileX`, `tileY`

(Source: `_FORBIDDEN_ROOT_KEYS` in `character_package.py`.)

---

## Binary file layout (`.charbin`)

Little-endian. Implemented in `charbin_io.py`.

```
Offset   Size     Field
------   ----     -----
0        8        magic = "SPMKCHAR"
8        4        formatVersion (uint32) = 1
12       4        jsonLength (uint32)
16       N        UTF-8 JSON package (N = jsonLength)
16+N     4        assetCount (uint32)
...      *        asset table (assetCount entries)
```

Each **asset entry**:

```
string     assetId   (uint32 length + UTF-8)
string     mime      (uint32 length + UTF-8, typically "image/png")
uint32     blobLength
bytes      blobLength bytes of PNG data
```

- JSON references assets only by **`assetId`** strings.
- All PNG bytes are embedded; nothing in `workspace/assets/` is required at runtime for the game to load the charbin.
- Trailing bytes after the last asset are an error.

---

## JSON package (top level)

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `schemaVersion` | `number` | yes | Must be `1`. |
| `packageType` | `string` | yes | Always `"character"` today. |
| `id` | `string` | yes | Stable slug: `^[a-z][a-z0-9_]*$` (e.g. `cynthia`, `rival_barry`). Used for filename `{id}.charbin`. |
| `displayName` | `string` | yes | Human label in UI / game. |
| `internalName` | `string` | yes | Engine-facing name; often same as `id`. |
| `baseProfile` | `string` | yes | Default sprite profile key. One of: `character`, `pokemon_small`, `pokemon_large`. |
| `metadata` | `object` | yes | See [metadata](#metadata). |
| `spriteSheets` | `array` | yes | Sheet records; may be `[]` for empty draft. |
| `actions` | `array` | yes | Action records; may be `[]`. |
| `dialogue` | `object` | yes | See [dialogue](#dialogue). |
| `relationships` | `array` | no | Reserved; default `[]`. |
| `unlock` | `any` | no | Reserved; default `null`. |
| `custom` | `object` | no | Forward-compatible bag; unknown keys preserved on merge. |

Unknown top-level keys on import are **preserved** (deep merge does not strip them).

---

## metadata

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `originGame` | `string` | no | Provenance label. |
| `characterType` | `string` | no | `player`, `npc`, `pokemon`, or `object` (legacy `playable` → `player`). Default `npc`. |
| `objectAnimated` | `boolean` | no | **Object only.** Hint for the game: use `animate` vs `static` when both actions exist. Default `false`. |
| `pokemonId` | `number \| null` | no | National dex number (Pokémon only). |
| `speciesName` | `string` | no | Species display name (Pokémon only). |
| `forms` | `array` | no | `{id, name}` form variants (Pokémon only). |
| `selectedFormId` | `string` | no | Active form id. |
| `pokedexEntry` | `string` | no | Flavor text (Pokémon only). |
| `pokemonTypes` | `string[]` | no | e.g. `["dragon","ground"]` (Pokémon only). |
| `pokemonSize` | `string` | no | `small` or `large` — large uses 64×64 cells on a 256×256 sheet (`pokemon_large` profile). |
| `pokeapi` | `object \| null` | no | Cached PokéAPI snapshot (Pokémon only). See [metadata.pokeapi](#metadatapokeapi). Filled by SPMK **Fetch from PokéAPI**; embedded in `.charbin` so the game need not call the API. |
| `description` | `string` | no | Short bio (single text area). Shown for both types. |
| `personality` | `string[]` | no | Trait chips. Legacy single string is coerced to one item. **UI:** NPC only. |
| `likes` | `string[]` | no | **UI:** NPC only; chip list. |
| `dislikes` | `string[]` | no | |
| `tags` | `string[]` | no | **UI:** NPC only; chip list. |
| `partnerPokemon` | `object \| null` | yes* | `null` if none. *Validator warns if key missing. **UI:** NPC only. |
| `extraPartnerPokemon` | `array` | no | Additional partners. |
| `custom` | `object` | no | Extension point. |

### metadata.pokeapi

Written when autofill runs (`GET /api/packages/pokemon/lookup`). Extension fields may grow; unknown keys are preserved.

| Field | Type | Description |
|-------|------|-------------|
| `fetchedAt` | `number` | Unix ms when snapshot was taken. |
| `slug` | `string` | PokéAPI species id (e.g. `garchomp`). |
| `speciesId` | `number` | National dex number. |
| `pokeapiPokemonUrl` | `string` | REST URL for `/pokemon/{slug}`. |
| `pokeapiSpeciesUrl` | `string` | REST URL for species resource. |
| `generation` | `string` | e.g. `generation-iv`. |
| `color` | `string` | Species color. |
| `shape` | `string` | Body shape. |
| `habitat` | `string` | Habitat (may be empty). |
| `eggGroups` | `string[]` | e.g. `["monster","dragon"]`. |
| `growthRate` | `string` | e.g. `slow`. |
| `captureRate` | `number` | Species capture rate. |
| `baseHappiness` | `number` | Base friendship. |
| `genderRate` | `number` | PokéAPI gender rate (-1 = genderless). |
| `isLegendary` | `boolean` | |
| `isMythical` | `boolean` | |
| `isBaby` | `boolean` | |
| `height` | `number` | Decimeters. |
| `weight` | `number` | Hectograms. |
| `baseExperience` | `number` | |
| `baseStats` | `object` | Keys: `hp`, `attack`, `defense`, `special-attack`, `special-defense`, `speed`. |
| `abilities` | `array` | `{id, name, isHidden, slot}`. |
| `evolutionChainId` | `number \| null` | For evolution UI / future linking. |

**No image data** in `pokeapi` (no sprite URLs, no CDN art). Sheet PNGs are embedded separately via `spriteSheets` / `assetId` only.

Top-level `metadata` fields (`pokemonId`, `speciesName`, `pokedexEntry`, …) mirror the snapshot for UI and simple game reads.

### partnerPokemon (when not null)

| Field | Type | Description |
|-------|------|-------------|
| `pokemonId` | `string` | Species id (e.g. `garchomp`). |
| `formId` | `string` | Form variant; default `default`. |
| `nickname` | `string \| null` | Optional display nickname. |
| `relationship` | `string` | e.g. `main_partner`. |

---

## dialogue

| Field | Type | Description |
|-------|------|-------------|
| `lines` | `string[]` | Plain dialogue lines. **UI:** NPC only; cleared on save for `player`. |
| `packs` | `array` | Reserved structured dialogue. |
| `custom` | `object` | Extension point. |

---

## spriteSheets[]

Each sheet is one embedded PNG grid.

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | `string` | yes | Unique within package (e.g. `walk`). |
| `name` | `string` | no | Display name (e.g. `Walk`). |
| `assetId` | `string` | yes | Key into embedded assets (e.g. `walk_png`). |
| `profile` | `string` | no | Profile key; defaults to package `baseProfile`. |
| `profileOverrides` | `object` | no | Override `columns`, `rows`, etc. from profile. |
| `animations` | `object` | no | Per-sheet animation overrides; see below. |

Rules:

- `id` and `assetId` must each be unique across all sheets.
- `assetId` must exist in the embedded asset table when validating with assets.
- `profile` must be a known profile name.

### spriteSheets[].animations (optional overrides)

Map of animation name → spec. Merged with profile defaults for validation.

| Field | Type | Description |
|-------|------|-------------|
| `frames` | `number[]` | Column indices (0-based) on the animation row. |
| `frameTimeMs` | `number` | Milliseconds per frame. |
| `cells` | `{row,col}[]` | Alternative frame addressing (advanced). |

Frame indices must satisfy `0 <= frame < columns * rows` (after overrides).

---

## actions[]

Links game logic to a sheet + profile animation name.

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | `string` | yes | Unique action id (e.g. `idle`, `walk`, `fishing`). |
| `type` | `string` | yes | `idle`, `movement`, `walk` (legacy), or `activity` (see below). |
| `sheetId` | `string` | yes | Must match a `spriteSheets[].id`. |
| `animationName` | `string` | yes* | Key in sprite profile `animations` or sheet override. *Not used on `activity` (phases name clips). |
| `movementDriven` | `boolean` | no | `true` for walk-style actions; always `false` for `activity`. |
| `activityKind` | `string` | activity only | `"single"` or `"session"`. |
| `phases` | `object` | activity only | Phase id → `{ animationName, loop? }`. |
| `facingMode` | `string` | no | `activity` only: `"four_direction"` (default) or `"south_only"`. |

Recommended pair for trainers / NPCs (`character` profile):

| id | type | animationName | movementDriven |
|----|------|---------------|----------------|
| `idle` | `idle` | `idle` | `false` |
| `walk` | `movement` | `walk` | `true` |

Recommended triple for `metadata.characterType: "pokemon"` (`pokemon_small` / `pokemon_large`):

| id | type | animationName | movementDriven | Notes |
|----|------|---------------|----------------|-------|
| `idle` | `idle` | `walk` | `false` | **Uses walk cycle** while standing (Gen-style bob). |
| `pause` | `idle` | `pause` | `false` | **Frame 0 only** — no animation advance. |
| `walk` | `movement` | `walk` | `true` | Movement-driven walk. |

Validator **warnings** (not errors) if `character` profile lacks idle or walk/movement actions, or if Pokémon lacks `pause` / walk or idle does not target `walk`.

Recommended for `metadata.characterType: "object"` (`object` profile, sheet id `sheet`):

| id | type | animationName | movementDriven | Notes |
|----|------|---------------|----------------|-------|
| `play` | `idle` | `play` | `false` | Row-major 4×4 grid, up to 10 non-empty cells, **no loop** (`loop: false`). |

Per-sheet `spriteSheets[].animations.play.frames` overrides detected frames after import. Objects must **not** use movement / walk actions.

### Activity actions (proposed)

Stationary overworld clips that use the 4-direction grid but **do not** move tiles (fishing, watering plants, petting). Same sheet layout as walk/run; gameplay does not advance world position.

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `type` | `string` | yes | `"activity"` |
| `activityKind` | `string` | yes | `"single"` (one `play` phase) or `"session"` (`enter` → `stay` → `exit`) |
| `sheetId` | `string` | yes | Sheet with PNG + `animations` overrides |
| `movementDriven` | `boolean` | yes | Always `false` |
| `facingMode` | `string` | no | `"four_direction"` (default) or `"south_only"` |
| `phases` | `object` | yes | Phase id → `{ animationName, loop? }` |

**Phase ids:** `single` → `play` only. `session` → `enter`, `stay`, `exit` (all required). Each `animationName` resolves on the sheet's `animations` map. Set `loop: true` on `stay`; `enter`, `exit`, and `play` default to one-shot (`loop: false`).

Example session action (fishing):

```json
{
  "id": "fishing",
  "type": "activity",
  "activityKind": "session",
  "sheetId": "fishing",
  "movementDriven": false,
  "phases": {
    "enter": { "animationName": "cast", "loop": false },
    "stay": { "animationName": "fishing", "loop": true },
    "exit": { "animationName": "exit", "loop": false }
  }
}
```

Example single action (water plants):

```json
{
  "id": "water",
  "type": "activity",
  "activityKind": "single",
  "sheetId": "water",
  "movementDriven": false,
  "phases": {
    "play": { "animationName": "water", "loop": false }
  }
}
```

Not yet validated in SPMK or loaded by C++ (`SpriteSheetAnimator` only toggles idle/walk today). Author via debug export JSON until tooling ships.

### metadata.itemApi (objects from PokéAPI)

Filled by batch import or future single fetch. No image URLs.

| Field | Type | Description |
|-------|------|-------------|
| `kind` | `string` | `"item"` |
| `itemId` | `number` | PokéAPI item id |
| `slug` | `string` | e.g. `master-ball` |
| `category` | `string` | e.g. `standard-balls`, `healing` |
| `cost` | `number` | Shop price |
| `attributes` | `string[]` | `consumable`, `holdable`, etc. |
| `shortEffect` / `effect` / `flavorText` | `string` | English text |
| `heldByPokemonCount` | `number` | Count only (full list omitted) |

---

## Sprite profiles (built-in)

Defined in `spmk_app/data/sprite_profiles.json`. Referenced by `baseProfile` and `spriteSheets[].profile`.  
**Game config** may mirror these in `pokemon-resort/config/gameplay/sprites/sprite_profiles.json` — keep them aligned when changing grid layout.

### Profile keys

| Key | Use |
|-----|-----|
| `character` | Trainers / NPCs / playable humans |
| `pokemon_small` | Small Pokémon overworld |
| `pokemon_large` | Large Pokémon overworld |
| `object` | Map objects (non-moving props) |

Pokémon profiles define `walk` (4 frames) and `pause` (frame 0 only). They do **not** define a profile `idle` key — the package `idle` action points at `walk`.

**Object** profile uses a 4×4 grid (32×32 cells, same upload scaling as characters) but only **row 0 / south** is used. Animations: `static` (frame 0), `play` (row-major cells, non-looping). No movement actions.

### Pokémon batch import (species, forms, animation layers)

**One `.charbin` per species** — alternate appearances and animations merge into the same file (e.g. all 64 Alcremie decorations × shiny × swim).

| Field | Description |
|-------|-------------|
| `animationVariant` | Optional layers: `shiny`, `swim`, `eating` (space/comma/`_` separated). Combined with filename. |
| `importMode` | `create`: replace whole package only for plain base import (`SPECIES.png`, no form, no layers). `add`: merge base `walk` only. |

**Filename parsing** (stem before `.png`):

| Pattern | Species id | Sheet suffix | Example sheet id |
|---------|------------|--------------|------------------|
| `PSYDUCK` | `psyduck` | (none) | `walk` |
| `GARCHOMP_female` | `garchomp` | `female` | `walk_female` |
| `ARCEUS_1` | `arceus` | `1` | `walk_1` |
| `ALCREMIE_42` | `alcremie` | `42` | `walk_42` |
| `ALCREMIE_42_shiny_swim` | `alcremie` | `42_shiny_swim` | `walk_42_shiny_swim` |
| `PIKACHU` + UI `shiny eating` | `pikachu` | `shiny_eating` | `walk_shiny_eating` |

Layer order in suffixes is always **form → shiny → swim → eating**. `swimming` → `swim`, `eat` → `eating`.

**Persist rules:**

- Any import with a **form** or **animation layer** merges into existing `{species}.charbin` (never replaces other sheets).
- Plain base + `create` replaces the file; plain base + `add` updates `walk` only.
- Missing species + form/layers creates a package with only those sheets (no assumed base walk).

**Metadata** (extension, under `metadata.custom`):

| Key | Description |
|-----|-------------|
| `overworldSpriteKeys` | Sorted list of sheet suffix keys (`default`, `42`, `42_shiny_swim`, …). |
| `overworldFormIds` | Form ids seen in filenames (`1`, `42`, `female`, …). |
| `bodyMarkers` | Accessory / sleep anchor boxes on the **base walk** sheet (pause frame, column 0) per facing. See shape below. |

**`bodyMarkers` shape** (SPMK Body markers editor):

```json
{
  "version": 1,
  "frameWidth": 32,
  "frameHeight": 32,
  "directions": {
    "south": {
      "head": { "x": 8, "y": 2, "w": 16, "h": 12 },
      "eyes": [{ "x": 10, "y": 6, "w": 4, "h": 3 }, { "x": 18, "y": 6, "w": 4, "h": 3 }],
      "hands": [{ "x": 4, "y": 20, "w": 5, "h": 6 }, { "x": 23, "y": 20, "w": 5, "h": 6 }]
    },
    "west": { "head": {}, "eyes": [], "hands": [{}] },
    "east": { "head": {}, "eyes": [], "hands": [{}] },
    "north": { "head": {}, "eyes": [], "hands": [] }
  }
}
```

Coordinates are pixel rects inside one walk cell (`frameWidth` × `frameHeight`). Front/back (`south`/`north`) allow two eyes and two hands; side facings allow two eyes and **one** hand (the visible arm).

### Profile shape

```json
{
  "frameWidth": 32,
  "frameHeight": 32,
  "columns": 4,
  "rows": 4,
  "directions": {
    "south": { "row": 0 },
    "west":  { "row": 1 },
    "east":  { "row": 2 },
    "north": { "row": 3 }
  },
  "animations": {
    "idle": { "frames": [0], "frameTimeMs": 250 },
    "walk": { "frames": [0, 1, 2, 3], "frameTimeMs": 120 }
  },
  "rendering": { ... }
}
```

### Direction → row (standard 4×4 sheet)

| Profile direction | Sheet row | Legacy / UI label |
|-------------------|-----------|-------------------|
| `south` | 0 | down / `base_down` |
| `west` | 1 | left / `base_left` |
| `east` | 2 | right / `base_right` |
| `north` | 3 | up / `base_up` |

Walk animation uses **columns 0–3** on that row. Idle uses **column 0** only.

SPMK UI previews **idle** and **walk** for all four directions (8 loops total when a walk sheet is loaded).

### rendering (per profile)

| Field | Type | Description |
|-------|------|-------------|
| `worldHeight` | `number` | World-space height hint. |
| `spriteScale` | `number` | Draw scale. |
| `anchor` | `string` | e.g. `center`. |
| `worldOffset` | `[x,y,z]` | World offset. |
| `screenOffsetPx` | `[x,y]` | Screen pixel offset. |

---

## Validation

`POST /api/packages/validate` (draft or body) returns:

```json
{ "ok": true, "errors": [], "warnings": [] }
```

| Check | Severity |
|-------|----------|
| `schemaVersion` supported | error |
| Forbidden runtime root keys | error |
| `id` slug format | error |
| `baseProfile` known | error |
| Duplicate `spriteSheets[].id` / `assetId` | error |
| Missing `assetId` or missing embedded bytes | error |
| Unknown sheet `profile` | error |
| Animation frame/cell out of grid | error |
| Duplicate `actions[].id` | error |
| Action `sheetId` unknown | error |
| `character` without idle / walk actions | warning |
| `metadata.partnerPokemon` absent | warning |
| Action `animationName` not in profile or sheet override | warning |

---

## Minimal example (JSON only)

```json
{
  "schemaVersion": 1,
  "packageType": "character",
  "id": "cynthia",
  "displayName": "Cynthia",
  "internalName": "cynthia",
  "baseProfile": "character",
  "metadata": {
    "originGame": "",
    "characterType": "npc",
    "description": "Sinnoh Champion",
    "personality": [],
    "likes": [],
    "dislikes": [],
    "tags": [],
    "partnerPokemon": null,
    "extraPartnerPokemon": [],
    "custom": {}
  },
  "spriteSheets": [
    {
      "id": "walk",
      "name": "Walk",
      "assetId": "walk_png",
      "profile": "character"
    }
  ],
  "actions": [
    {
      "id": "idle",
      "type": "idle",
      "sheetId": "walk",
      "animationName": "idle",
      "movementDriven": false
    },
    {
      "id": "walk",
      "type": "movement",
      "sheetId": "walk",
      "animationName": "walk",
      "movementDriven": true
    }
  ],
  "dialogue": { "lines": [], "packs": [], "custom": {} },
  "relationships": [],
  "unlock": null,
  "custom": {}
}
```

Embedded assets: `{ "walk_png": <PNG bytes> }` — typically 128×128 (4×4 cells of 32×32).

---

## SPMK authoring API (summary)

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/api/packages/library` | List scanned `.charbin` files |
| GET | `/api/packages/library-thumb/{package_id}` | PNG crop of base_down / south row col 0 (library card) |
| POST | `/api/packages/delete/{package_id}` | Delete `{id}.charbin` from library |
| GET | `/api/packages/thumbnail?path=` | Same crop by absolute file path (legacy) |
| GET | `/api/packages/draft` | Current workspace draft |
| POST | `/api/packages/draft/new` | New draft `{ id, displayName }` |
| POST | `/api/packages/draft/open-path` | Open file into draft |
| PATCH | `/api/packages/draft` | Merge fields into draft package |
| POST | `/api/packages/draft/asset` | Upload PNG (`assetId`, file) |
| POST | `/api/packages/draft/add-sheet` | Upload PNG + merge sheet: `mode` (`primary` \| `replace_primary` \| `walk_variant` \| `custom_anim`), `label`, optional `walkSheetId`, `animKind` (`movement` \| `idle` \| `south_only`), `includeIdle`, `frameCount`, `frameTimeMs` — writes `spriteSheets[].animations` + `actions[]` |
| POST | `/api/packages/save` | Write draft to `{id}.charbin` |
| POST | `/api/packages/validate` | Run validator |
| POST | `/api/packages/delete/{package_id}` | Delete library file (preferred) |
| GET | `/api/packages/pokemon/lookup?q=` | PokéAPI autofill + fuzzy suggestion |
| GET | `/api/packages/item/lookup?q=` | PokéAPI item autofill (objects) |
| POST | `/api/packages/batch/import-sprites` | Multipart: `characterType`, `files[]`; Pokémon also `animationVariant` (optional), `importMode` (`create` \| `add`) |
| DELETE | `/api/packages/file` | Delete by `{ path }` in JSON body (legacy) |
| GET | `/api/packages/profiles` | Sprite profile definitions |

Draft workspace files (not in charbin): `workspace/package_draft.json`, `workspace/package_draft_assets/`.

---

## Debug export

`POST /api/packages/export/debug-loose` writes:

- `{id}.character.json` — package JSON with `debugAssetPath` on sheets
- `assets/{assetId}.png` — extracted PNGs

For inspecting packages without parsing binary.

---

## Related docs

- [README.md](../README.md) — run instructions and workflow
- [SPRITE_SHEET_WORKFLOW.md](./SPRITE_SHEET_WORKFLOW.md) — legacy workspace sheet pipeline (separate from charbin)
- [ANIMATION_MODEL.md](./ANIMATION_MODEL.md) — workspace project animations (legacy)

---

## Changelog

| Date | Change |
|------|--------|
| 2026-05-30 | Initial canonical schema doc (v1 binary + JSON, profiles, UI field rules). |
| 2026-05-31 | `characterType` simplified to `npc` / `player`; `personality` is `string[]`. |
| 2026-05-31 | Added `pokemon` type, PokéAPI lookup, sheet PNG/WebP 64→32 normalize. |
| 2026-05-31 | `metadata.pokeapi` snapshot; Pokémon idle→`walk`, `pause` action + profile anim. |
| 2026-05-31 | Pokémon batch: `animationVariant` + `importMode`; multi-sheet `walk_*` actions per species. |
| 2026-05-31 | Pokémon batch: parse `female` / numeric forms + combinable `shiny`/`swim`/`eating` layers; `overworldSpriteKeys`. |
| 2026-06-12 | Add-sheet: general animation sheet flow (`custom_anim`) for all character types; Pokémon walk variants stay on batch import. |
| 2026-06-12 | Proposed `type: activity` actions with `activityKind` and `phases` (single play and enter/stay/exit sessions). |
