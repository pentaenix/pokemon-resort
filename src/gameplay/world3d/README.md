# World3D Runtime

Purpose: Gen 4-style overworld runtime modules used by the `RESORT -> 3D TEST` proof of concept.

Owned modules:
- `camera/`: follow camera preset and projection helpers.
- `characters/`: movement and sprite-sheet animation state.
- `data/`: JSON loading for scene and character definitions.
- `rendering/`: map projection + billboard sprite rendering.
- `events/`: world event bus adapters.

External dependencies:
- SDL2 renderer
- SDL2_image texture loading

Extension points:
- collision grid and permissions
- NPC controllers
- chunk streaming
- dialogue/event orchestration

Do not place save bridge or transfer persistence in this domain.
