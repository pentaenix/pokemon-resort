# Aquarium Interiors

Massive aquarium residents use catalogue `presentation.scaleMultiplier: 0.9`:
Gyarados, Suicune, Lugia, Wailord, Milotic, Kyogre, Palkia and Dhelmise. This
reduces linear rendered dimensions and physical-clearance envelopes by 10%,
without rebaking models or changing other scenes. Existing stocking masks and
the fixed glass comfort margin remain conservative. Scale changes require an
updated physical-envelope source signature through the native envelope baker.

Large-cruiser movement (including curated Kyogre) enables continuous cruising:
turns retain at least 65% forward speed, turn rates are limited by body radius
and capped at 32 degrees/second, and vertical travel eases within 20 degrees.
Reversing-arc look-ahead checks navigation before publishing motion. If no safe
arc exists, the actor waits/replans without pivoting or bypassing glass.
Explicit authored movement configs can opt in with `continuousCruise: true`
alongside `forwardOnly: true`; player-tank profiles select it automatically.
Stocked Kyogre cruises at 0.585m/s (30% above its original 0.45m/s).
Remoraid uses the escort profile. Stocking assigns an explicit same-tank cruiser
host, preferring Kyogre; escorts inherit that host's cruise speed. Without an
eligible host, the existing simulation uses same-species schooling.
Hosts ignore their own escorts in crowd steering and movement gates; overlap
correction moves only the escort. Escorts still avoid hosts and other residents.
Cruise heading corrections ease over 0.8 seconds near the desired heading,
with the same eased turn law used by the swept-arc predictor.
If the normal-speed arc does not fit, cruising tries 65% and then 40% speed
while retaining forward translation and the same body envelope. Cached steering
retains that speed choice. Recovery replans instead of issuing a tiny escape
target that the cruiser cannot turn into; glass collision remains mandatory.

Kyogre additionally enables `habitatTour`: low/high/middle depth destinations,
completed when their depth band is reached rather than orbiting an exact point.
Sustained turns can bank the model up to 6 degrees, with expanded body clearance checked
before applying roll. Vertical goals are retained through avoidance turns and
level off at their destination. No camera-dependent posing or teleportation.

Kernel ABI 16 closes the below-floor tunnel navigation gap outside the glass
bridge footprint. Side-water layers meet the under-bridge water at the underside
of the glass; the dry corridor and glass itself remain excluded. Derived outputs
are rebuilt; tank design schema is unchanged. Maker WASM artifacts must be rebuilt
against ABI 16 before consuming this shared-kernel correction.

Room editing groundwork and pending live integration: [independent aquarium rooms](aquarium-rooms.md).

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
  "buildingPresentation": {
    "camera": {
      "enabled": true,
      "distanceBehindPlayerTiles": 22,
      "heightAbovePlayerTiles": 30,
      "nearClipTiles": 0.5,
      "farClipTiles": 128
    },
    "lighting": {
      "enabled": true,
      "brightness": 1,
      "tint": [0.9, 0.97, 1.08]
    },
    "tankLighting": {
      "waterAttenuationIntensity": 1,
      "waterSurfaceSpeed": 1
    }
  },
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

`buildingPresentation` is an aquarium-map-only camera and lighting override.
`distanceBehindPlayerTiles` moves the normal follow camera horizontally behind
the player and `heightAbovePlayerTiles` sets its vertical height; the runtime
derives the camera pitch and orbit distance, so tuning the view does not require
trigonometry. Optional `farClipTiles` extends how deeply that aquarium camera
can render; zero or omission preserves the map camera's far plane. The Builder
Lab uses 128 tiles so long player tanks remain complete while its override stays
out of the authored gallery and every outdoor map. The current values are slightly closer and lower than the shared
overworld camera. `lighting.brightness` and the RGB `tint` grade the aquarium
room and ordinary overworld actors. `tankLighting` independently grades
authored tank models, player-built tank meshes, and aquarium Pokémon so a dim
room can retain bright, readable exhibits. Its `spillColor`, `spillOpacity`,
and `spillReachTiles` produce a soft additive ring on the floor outside each
tank footprint; this is local aquarium geometry, not global bloom or a shared
shader-state change.
The block may be set once at the root for every configured aquarium map or
overridden inside an individual map entry. Maps without a matching aquarium
entry always retain their original camera and lighting.

