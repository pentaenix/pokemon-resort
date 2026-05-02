## Transfer / Mirror Ideas (parking lot)

This file is **not** a committed roadmap. It’s a place to keep “future fun” ideas that relate to
the Transfer + Resort storage loop so they don’t get lost during implementation.

### Pokemon memories / story layer

- **Travel log**: record when a Pokémon is placed in a party and where (map/location id), as a “memory”.
- **Return notes**: on mirror return, add a memory entry like “Returned from Hoenn (Gen 3) at Lv 37”.
- **Achievements**: small canonical “did something great” badges (e.g., “First mirror trip”, “Reached Lv 50 in Gen 3”).
- **Ribbons/marks provenance**: store where a ribbon/mark was earned (game + timestamp + optional location).

### Mirror session UX

- **Lossy projection prompt**: when sending to an older generation, warn:
  - “This mirror can’t carry all information in that game; Resort will restore it when you return.”
- **Preview of changes**: list what will and won’t carry over (a “loss manifest” summary).

### Policy / rules to decide later

- Which fields count as **progression** (adopt from return) vs **identity** (restore/preserve)?
- How to handle **evolution** when the mirror game allows a different evolution path than the canonical origin.
- Whether to support **moves learned** in older games as “remembered moves” vs overwriting modern movesets.

