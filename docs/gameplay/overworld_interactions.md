# Overworld Interactions

This document is the source-of-truth handoff for the Gen 4 overworld interaction prototype. It covers player-triggered interactions with NPCs and Pokemon in `RESORT -> 3D TEST`.

## Runtime Flow

Pressing Accept while facing an interactable target starts an interaction sequence instead of directly toggling the textbox.

Current default sequences live in `config/gameplay/world3d/interactions.json`:

- Pokemon: `facePlayer`, `pokemonInteractionSession`, `textFree`
- Characters: `facePlayer`, `textFree`

The screen adapter owns world effects only:

- lock the target so movement and idle behavior pause
- face the target toward the player
- start or stop Pokemon interaction activity animations
- open and close the overworld textbox
- release locks after the sequence and any Pokemon exit animation finish

Pure sequence and text selection rules live under `gameplay/world3d/interactions`. Do not add new interaction state machines directly to `Overworld3DTestScreen`.

## Behavior Actions

The reusable action vocabulary is:

- `facePlayer`
- `faceDirection` with `direction`: `up`, `down`, `left`, `right`, `north`, `south`, `east`, or `west`
- `faceAway`
- `textLiteral` with `text` or `value`
- `textFree`
- `cry`
- `emoticon`
- `pokemonInteractionSession`

`cry` and `emoticon` are reserved sequence actions in this pass. They intentionally advance without presentation until audio/VFX adapters exist.

## Text Selection

`textFree` for Pokemon asks the interaction text selector for a line from `config/gameplay/world3d/interaction_text.json`. `textFree` for a human NPC reads that actor's `dialogue.lines` list from its charbin instead; human-specific writing is not shared through the Pokemon catalog.

Each text entry has:

- `id`
- `body`
- `requiredTags`
- `weight`
- `globalCooldownSeconds`

Selection rules:

1. Build an interaction context from target kind and available Pokemon metadata.
2. Keep only entries whose `requiredTags` are all present.
3. Prefer the matching group with the most required tags.
4. Pick by weight inside that group.
5. Put the selected text id on a global in-memory cooldown.

Cooldowns are global for the runtime session, not per Pokemon. If one water Pokemon uses a specific pool line, another water Pokemon cannot immediately use the same text while that text id is cooling down.

Template variables available to authored text are:

- `{NAME}`
- `{SPECIES}`
- `{ROLE}`
- `{TILE}`
- `{PLAYER_NAME}`
- `{TYPE_PRIMARY}` / `{TYPE_SECONDARY}`
- `{NATURE}`
- `{FORM}`
- `{COMPANION_NAME}`
- `{NEARBY_POKEMON_NAME}`

`NAME`, `SPECIES`, type values, role, tile, and player name have runtime defaults in the prototype. Future data sources will fill the remaining values; missing variables render as an empty string. If no text matches, `textFree` falls back to an empty string so the existing empty textbox flow remains valid.

## Pokemon Interaction Animations

Pokemon targets currently drive a player-side charbin `activity` session while the textbox is active. The target Pokemon faces the player and stays locked, while Haru's player package plays the size-based interaction animation.

The action id is chosen from the target Pokemon's `metadata.pokemonSize`:

- missing, unknown, or `small`: `small_interact`
- `medium`: `medium_interact`
- `large`: `large_interact`
- `human`: no player-side activity; the sequence continues through its authored behavior actions

The player charbin action must be `type: "activity"` and `activityKind: "session"` with phases:

- `enter`
- `stay`
- `exit`

Runtime behavior:

1. Start Haru's matching `enter`.
2. Advance to looping `stay`.
3. Show or continue interaction text while Haru stays in `stay`.
4. On final Accept or Back, request `exit`.
5. Unlock the target after Haru's `exit` finishes.

If the player package lacks the required activity action, interaction still works: the target faces the player and the text sequence continues.

## Temporary Companion Source

Until the real spawn system and companion-selection UI exist, `config/gameplay/world3d/pokemon_spawns.json` selects the temporary Resort roster. `boxId` is zero-based, so `0` selects Resort Box 1. The first valid Pokemon in the selected box becomes the overworld follower. Remaining selected Pokemon spawn at random valid points as roaming Pokemon actors. `maxPokemon` caps the total selected Pokemon, including the follower. An empty box, `enabled: false`, or `maxPokemon: 0` produces no follower and no roster Pokemon.

## Extension Rules

- Add authored behavior choices to `interactions.json`.
- Add reusable Pokemon text to `interaction_text.json`; add human-specific text to the target character's charbin `dialogue.lines`.
- Feed future database, charbin, map, tile, or relationship data into `InteractionTextContext` instead of changing selector rules per scene.
- Keep text rendering separate from selection. The selector returns a string; textbox renderers decide how to draw it.
- Keep target-specific world changes in thin adapters such as `NpcActorDriver` and `FollowerController`.