`species` is resolved case-insensitively by name against the existing Attend model catalog, preferring compiled `.glbz` assets. `form` optionally selects an Attend form ID such as `"00"`; rendering and physical bounds use the same form. `pokemonScale` is the one shared render scale for every species; Attend models retain their relative proportions, so changing it scales Dewgong, Clamperl, Kyogre, and future aquarium Pokémon together. `sizeMultiplier` is an optional per-entry exception and defaults to `1`.

Player-built tanks start empty. Their saved rosters are resolved exclusively
through the approved-species catalogue; there is no tank-index-based testing
population or special Kyogre tank. Catalogue profiles may still use the shared
formation simulation, fixed-step navigation, animation desynchronization, and
runtime LOD systems. Source models, materials, and textures remain shared while
each visible actor owns only its small runtime pose buffer. Species assignments
remain policy-owned and do not enter tank geometry or navigation.

`pokemonPresentation` is an aquarium-only lighting and color grade. Its `brightness`, `pokemonBrightness`, `saturation`, `contrast`, `ambient`, `directional`, `formShadow`, `lightDirection`, and `tint` fields use the same meanings as Attend. The committed defaults reproduce Attend's clear-scene Pokémon presentation. Values may be overridden at map level and are copied into aquarium actor draw submissions. When `tankLighting` is enabled, its brightness and tint are composed into the actor presentation instead of the darker room light. This never modifies shared textures, overworld sprites, or Attend itself.

`positionMeters` is the preferred readable position format: `{"x": -1.15, "y": 0.2, "z": -1.15}` in Aquarium Maker local metres. The older `startingPositionMeters: [x, y, z]` remains supported, and numeric strings are accepted. Invalid or missing coordinate fields reject a live edit instead of silently becoming zero. With `verticalAnchor: "bottom"`, Y is an offset above the lowest swim-volume layer. With `verticalAnchor: "floor"`, Y is an offset above the exported `coordinateSystem.floorLevelY`; this is the right anchor for shallow touch pools whose water layer sits above the physical floor. X and Z always remain tank-local. Positions that overlap exported glass or obstacle navigation are adjusted to a deterministic nearby navigable point and logged rather than allowing geometry to escape the tank.

Both the game and Map Studio watch `aquariums.json` every 250 ms. Saving a valid edit applies the aquarium camera/light grade and rebuilds only the aquarium simulation; it does not reload the map or tank models. Invalid JSON is rejected while the previous live setup remains visible.

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

Kyogre's `BodyANeolant_Inc` and `BodyBNeolant_Inc` red-line overlays use the same
aquarium-only additive emission path, controlled by `haloBrightness` and
`fogRetention`. Body and eye materials remain normally lit; no model rebake,
dynamic lights, shadows or global shader changes are needed.
The dedicated `fs_aquarium_pokemon_pulse` program modulates only these overlays:
3s dark, 1.2s centre-outward illumination, 2s lit, 1.2s centre-outward extinction.
Model-local stripe bounds define the radial sweep; actor time advances it in both
ordinary and post-fog emission passes. Lanturn/Chinchou stay steadily emissive.

Aquarium-only bulb emission: the current Chinchou/Lanturn GLBZ exports omit
their glow-shell blend semantics. `AquariumPokemonEmission.hpp` identifies only
those model IDs and their `BodyNeolantInc` bulb and
`BodyChonchieNon`/`BodyChoncheNon` shell
materials (including shiny variants). Bulbs render self-lit; black-backed shells
use restrained additive blending without depth writes. Body and eye materials
retain normal exhibit lighting. Every draw sets its own lighting uniforms.
After bounded fog, the existing bulb geometry is redrawn with configurable color
retention and additive halo contribution, before glass/water. It uses the
original scene depth (LEQUAL, no depth writes), so walls, floors and nearer
animals still occlude emission. This is an artistic visibility floor through
murky water, not dynamic lighting or bloom. No new textures, shaders, lights,
shadows, or changes to Attend/outdoor rendering are involved.

