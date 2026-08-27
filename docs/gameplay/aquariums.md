# Aquarium Interiors

Aquariums are an optional, map-scoped world feature. A normal placed GLB supplies the tank transform, Aquarium Maker's navigation JSON supplies the authoritative water volume, and `config/gameplay/world3d/aquariums.json` supplies its population. Maps without a matching entry do not construct or update the module.

## Authoring

1. Put the tank GLB and its `*.navigation.json` in an ordinary model asset folder with `model.json`. This makes it discoverable by Map Studio.
2. Place the model in the OWMAP. The placement ID is the stable join key used by the aquarium config.
3. For tanks extending below an interior floor, add `interior.floorCutouts`. A `placementId` plus convex `localPolygon` cuts the procedural floor at the model's transformed installation boundary. The floor is triangulated around rounded or angled edges while retaining its original per-cell UVs; authored collision remains independent. Legacy tile rectangles remain valid.
4. Add the population to `aquariums.json`.

Aquarium Maker exports geometry at 16 model units per metre, matching Resort's
16 world units per cell. Tanks therefore use the ordinary placement scale of
`1.0`: one authored metre occupies exactly one map cell. For example, the large
tank's nominal 10 m × 4.8 m footprint occupies 10 × 4.8 cells. The same export
contract is written into the companion navigation JSON, so geometry, swimming
volumes, and floor cutouts agree without a per-aquarium scale workaround.

```json
{
  "pokemonScale": 0.3,
  "maps": [{
    "mapId": "aquarium12",
    "tanks": [{
    "placementId": "aquarium",
    "navigation": "assets/overworld/models/aquarium/aquarium.navigation.json",
    "seed": 87,
    "inspectionCamera": {
      "positionOffsetTiles": [0, 1.35, 1.15],
      "lookAtOffsetMeters": [0, 0.9, 0],
      "interactionReachTiles": 1.5,
      "smooth": 0
    },
    "pokemon": [{
      "species": "dewgong",
      "count": 1,
      "animation": "idle_default",
      "behavior": "wander",
      "speedMetersPerSecond": 0.55,
      "turnDegreesPerSecond": 120,
      "bodyRadiusMeters": 0.04
    }]
    }]
  }]
}
```

`species` is resolved case-insensitively by name against the existing Attend model catalog, preferring compiled `.glbz` assets. `form` optionally selects an Attend form ID such as `"00"`; rendering and physical bounds use the same form. `pokemonScale` is the one shared render scale for every species; Attend models retain their relative proportions, so changing it scales Dewgong, Clamperl, Kyogre, and future aquarium Pokémon together. `sizeMultiplier` is an optional per-entry exception and defaults to `1`.

`startingPositionMeters` optionally supplies an `[x, y, z]` origin in Aquarium Maker space. `verticalAnchor: "bottom"` derives Y from the navigation floor and the rendered Pokémon's lower bound, which is appropriate for stationary bottom dwellers. Invalid starts are replaced by a deterministic valid point rather than allowing geometry to escape the tank.

`behavior` is reusable rather than species-specific:

- `stationary` holds the validated start while its animation continues.
- `wander` selects deterministic waypoints throughout the exported water volume.
- `school` builds an automatically sized looping formation from the tank volume, staggers members around it, adds vertical variation, and applies local separation so members do not overlap.

Speed and minimum body radius use Aquarium Maker meters. The runtime expands that minimum using the selected Attend form's actual rendered bounds and the global scale, so making every Pokémon larger also tightens its valid swim space. A speed of `0` remains compatible with legacy configs and infers `stationary`. `walk`, `run`, `idle_default`, `idle_ground`, and `fly_flap` resolve through the Gen 1–7 Attend animation slots; raw animation names are also accepted.

Tank inspection uses the same interaction button as NPC/Pokémon talk. The player must face a tank inside `interactionReachTiles`. `positionOffsetTiles` is `[side, lower, closer]` relative to the existing follow-camera pose and `lookAtOffsetMeters` is tank-local Aquarium Maker space. This preserves the Gen 4 camera's valid clipping distance while making the focused view lower and closer. The camera remains above the authored installation floor and the target remains inside the water, so below-floor tanks cannot pull it underground. `smooth <= 0` snaps immediately; a positive value moves the camera at that many world units per second. Advance or Back returns to the normal follow camera.

To author an exact inspection pose in the running game, press `Q` to begin from the current camera, move with WASD/arrow keys, use Space/Shift for height, and left-drag to set yaw and pitch. Press `2` (or keypad 2) to print and copy a compact JSON camera capture. The record includes the active map, nearest aquarium placement, exact camera XYZ/yaw/pitch, and the player pose needed to reproduce the offset. Paste that record into the development task when promoting a capture into a preset.

The simulation operates in tank-local meters, respects polygon holes and vertical layers, and transforms actors through the placed model's position, yaw, scale, and exported units-per-meter. This includes navigation below the room floor. Aquarium Pokémon use the same Attend material presentation rules for default forms, eye sheets, eye compositing, texture wrapping, alpha classification, and culling; the aquarium renderer is not a separate texture interpretation.

Aquarium Maker rock variation is stored as glTF `COLOR_0`, not as a redundant bitmap. Kelp uses material base colors plus exported node-rotation `WaterSway` clips. The shared GLB renderer consumes both contracts in bgfx and SDL fallback, so the model and its animation also appear in Map Studio's exact preview.
