# Overworld Script Engine

The Script Engine provides one readable authoring format for overworld idle, interaction, and NPC behavior.

## Files

Scripts live under `config/gameplay/world3d/scripts/`. `script_catalog.json` lists each script path. Each script has an `id`, `kind` (`idle`, `interaction`, `npc`, or `door`), target gates, trigger, priority, weight, cooldown, optional `when` conditions, and an ordered `actions` list.

`when.allTags` requires every tag. `when.noneTags` rejects matching tags. `when.closeTo` contains a context tag and maximum tile distance. Missing facts fail conditions safely.

Scripts are selected by highest priority, then weighted random selection among tied candidates. A selected script observes its own runtime cooldown.

Pokemon interactions always enter this selection path. If selection yields no eligible valid interaction script, the interaction controller supplies the established face/session/free-text fallback. NPCs only enter catalog selection when their charbin has `metadata.npcInteractionMode: "scripted"`; legacy and `direct_dialogue` NPCs read their own charbin dialogue directly. Scripted NPC selection safely falls back to direct dialogue. The source boundary reserves this future precedence without requiring a charbin script id: runtime-assigned interaction script > NPC scripted-mode source > direct-dialogue fallback.

Returning from Pokemon Attend supplies the one-shot `AFTER_POKEMON_ATTEND` interaction context tag to the preserved Pokemon target. Scripts can gate on that tag to author return reactions. The default return script performs two shared `JUMP` actions separated by `WAIT`, then shows `{NAME} is really happy!`; literal `TEXT` actions use the same interaction template variables as free text.

## Supported Actions

`WAIT`, `FACE`, `MOVE`, `WANDER`, `JUMP`, `TEXT`, `TEXT_FREE`, and `POKEMON_INTERACTION_SESSION` are part of the original vocabulary. `CRY` and `EMOTICON` are recognized but rejected by validation until their presentation adapters exist.

Door scripts use trigger `MOVE_TOWARD` and add these ordered actions:

- `PLAY_TILE_ANIMATION` with `value: "open"` or `value: "close"`. Close plays
  the trigger-phase RTPKS animation in reverse unless the tile names a close clip.
- `TRANSITION_CLOSE` and `TRANSITION_OPEN`, using the same circular iris
  transition configuration as Attend.
- `TELEPORT_TO_LINK`, which resolves the trigger's link and destination anchor.
- `MOVE_PLAYER`, with `direction` and `tiles`, for the authored step out of a door.

Approaching a door first turns the player toward its trigger without starting a
movement step. The default enter script then opens the tile once, holds its open
pose, closes the iris, teleports, and opens the iris. The default exit script closes the iris, teleports, opens the iris, moves the
player south one tile, and closes the referenced exterior door tile.

The language is deliberately linear. Branches, loops, durable state, smart-object reservations, and multi-actor handshakes are future extensions.

## Compatibility

Follower idle selection uses `idle_follower_default` before handing execution to the existing nature planner. This keeps established jump timing, landing dust, terrain traversal, and return behavior intact. NPC `scriptId` values map to the existing static, idle-rotate, wander, patrol, and follow executors while those action adapters are generalized.

The Script Engine Operations Desk tool is the source for creating and validating scripts. Its Text database mode authors the shared Pokemon `TEXT_FREE` catalog with required tags, weights, global cooldowns, and an insertable template-variable tray. Human NPC dialogue belongs to each character charbin's `dialogue.lines`, not this shared catalog. The context preview explains tag and proximity matching; it does not simulate actors.