Tune `pokemonPresentation.emission` in `config/gameplay/world3d/aquariums.json`
(restart the game to reload). Root values apply to all aquarium maps; a map's
`pokemonPresentation.emission` can override individual fields:

```json
"emission": { "bulbBrightness": 1.5, "haloBrightness": 1.1, "fogRetention": 0.75 }
```

`bulbBrightness` and `haloBrightness` range from 0 to 4; `fogRetention` ranges
from 0 (no post-fog restoration) to 1 (full restoration). These are independent
of room/exhibit dimming. Legacy omitted-field defaults are 1, 0.55 and 0.4;
the authored settings above strengthen the core, double the halo and retain
75% of the core through fog. Invalid/nonfinite values keep the inherited value.

The normal playable route now separates presentation from unrestricted builder
testing:

```text
Resort exterior → Aquarium Builder Lab
                         ├─ north doorway ⇄ aquarium12 authored gallery
                         └─ south doorway → Resort exterior
```

`aquarium12` retains its three authored tanks, Pokémon, collision, and rounded
floor cutout. The exterior aquarium door now enters `aquarium_builder_lab`, a
separate shell-less interior with no authored models or floor cutouts. The
Builder Lab uses the compact 24×18 layout after playtesting showed the expanded
54×33 room made navigation and tank composition unnecessarily diffuse. The
authored gallery remains 24×18.
The resort arrives through the lab's south doorway. The north doorway provides
an optional two-way route to the authored gallery; walking back out of the
gallery returns through that same north lab doorway. The south doorway returns
to the resort. Both rooms use eight-tile side/back walls, three-tile-high door
clearance, and a black lower front facade that hides below-floor tank geometry.
Each three-cell-wide doorway responds across its complete width; its movement
trigger is one row farther south than the original centre-only threshold.
The lab has a clear walkable centre aisle and
340 construction cells. The three-cell doorway lanes at both ends are
intentionally outside its construction mask so a saved tank cannot block
travel.

The 340-cell mask remains authoritative, but the yellow editor overlay only
builds and uploads the camera-local working window. Outside construction mode it
does not enumerate the mask at all. This keeps room size from multiplying
per-frame overlay work while cursor panning still exposes every buildable cell.

Player-built water uses a dedicated bounded fog composite at the DS-native
world resolution. After the opaque room, tank floor, and Pokémon render, the
generated water sides and surface delimit the affected pixels. The shader
reconstructs the visible scene point from depth, intersects that camera ray with
the tank bounds, and applies exponential extinction and absorption to only the
distance travelled through water between the viewing glass and the visible fish
or substrate. The response does not clamp at the nominal visibility distance,
so sand close to the glass stays light while the same material seen through more
water progressively darkens along a gentle exponential curve with no hard
transmission floor. Room air contributes nothing. Tank contributions
use cumulative premultiplied compositing, so overlapping silhouettes from
separate tanks cannot overwrite an earlier tank's attenuation.
Water surface and glass render afterward, and authored tanks and non-aquarium
rendering are unchanged.
Player-built glass has its own inexpensive shader. Broad front-facing panes stay
nearly clear, while grazing angles and rounded silhouettes catch a soft
silver-blue room reflection. A broad reflection remains visible on flat panes;
the combined opacity is capped at 18%. The reflection moves with the camera, with no
animated stripes or waterline markings. It uses the existing glass draw without
reflection textures, refraction, blur, or changes to shared transparent materials.
`buildingPresentation.camera.nearClipTiles` is aquarium-local; lowering it keeps
nearby swimmers visible from tunnel-height cameras without changing outdoor
depth projection. `buildingPresentation.tankLighting.waterAttenuationIntensity`
controls bounded fog from `0` (disabled) through `1` (baseline); larger values
shorten visibility while final opacity remains normalized.
The player-facing murkiness slider retains nine discrete ticks, resampled across
the useful original 0–5 response. Its final tick exactly matches the former
level 5, giving finer control without exposing the excessively opaque former
levels 6–8. Brightness stays in floating point through substrate and water
drawing so upper ticks do not saturate in packed mesh colors.
Player-built surfaces reuse only Black 2 open-ocean tile 3287's translucent
`sea_mizu1_1` upper layer. Its 240-sample, 30 Hz UV track stays
world-continuous; the opaque ocean base is not drawn. The aquarium-local
`tankLighting.waterSurfaceSpeed` scales that track from `0` (paused) to `4`.
One surface pass samples the layer twice after bounded fog: the primary follows
the authored motion and a softer sample runs in reverse. Their crossing
highlights add surface motion without the opaque Black 2 base or a second
murkiness layer.

