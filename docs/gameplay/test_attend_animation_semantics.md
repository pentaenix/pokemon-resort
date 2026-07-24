# TEST ATTEND Gen 1-7 Animation And Expression Semantics

This file is the working dictionary for Gen 1-7 Pokemon Attend animation semantics from the 3DS/Alola-style model set.

The source of truth at runtime is `config/gameplay/pokemon_attend/providers/gen1_7.json`, specifically `defaults.interactionAdapter`. This document explains why those defaults exist and which combinations are useful for interaction features.

## Eye Frames

RAE Gen 1-7/3DS eye sheets use a shared 8-frame expression layout for eye-named materials.

| Frame | Semantic | Use |
|---:|---|---|
| `0` | `normal_open` | Neutral idle, walking, running, default look. |
| `1` | `angry` | Annoyed reactions, bad petting, refusal. |
| `2` | `sick_hurt` | Hurt, faint lead-in, low-energy reactions. |
| `3` | `happy` | Good petting, affection, positive emotes. |
| `4` | `closed` | Blink, sleep, pet eye-close. |
| `5` | `sad` | Sad/no reactions. |
| `6` | `hit` | Impact frame when taking damage. |
| `7` | `padding_red_stain` | Padding/unused; do not intentionally use. |

Separate iris/pupil meshes are composited through the eye-white mask in the attend bgfx renderer. They render only for `normal_open`; every other authored eye-sheet frame replaces them so expressions such as `happy` do not overlap the normal pupils.

## Mouth Frames

Some Gen 1-7/3DS Pokemon, including Eevee-style models, use frame-sheet mouths rather than a separate mesh-mouth skeletal animation. These mouth sheets are driven separately from eye sheets so interactions can later mix body animations, eye expressions, and mouth expressions.

| Frame | Semantic | Use |
|---:|---|---|
| `0` | `closed_normal` | Neutral idle closed mouth. |
| `1` | `angry_open` | Open angry/annoyed mouth candidate. |
| `2` | `happy_open` | Open happy mouth candidate. |
| `3` | `sad_closed` | Closed sad mouth candidate. |
| `4` | `happy_open_wide` | Wider happy/open mouth candidate. Useful for stronger happy, vocal, or eating beats. |
| `5` | `unused_5` | Missing/unused; do not intentionally use. |
| `6` | `closed_narrow` | Narrow closed mouth variant. |
| `7` | `unused_7` | Missing/unused; do not intentionally use. |

## Skeletal Slots

These slots are observed from Gen 1-7/3DS Pokemon GLBs and should be treated as provider defaults, not hard game rules. Some Pokemon omit slots, and a few species swap meanings around locomotion/idle.

