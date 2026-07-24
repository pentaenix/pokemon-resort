# Character packages (`.charbin`)

Game-ready character and Pokémon overworld definitions live here.

| Path | Purpose |
|------|---------|
| `playable/*.charbin` | Player / playable humans (e.g. Haru) |
| `npc/*.charbin` | NPC trainers and story characters |
| `pokemon/*.charbin` | Pokémon overworld species |
| `objects/*.charbin` | Map objects (signs, PCs, props — static or simple animation) |
| `CHARBIN_SCHEMA.md` | Binary/JSON schema for C++ (synced from `spmk/docs/CHARBIN_SCHEMA.md` by SPMK) |

Author packages in **SPMK** (Characters tab). Default save folder is this directory when `spmk` and `pokemon-resort` sit side by side in the repo.

Override folder: SPMK library sidebar **Change folder…**, or `SPMK_CHARACTERS_DIR` in the environment.

## 3D overworld test (local packages)

The **3D TEST** screen and bgfx follower need packages present on disk (often not committed). Minimum set:

- Player path from the loaded map metadata (e.g. `playable/haru.charbin`)
- `pokemon/psyduck.charbin` when using default `config/gameplay/followers/session.json`
- `objects/poke_ball.charbin` (or the configured `pokeballId`)

Missing packages log a warning and disable the follower; the player package must load for the screen to start.