Construction documents remain map-scoped, so the gallery and lab use distinct
save files. Press Z on keyboard or Y on controller while standing on a yellow
allowed cell to enter construction. Walk around the reserved doorway lane before
pressing Z/Y. Construction changes to a close, north-oriented perspective view
only six degrees higher than the room's normal camera. The camera keeps a
close, readable working area instead of fitting the whole room. WASD/arrows,
D-pad/left stick, or the mouse move the grid cursor. It remains still while the
cursor is in the central zone and pans smoothly after the cursor crosses a view
edge; a stationary mouse at the edge continues scrolling the cells beneath it.
The mouse wheel zooms this construction camera in and out. Height and depth
remain paired beside the move compass at floor level, so even a maximum-height
tank never moves its own lowering control outside the viewport.
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
exclusions, and suggested spawns into the ordinary `AquariumSimulation`.
Player-built tanks remain empty until stocked. Select a player tank in
construction mode and activate the circular fish icon in the top-left to open
the stocking overlay. Its Pokémon tab mirrors the Transfer screen: approved
species occupy 30-slot, six-column boxes on the left and the selected tank uses
the equally sized destination panel on the right. A/left click picks up the
focused species with the red Basic Move tool; dropping it on the tank stocks it.
X/right click removes a resident, while B/Y cancels a held sprite or closes the
overlay. Each change is an undoable transactional aquarium-design revision.
On the Metal/bgfx path, both the fish action and the complete stocking overlay
are composited in the same full-resolution presentation view as the other
construction controls. The panel raster is cached until focus, residents, or
viewport size changes, preserving sharp text and sprites without per-frame uploads.
Tank documents store only stable species IDs and counts. The catalogue supplies
the current model, presentation, behavior, habitat, and abstract capacity mask.
The capacity preview deterministically packs those masks into the tank grid and
draws each resident's sprite over its occupied cells. The grid remains an
overstocking visualization rather than a request for an in-world spawn position.
It is always a complete rectangle, including for concave tanks, and large
boards retain readable cells in an independently scrollable viewport. Packing
is automatic; the player never has to solve a placement puzzle.
The tank board uses the complete destination viewport and keeps large capacity
cells; mouse-wheel or vertical controller navigation pans deeper boards rather
than shrinking them into an unreadable overview.
The Exhibit tab intentionally removes both PC boxes. A compact top row selects
River, Swamp, Open Ocean, or Depths as the water/light color; separate discrete
sliders control tank brightness and murkiness without exposing numbers. The
final murkiness tick deliberately approaches opaque underwater fog. Sand,
Gravel, Moss, and Dirt cards beneath the sliders select flat, repeatable Black 2
floor materials directly from stable RTPKS tile IDs. Color, brightness,
murkiness, and substrate are independent authored tank values and every change
uses the normal transactional edit/undo path.

