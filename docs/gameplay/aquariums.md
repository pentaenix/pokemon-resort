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
      "smooth": 640,
      "returnSmooth": 2400,
      "inspectionView": {
        "behindPlayerTiles": 11,
        "frontHeightTiles": 5.1543217,
        "sideHeightTiles": 4.4304316
      },
      "focusedView": {
        "standoffTiles": 9.6852147,
        "frontHeightTiles": 4.1543217,
        "sideHeightTiles": 3.4304316,
        "frontPitchDegrees": -10.4799919,
        "sidePitchDegrees": -10.7199888,
        "nearClip": 12
      }
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

`pokemonPresentation` is an aquarium-only lighting and color grade. Its `brightness`, `pokemonBrightness`, `saturation`, `contrast`, `ambient`, `directional`, `formShadow`, `lightDirection`, and `tint` fields use the same meanings as Attend. The committed defaults reproduce Attend's clear-scene Pokémon presentation. Values may be overridden at map level, are copied into aquarium actor draw submissions, and never modify shared textures or the lighting of tanks, glass, scenery, overworld sprites, or Attend itself.

`positionMeters` is the preferred readable position format: `{"x": -1.15, "y": 0.2, "z": -1.15}` in Aquarium Maker local metres. The older `startingPositionMeters: [x, y, z]` remains supported, and numeric strings are accepted. Invalid or missing coordinate fields reject a live edit instead of silently becoming zero. With `verticalAnchor: "bottom"`, Y is an offset above the lowest swim-volume layer. With `verticalAnchor: "floor"`, Y is an offset above the exported `coordinateSystem.floorLevelY`; this is the right anchor for shallow touch pools whose water layer sits above the physical floor. X and Z always remain tank-local. Positions that overlap exported glass or obstacle navigation are adjusted to a deterministic nearby navigable point and logged rather than allowing geometry to escape the tank.

Both the game and Map Studio watch `aquariums.json` every 250 ms. Saving a valid edit rebuilds only the aquarium simulation and prints every resolved actor world position; it does not reload the map or the tank models. Invalid JSON is rejected while the previous live setup remains visible.

`movementPlane: "floor"` uses the exported navigation polygon as a horizontal walkable surface. It is intended for shallow touch pools: the visible model can extend above the thin water layer while rock holes and other polygon holes remain impassable. In this mode `bodyRadiusMeters` is the contact/navigation footprint, so broad fins or a face-up silhouette do not force a visual scale exception. The default, `volume`, keeps full 3D swimming. `pitchDegrees` rotates both the rendered model and its measured vertical bounds; `-90` lays a forward-facing Pokémon face-up on the pool floor.

`behavior` is reusable rather than species-specific:

- `stationary` holds the validated start while its animation continues.
- `wander` selects deterministic waypoints throughout the exported water volume.
- `school` builds an automatically sized looping formation from the tank volume, staggers members around it, adds vertical variation, and applies local separation so members do not overlap.

Speed and body radius use Aquarium Maker meters. Volume swimmers expand the configured minimum using the selected Attend form's actual rendered bounds and the global scale; floor navigators use the explicit contact radius described above. A speed of `0` remains compatible with legacy configs and infers `stationary`. `walk`, `run`, `idle_default`, `idle_ground`, and `fly_flap` resolve through the Gen 1–7 Attend animation slots; raw animation names are also accepted.

Tank inspection uses the same interaction button as NPC/Pokémon talk. The player must face a tank inside `interactionReachTiles`. The first press enters an inspection view behind the player's approached tank face. `inspectionView.behindPlayerTiles` makes that change explicit instead of inheriting the very distant normal follow camera. Its front/side heights are measured above the tank installation floor. `positionOffsetTiles[0]` remains an optional lateral adjustment, and `lookAtOffsetMeters` is the tank-local target. Configs without `inspectionView` retain the legacy `[side, lower, closer]` follow-camera offsets. A second press enters the close focused view, where the player, follower, NPCs, overworld Pokémon, and their transient effects are hidden while aquarium Pokémon remain visible.

The approached face is captured before presentation begins, so side interactions still frame the correct side of the tank. During either inspection stage the player sprite is presented facing north regardless of approach direction. Exiting with Back or movement restores the exact pre-inspection facing before normal input resumes.

`focusedView` is generated from the placed tank's transformed navigation bounds, so the same settings support north, south, east, and west faces. `standoffTiles` measures outward from the nearest glass face; smaller values zoom the second focus closer. `frontHeightTiles` and `frontPitchDegrees` cover the tank's local front/back, while the `side*` fields cover its local left/right. `nearClip` applies only to the close view and its return transition, then the original Gen 4 near clip is restored. `wallClipRadiusTiles` cuts only default room-wall fragments extremely close to the focused camera; it does not clip the tank, floor, placed models, or Pokemon. This keeps a nearby room wall from occluding the tank without weakening normal overworld depth precision.

For shallow tanks whose water layer is no taller than one map cell, the close view targets the transformed midpoint of the water volume instead of applying the generic tall-tank pitch. Extremely thin volumes also collapse their look-at safety margins to that midpoint. This keeps touch pools centered in frame rather than aiming over the water and into the room.

`smooth <= 0` snaps immediately; a positive value moves the camera at that many world units per second. `returnSmooth` independently controls the faster return to gameplay. North-facing entry and focus use `smooth`; other approach directions snap, avoiding a camera sweep through room walls or floors. Back or any movement direction exits either inspection stage. Hidden overworld actors reappear immediately, the player moves on that same input, and the returning camera follows the moving player instead of locking controls or aiming at a stale position.

