# Overworld Script Engine

The Script Engine provides one readable authoring format for overworld idle, interaction, and NPC behavior.

## Files

Scripts live under `config/gameplay/world3d/scripts/`. `script_catalog.json` lists each script path. Each script has an `id`, `kind` (`idle`, `interaction`, or `npc`), target gates, trigger, priority, weight, cooldown, optional `when` conditions, and an ordered `actions` list.

`when.allTags` requires every tag. `when.noneTags` rejects matching tags. `when.closeTo` contains a context tag and maximum tile distance. Missing facts fail conditions safely.

Scripts are selected by highest priority, then weighted random selection among tied candidates. A selected script observes its own runtime cooldown.

## Supported Actions

`WAIT`, `FACE`, `MOVE`, `WANDER`, `JUMP`, `TEXT`, `TEXT_FREE`, and `POKEMON_INTERACTION_SESSION` are part of the v1 vocabulary. `CRY` and `EMOTICON` are recognized but rejected by validation until their presentation adapters exist.

The language is deliberately linear. Branches, loops, durable state, smart-object reservations, and multi-actor handshakes are future extensions.

## Compatibility

Follower idle selection uses `idle_follower_default` before handing execution to the existing nature planner. This keeps established jump timing, landing dust, terrain traversal, and return behavior intact. NPC `scriptId` values map to the existing static, idle-rotate, wander, patrol, and follow executors while those action adapters are generalized.

The Script Engine Operations Desk tool is the source for creating and validating scripts. Its Text database mode authors the shared Pokemon `TEXT_FREE` catalog with required tags, weights, global cooldowns, and an insertable template-variable tray. Human NPC dialogue belongs to each character charbin's `dialogue.lines`, not this shared catalog. The context preview explains tag and proximity matching; it does not simulate actors.