The nearby light spill uses the selected color, while a preset-local interior
grade plus the brightness control also tints and dims the tank's floor and Pokémon.
River is neutral; Swamp gives the sand a restrained green cast; Open Ocean cools
the interior; and Depths substantially lowers substrate and resident light so
near-black water reads as a genuinely dark exhibit rather than tinted glass over
bright contents. Older designs default to River, and authored aquarium tanks
continue using map-level lighting rather than player presets.
Initial population placement is deterministic and validates each model envelope
against both navigation and residents already installed in that tank. Ordinary
residents distribute through valid water, schools begin as separated local
groups, and configured followers are installed after their leader and begin in
its formation envelope even when the source roster listed them first. A resident
with no valid separated spawn is omitted rather than visibly intersecting
another Pokémon on room entry.
During simulation, the complete baked envelope remains authoritative for glass,
navigation, fit, and initial spawn separation. Fish-to-fish movement instead
uses a smaller catalogue-driven body core: tails, fins, and tentacles are soft
visual space rather than invisible walls. Predictive steering considers only
the three nearest relevant neighbours and is capped so it can bend a route but
cannot cancel locomotion. Only a deep core penetration rejects a move; a small
post-step correction separates intersecting cores when navigation permits.
Collision-blocked swimmers retain one avoidance direction long enough to steer
around a neighbor. Only a sustained blockage selects one new detour, preventing
forward-facing models from spinning toward a different random waypoint every
frame while preserving forward progress in crowded tanks.
The catalogue's `capacityClearance` policy adds one column and row to the
smallest normal footprints and two columns and rows to larger footprints;
Wishiwashi, Pyukumuku, Barboach, and maximum-size Wailord remain unexpanded.
Bottom crawlers use their rendered bounds to rest on the lowest water floor
even when no explicit spawn position is authored. All player-tank swimmers use
their measured horizontal envelope plus a small comfort band for navigation,
preventing their visible body from approaching the glass boundary.
Capacity and physical fit are separate gates. Every approved entry has a
native-baked envelope sampled across its selected movement and rest animations.
The stocking controller checks that envelope against the selected tank's real
layered navigation, curves, holes, vertical clearance, and glass comfort band.
Large cruisers additionally require a navigable turning and travel segment.
Unavailable cards remain visible but dim, with width, vertical, turning, or
full-grid pictograms rather than explanatory text. Simulation consumes the same
envelope, preventing stocking acceptance and spawning from disagreeing.
Corsola uses the catalogue's `timid-reef` profile: copies spawn at independent
valid bottom positions, rest for long randomized intervals, and make only short
local adjustments. A nearby Mareanie or Toxapex temporarily overrides that
rest state with a bounded flee maneuver; threat species and distances remain
catalogue data rather than simulation-specific Pokémon checks.
Bottom residents use catalogue activity windows rather than moving continuously.
Ordinary bottom crawlers pause between excursions. Shellder and Cloyster
override that baseline with very long rests and short backward relocation
bursts; Pyukumuku uses a gentler, more frequent move/rest rhythm. The
`benthic-rest-swimmer` profile gives Whiscash, Huntail, Gorebyss, Relicanth, and
Clawitzer longer lower-water swims followed by a validated return to the tank
floor, switching between the curated movement and idle animations at each phase.
Tentacool, Tentacruel, Frillish, and Jellicent use the catalogue's
`jelly-drift` profile. They remain near independent local anchors, alternate
gentle rises and descents, drift only a small distance sideways, do not pitch
like forward-swimming fish, and receive deterministic animation offsets so a
group does not pulse in lockstep.
Geometry and simulation do not name those species. Fish movement, rendered
body clearance, layered/deep water, and tunnel avoidance therefore use the same
containment code as authored aquariums. Rebuilding or deleting player tanks
replaces only their simulated swimmers; authored populations remain live.
Player-built flat sand reuses the exterior Black 2 sand tile (RTPKS tile 103,
material 8), repeats it once per aquarium cell, and applies a slightly
desaturated aquarium tint. `tankLighting.sandDarkening` independently darkens
that substrate from `0` (source brightness) to `1` (black), before the selected
player exhibit applies its own interior grade. The texture is loaded only for aquarium interiors
and safely falls back to the previous flat tint if the tile package is missing.
Murkiness maps to a non-linear visibility distance and maximum opacity. The
final ticks reduce visibility below one cell and approach opaque fog; lower
ticks preserve long clear sightlines. The shader clamps the segment at the
visible opaque surface or the tank exit, whichever comes first, so a near fish,
rear fish, substrate, and wall behind the aquarium receive predictably different
coverage. The selected exhibit water color is the asymptotic fog color. The default
aquarium presentation uses `waterAttenuationIntensity: 2.4`,
`waterSurfaceSpeed: 0.7`, and `sandDarkening: 0.18`.
Player-built tanks also join the existing inspection-camera tank list after
load and after every successful construction command. Facing one and pressing
the ordinary interaction control enters the same inspection view; pressing it
again enters the close focused zoom. Camera bounds come from the generated
water volume, and deleting or resizing a tank refreshes the interaction target.