| Slot | Semantic | Notes |
|---|---|---|
| `slot4_00` | `idle_default` | Default idle. Flying Pokemon may stay airborne. |
| `slot4_01` | `emote_vocal` | Vocal/emote. Examples: Meowth cleans head; Charmander/Squirtle vocalize; Lapras wiggles fins/ears. |
| `slot4_03` | `jump_start` | Start jump. |
| `slot4_04` | `jump_loop` | During jump. |
| `slot4_05` | `jump_land` | Landing jump. |
| `slot4_08` | `attack_physical` | Physical/contact attack. |
| `slot4_12` | `attack_special` | Special/ranged attack. |
| `slot4_13` | `attack_unique` | Species-specific attack/action. |
| `slot4_16` | `reaction_hit` | Getting hit. |
| `slot4_17` | `reaction_faint` | Fainted/down pose. |
| `slot4_22` | `mouth_vocal` | Mouth-only open/close animation for mesh-mouth Pokemon. |
| `slot5_00` | `idle_ground` | Ground/water-level idle variant for some flying/floating Pokemon. Species-dependent. |
| `slot5_04` | `sleep_start` | Start sleep/lie down. May be useful reversed for wake-up later. |
| `slot5_05` | `sleep_loop` | Sleeping loop. |
| `slot5_06` | `reaction_shake` | Shake reaction. |
| `slot5_07` | special state | Seen on Magearna in ball; treat as species-specific until mapped. |
| `slot5_09` | `reaction_shake` | Alternate shake/no reaction candidate. |
| `slot5_12` | `emote_alt` | Alternate emote. |
| `slot5_16` | `emote_alt` / `emote_happy` fallback | Alternate emote. |
| `slot5_21` | `emote_happy` | Preferred happy pet emote for now. |
| `slot5_22` | `eat_start` | Start eating. |
| `slot5_23` | `eat_loop` | Eating loop. Some Pokemon only have this from the eat set. |
| `slot5_24` | `eat_end` | End eating. |
| `slot5_25` | `eat_peck` | Peck/bite-like food interaction. |
| `slot6_00` | `idle_ground` fallback | Locomotion idle variant; species-dependent. |
| `slot6_01` | `fly_flap` | Flap/glide for flying Pokemon that glide, such as Gligar. |
| `slot6_02` | `walk` | Walk cycle. |
| `slot6_03` | `run` | Run cycle. |
| `slot6_08` | `levitate_stop` | Stop levitating for levitating runners such as Kirlia/Gardevoir. |
| `slot6_09` | `levitate_start` | Start levitating for levitating runners such as Kirlia/Gardevoir. |

## Runtime Combos

Combos pair a semantic skeletal animation with eye and mouth expressions. Gameplay should trigger combo names instead of raw slots or raw frame numbers. Body animation combos are crossfaded as full-body overlays over idle; eye expressions can linger briefly after the body blend finishes.

| Combo | Animation semantic | Eye semantic | Mouth semantic | Timing | Current use |
|---|---|---|---|---|---|
| `pet_happy` | `emote_happy` | `happy` | `happy_open` | Requires `1.0s` pet, fades in `0.35s`, fades out `0.55s`, eyes linger `0.35s`; large Pokemon multiply fade times by `1.55`. | Triggered when a pet drag ends successfully for long enough. |

After the `1.0s` pet threshold is reached but before release, `pet_happy` can play a small ready cue with `readyAnimation = mouth_vocal`, `readyMouth = happy_open`, and low animation weight. This gives feedback that the pet has registered without spending the full happy emote until release.

Useful future combos:

| Combo | Animation semantic | Eye semantic | Mouth semantic | Notes |
|---|---|---|---|---|
| `pet_annoyed` | `reaction_shake` | `angry` or `sad` | `angry_open` or `sad_closed` | Bad petting or touching disliked zones. |
| `feed_start` | `eat_start` | `normal_open` | `happy_open` | First food contact. |
| `feed_loop` | `eat_loop` | `happy` | `happy_open_wide` | Eating while food is held. |
| `feed_end` | `eat_end` | `happy` | `happy_open` | Food consumed. |
| `sleep` | `sleep_loop` | `closed` | `closed_narrow` | Resting state. |
| `wake` | `sleep_start` reversed later | `normal_open` | `closed_normal` | Needs reverse playback support. |
| `hit` | `reaction_hit` | `hit` | `angry_open` | Impact feedback. |
| `faint` | `reaction_faint` | `sick_hurt` | `sad_closed` | Battle-style collapse. |

## Mouth Notes

Mouths are less uniform than eyes:

- Some Pokemon use skeletal or mesh-mouth animation, such as `slot4_22`.
- Some Pokemon share frame-sheet style metadata with eye sheets; those use the mouth frame dictionary above.
- Some Pokemon rely on the body animation alone.

TEST ATTEND now keeps mouth semantics separate from eyes. The current pet-happy path drives happy mouth frames when a mouth sheet is present, while mesh-mouth Pokemon can still use `mouth_vocal` animation as a low-weight ready cue. Feeding and vocalization should continue to ask for semantic mouth names instead of raw frame numbers.