To author an exact inspection pose in the running game, press `Q` to begin from the current camera, move with WASD/arrow keys, use Space/Shift for height, and left-drag to set yaw and pitch. Press `2` (or keypad 2) to print and copy a compact JSON camera capture. The record includes the active map, nearest aquarium placement, exact camera XYZ/yaw/pitch, and the player pose needed to reproduce the offset. Paste that record into the development task when promoting a capture into a preset.

The simulation operates in tank-local meters, respects polygon holes and vertical layers, and transforms actors through the placed model's position, yaw, scale, and exported units-per-meter. This includes navigation below the room floor. Aquarium Pokémon use the same Attend material presentation rules for default forms, eye sheets, eye compositing, texture wrapping, alpha classification, and culling; the aquarium renderer is not a separate texture interpretation.

Tank placements can use the map's half-tile offset convention when an installation should sit through the middle of authored collision cells. `aquarium12` applies that offset consistently to its tanks; its 5×5 touch-pool footprint is blocked independently from its internal Pokémon navigation so the player cannot clip through the installation.

The demo gallery is 24×18 cells. Tank geometry remains at scale `1.0`; the room was compacted around the installations instead of shrinking them, and every authored collision footprint moved with its tank.

## Construction test route

The normal playable route now separates presentation from unrestricted builder
testing:

```text
Resort exterior → Aquarium Builder Lab
                         ├─ north doorway ⇄ aquarium12 authored gallery
                         └─ south doorway → Resort exterior
```

`aquarium12` retains its three authored tanks, Pokémon, collision, and rounded
floor cutout. The exterior aquarium door now enters `aquarium_builder_lab`, a
separate 24×18 shell-less interior with no authored models or floor cutouts.
The resort arrives through the lab's south doorway. The north doorway provides
an optional two-way route to the authored gallery; walking back out of the
gallery returns through that same north lab doorway. The south doorway returns
to the resort. The lab has a clear walkable centre aisle and
340 construction cells. The three-cell doorway lanes at both ends are
intentionally outside its construction mask so a saved tank cannot block
travel.

Construction documents remain map-scoped, so the gallery and lab use distinct
save files. Press Z on keyboard or Y on controller while standing on a yellow
allowed cell to enter construction. Walk around the reserved doorway lane before
pressing Z/Y. Construction changes to a close, north-oriented perspective view
only six degrees higher than the room's normal camera. The camera keeps a
close, readable working area instead of fitting the whole room. WASD/arrows,
D-pad/left stick, or the mouse move the grid cursor. It remains still while the
cursor is in the central zone and pans smoothly after the cursor crosses a view
edge; a stationary mouse at the edge continues scrolling the cells beneath it.
Keyboard/controller movement takes cursor ownership immediately, so an idle
mouse cannot pull the cursor back. A construction-only window follows the camera
along the near procedural wall so the shallower angle cannot occlude active
cells; it does not alter authored models or global render state.

The left rail owns construction commands. Selecting or reviewing a tank opens a
compact tray along the bottom for Height and Corners. Each property is a labeled
card with less/more arrows and a nonnumeric rail, not a row of dot buttons.
Mouse users click the arrows or use the wheel; keyboard/controller users focus a
property with Tab or LB/RB and change it with Q/E, brackets, or LT/RT.

Player-facing shape editing starts from a rectangle and uses Subtract. Move the
cursor onto an occupied cell and press A/Space/click to cut or restore it; press
X or click Apply to review the result. Cuts must reach an outside edge and may
not split the remaining tank, create an enclosed hole, or reduce it below the
minimum usable area. Existing L/U documents remain loadable and are converted
to an equivalent rectangular cut pattern only when Subtract is opened. Spatial
operations retain world-space gizmos: a centre handle moves a tank and its edge
and corner handles resize it.

Construction HUD layout and pointer hit testing use the same logical viewport,
including on scaled framebuffers. The yellow grid, picking surface, committed
tank, navigation, and population all use the schema-2 half-cell installation
offset on both horizontal axes. Grid tiles are inset so neighboring cells remain
visually distinct.

The player, follower, NPCs, and overworld effects are hidden while construction
owns the room. Exiting restores normal movement and teleports the player to the
reserved south-door circulation cell, facing into the room, so newly committed
geometry cannot trap them. The follower remains stowed through that teleport
and is summoned by its ordinary controller only after the player takes a new
step.

Each committed player tank carries kernel-derived water layers, dry tunnel
exclusions, and suggested spawns into the ordinary `AquariumSimulation`. The
temporary testing population policy currently contributes six schooling
Wishiwashi, one bottom-resting Clamperl, and one floor-wandering Pyukumuku to
each player-built tank. Geometry and simulation do not name those species. Fish movement, rendered
body clearance, layered/deep water, and tunnel avoidance therefore use the same
containment code as authored aquariums. Rebuilding or deleting player tanks
replaces only their simulated swimmers; authored populations remain live.
Player-built tanks also join the existing inspection-camera tank list after
load and after every successful construction command. Facing one and pressing
the ordinary interaction control enters the same inspection view; pressing it
again enters the close focused zoom. Camera bounds come from the generated
water volume, and deleting or resizing a tank refreshes the interaction target.

Aquarium Maker rock variation is stored as glTF `COLOR_0`, not as a redundant bitmap. Kelp uses material base colors plus exported node-rotation `WaterSway` clips. The shared GLB renderer consumes both contracts in bgfx and SDL fallback, so the model and its animation also appear in Map Studio's exact preview.

Aquarium Pokémon poses are sampled at 24 Hz into persistent GPU buffers and reused by both opaque and translucent passes. This preserves handheld-style animation timing while avoiding host-refresh-rate CPU skinning and transient GPU uploads for every actor. Decoded Attend models are also shared between bounds measurement and rendering, and measured bounds are cached until the source asset changes. Entering a tank map therefore decompresses/parses each configured species once rather than twice, while live position edits reuse the existing results.