Aquarium Maker rock variation is stored as glTF `COLOR_0`, not as a redundant bitmap. Kelp uses material base colors plus exported node-rotation `WaterSway` clips. The shared GLB renderer consumes both contracts in bgfx and SDL fallback, so the model and its animation also appear in Map Studio's exact preview.

Aquarium Pokémon poses are sampled at 24 Hz into persistent GPU buffers and reused by both opaque and translucent passes. This preserves handheld-style animation timing while avoiding host-refresh-rate CPU skinning and transient GPU uploads for every actor. Decoded Attend models are also shared between bounds measurement and rendering, and measured bounds are cached until the source asset changes. Entering a tank map therefore decompresses/parses each configured species once rather than twice, while live position edits reuse the existing results.
# Tank decoration controls

## Visitors

Procedural aquarium rooms use human packages from
`config/character_testing/characters.json`, with repeats allowed. Partner Pokémon,
authored activities and interaction scripts are not copied. Settings live in
`config/gameplay/world3d/aquarium_visitors.json`; restart the scene to reload.

Appearances use a once-shuffled round-robin: exhaust the available human packages
before repeating, for both initial residents and later arrivals. Adults walk at
`adultWalkSpeedMultiplier` (default 0.55) times ordinary walking speed, with animation
playback matched to movement. Faster child profiles are not implemented yet.
Both walking and idle visitor sprite rows are camera-relative at render time;
their world facing, path and animation frame remain unchanged during inspection.

- One visitor per 32 reachable dry walking cells, capped at 24 per room; initial
  occupancy is 65%, approximately half already watching exhibits.
- `watchMinSeconds`/`watchMaxSeconds`: 5–20 seconds in the shipped config. Dialogue pauses
  the stop. Unvisited tanks are preferred, then different viewing spots.
- `arrivalIntervalSeconds`: entrance arrivals every 35 seconds, subject to capacity.
  `exitChanceAfterWatching` and `roomChangeChanceAfterWatching` control departures
  and linked-room travel. Departures despawn without joining the overworld.
- Watching/idle visitors block their cell; traveling visitors yield using existing
  NPC movement/reservations. Oncoming occupied steps are never crossed. Watch spots
  avoid narrow corridors and doorway lanes; routes respect tanks and dry tunnels.
- Construction pauses visitors. Committed revisions invalidate routes/viewing spots,
  relocate trapped visitors and retire surplus residents when capacity shrinks.
- Identity, appearance and visited-tank history survive room travel in memory.
  Off-screen rooms pause walking. Incoming visitors queue at their linked doorway
  until admitted within capacity. Restarting the scene starts a new population.
- Visitor dialogue comes from `config/gameplay/world3d/aquarium_visitor_dialogue.json`
  (92 authored templates). Watching visitors use their current exhibit's successfully
  populated species names, decoration categories and tunnel presence; visitors between
  exhibits use general chatter. Recent templates are shared across visitors to reduce
  repetition, and exhausted pools reuse least-recent lines.
- Each visitor allows two initiated exchanges per aquarium visit, across linked rooms.
  Afterward they remain simulated/collidable but are not an interaction target. Leaving
  the aquarium resets conversation allowances; room travel, construction and subsequent
  stops do not. Restart the scene to reload the dialogue catalogue.

Room capacity, viewing-spot counts and departures/transfers use state-change logs.
Game-scale crowd yielding, dialogue and room arrival visuals require playtesting.

The player's follower also uses camera-relative walk/idle sprites. In aquarium
rooms its normal idle delay starts tank-interest behavior instead of nature scripts:
prefer unvisited viewing spots within twelve cells of the owner, watch for the
visitor-configured duration, then choose another spot or occasionally return behind
the owner (20% chance after a stop). Moving follows the owner's recorded cell trail,
including corners. The last movement target is retained while stopped; changing
facing alone neither moves the follower nor cancels its current exhibit visit.
An occupied trail target causes waiting/replanning, never an arbitrary side target.
Once idle facing an exhibit, the follower can deliberately join a free side viewing
spot (40% opportunity), facing the same tank. This is an idle viewing action only;
movement cancels it. Inspection uses the owner's original world facing, not the
camera-adjusted player sprite direction, to identify the shared exhibit.
Routes avoid current NPC cells/step reservations, the owner and doorway cells;
autonomous visits never transfer rooms or leave. No route means wait/replan, not
teleport. The aquarium override does not affect outdoor follower behavior.

## Tank inspection controls

Accept while facing a tank enters an over-the-shoulder view aimed along the
player's approach direction. Accept again frames the entire tank and keeps the
player visible. Foreground tanks obstructing the selected tank, tanks entirely
behind, or tanks whose footprint contains the camera (above or below its eye)
(including their residents/decorations/light spill/fog), and the room wall behind
the camera are temporarily excluded. Neighboring tanks at the same depth remain visible.
Mouse movement gently turns the whole-tank view without translating the camera.
Click a visible resident for close-up tracking: the camera approaches its glass
face but stops outside the tank bounds. A narrower field of view supplies the
remaining zoom for small or distant residents without entering the water.
Accept/Back restores the overview and its field of view, then
Accept/Back exits inspection. Movement retains the existing quick-exit behavior.
Map changes and exit restore normal rendering; simulation and saves are unchanged.

## Decoration controls

Entering general construction starts at the maximum supported zoom-out. New
decorations start at six quarter-scale ticks (1.5× the old default); existing
saved placements retain their sizes. Category icons use filled silhouettes.

Select a player tank and choose the coral icon beside stocking. Decoration mode
centers the selected tank above the tray at the normal construction camera angle and temporarily hides residents and
obstructing water/glass. Drag a preview from the lower tray into the tank and
release to place it, or click a preview to place it at the tank center when valid.
Small pointer jitter stays a click; dragging out and returning to the tray cancels.
Names are deliberately omitted. Scroll over the tray to
browse, or scroll over the tank to zoom. Circular rock, coral, plant and sparkle
icons above the tray filter the catalogue (Page Up/Down also changes category).
The dotted capacity row and height guide are no longer displayed.

Select a decoration, then drag its move, vertical-height, diagonal-size, or
rotation knob. Releasing ends the operation; invalid drops cancel that draft.
Lateral movement preserves height. The existing construction checkmark, cross,
trash, and history icons handle finish, cancel, deletion, undo, and redo. Finish
saves the arrangement transactionally; leaving with cancel discards unsaved
arrangement changes. Controller placement and editing remain available.

There are 25 decoration slots per tank. Overlap, burial and floating placements
are permitted within the tank; signs and decoration-aware fish navigation remain
out of scope. Saved transforms, not meshes, are authoritative.
