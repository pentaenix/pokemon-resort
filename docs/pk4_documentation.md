# Pokémon Gen 4 Overworld Technical Documentation


## 1. Source confidence

The strongest source for **Pokémon Platinum camera internals** is the public `pret/pokeplatinum` decompilation. It exposes the camera data structures, camera math, projection selection, clipping values, field camera presets, and target-follow behavior.    

The strongest source for **map-file organization** is Project Pokémon’s Gen 4 map-structure documentation, which describes Diamond/Pearl maps as four-part files containing movement permissions, 3D objects, an NSBMD model map, and BDHC height data. ([Project Pokemon Forums][1])

The strongest source for **dynamic camera RAM behavior** is the PokeHacking Gen IV dynamic-camera tutorial. It documents a “camera box” in RAM containing camera position, target position, camera up vector, and previous target values, and states that these camera-box parameters are the same in Platinum and HeartGold/SoulSilver. ([PokeHacking][2])

---

# 2. Core camera architecture

Platinum defines a `Camera` object with:

```text
perspective projection parameters
lookAt.position
lookAt.target
lookAt.up
distance
angle
projection mode
fovY
previous target position
tracked target pointer
per-axis tracking flags
optional position-history buffer
```

The camera supports both **perspective** and **orthographic** projection modes. The projection enum contains `CAMERA_PROJECTION_PERSPECTIVE` and `CAMERA_PROJECTION_ORTHOGRAPHIC`. 

Default clipping constants are:

```text
near clip = 150 * FX32_ONE
far clip  = 900 * FX32_ONE
```

These are defined as `CAMERA_DEFAULT_NEAR_CLIP` and `CAMERA_DEFAULT_FAR_CLIP`. 

The camera uses a look-at model. When computing the active view matrix, the engine calls `NNS_G3dGlbLookAt` with camera position, up vector, and target. 

---

# 3. Camera position math

When initialized around a target, the camera computes its world position from:

```text
target position
distance
pitch angle
yaw angle
```

The decompiled function `Camera_AdjustPositionAroundTarget` computes:

```text
position.x = sin(yaw) * distance * cos(pitch)
position.z = cos(yaw) * distance * cos(pitch)
position.y = sin(-pitch) * distance

position += target
```

In Platinum’s field camera table, pitch values are stored as negative angles. For the default camera, the stored pitch is approximately `-59.0515°`, so the derived Y offset is positive above the target.  

---

# 4. Camera projection behavior

For perspective projection, the camera calls:

```text
NNS_G3dGlbPerspective(
    sinFovY,
    cosFovY,
    aspectRatio,
    nearClip,
    farClip
)
```

For orthographic projection, the camera computes:

```text
top   = tan(fovY) * distance
right = top * aspectRatio
```

Then it calls:

```text
NNS_G3dGlbOrtho(top, -top, -right, right, nearClip, farClip)
```

The camera code therefore uses the stored `fovY` value as the angle used to compute the half-height of the view volume in the orthographic path. 

The default aspect ratio is:

```text
4 / 3
```

This is defined in `camera.c` as `CAMERA_DEFAULT_ASPECT_RATIO`. 

---

# 5. Platinum field camera presets

Platinum defines a table of field camera settings in `src/overlay005/field_camera.c`. Each entry contains:

```text
distance
cameraAngle
projection
verticalFov
nearPlaneDist
farPlaneDist
```

The default overworld camera is:

```text
projection: perspective
distance: 666.922119140625
pitch: -59.051513671875°
yaw: 0°
roll: 0°
verticalFov parameter: 8.0914306640625°
near clip: default 150
far clip: default 900
```



The documented Platinum camera presets include:

| Camera type                |   Projection |    Distance |       Pitch | FOV parameter |
| -------------------------- | -----------: | ----------: | ----------: | ------------: |
| Default                    |  Perspective |  666.922119 | -59.051514° |     8.091431° |
| Pastoria Gym               |  Perspective |  666.922119 | -68.367920° |     8.091431° |
| Zoomed In                  |  Perspective |  515.456055 | -54.656982° |    10.458984° |
| Canalave Gym               |  Perspective |  666.922119 | -59.051514° |     8.091431° |
| Interior Orthographic      | Orthographic | 1563.537842 | -50.086670° |     3.521118° |
| Spear Pillar               |  Perspective |  316.501221 | -59.046021° |    16.880493° |
| Mt. Coronet Exterior South |  Perspective |  866.554443 | -73.108521° |     6.333618° |
| Mt. Coronet Exterior North |  Perspective |  666.922119 | -59.046021° |     8.091431° |
| Stark Mountain Room 2      |  Perspective |  662.922119 | -70.471802° |     9.849243° |
| Oreburgh Gym               |  Perspective |  357.604492 | -40.588989° |    15.029297° |
| Veilstone Gym              |  Perspective | 1202.355713 | -60.803833° |     4.575806° |
| Slightly Zoomed Out        |  Perspective |  675.833252 | -57.815552° |     8.091431° |
| Cave                       |  Perspective |  574.577881 | -63.264771° |     9.497681° |
| Iron Island Cave           |  Perspective |  515.456055 | -47.796021° |    10.458984° |
| Hall of Origin             |  Perspective |  169.462158 | -78.376465° |    29.536743° |
| Lake Acuity                |  Perspective |  653.929443 | -54.656982° |     8.349609° |
| Unused 16                  |  Perspective |  330.921875 | -59.051514° |    15.474243° |



---

# 6. Default camera derived placement

Using the decompiled default camera constants and the camera-position formula:

```text
distance = 666.922119140625
pitch    = -59.051513671875°
yaw      = 0°
```

The camera offset from the target is approximately:

```text
x = 0
y = +571.97
z = +342.98
```

This is a derived value from the exact decompiled constants and formula, not a separately named ROM constant.  

The apparent full vertical FOV in a modern camera model is likely approximately:

```text
2 * 8.0914306640625° = 16.182861328125°
```

That statement is an interpretation of the decompiled projection math, because the original code stores sine/cosine of `fovY` and the orthographic path uses `tan(fovY) * distance` as the half-height. 

---

# 7. Camera follow behavior

The active camera can track a target position. During view-matrix computation, if a tracked target exists, the camera:

```text
1. subtracts previous target position from current target position
2. masks movement by per-axis tracking flags
3. optionally applies camera history delay
4. moves both camera position and camera target by the resulting delta
5. stores the current target as previous target
6. computes the look-at matrix
```



The field camera defines:

```text
FIELD_CAMERA_DELAY = 6
FIELD_CAMERA_HISTORY_SIZE = FIELD_CAMERA_DELAY + 1
```

When history is enabled for the field camera, it initializes camera history with a delay of 6 and the delay mask `CAMERA_DELAY_Y`. This means the documented field-camera history path delays Y-axis tracking, not X/Z tracking. 

---

# 8. Dynamic camera RAM behavior

The PokeHacking dynamic-camera research identifies a RAM region called the “camera box.” It contains camera position, target position, camera up vector, and previous target values. The tutorial states that the camera box contains enough values to define camera movement, and that the parameter properties are the same in Pokémon Platinum and HeartGold/SoulSilver. ([PokeHacking][2])

The documented camera-box fields include:

```text
Camera X
Camera Y
Camera Z
Target X
Target Y
Target Z
Camera Up Vector X
Camera Up Vector Y
Camera Up Vector Z
Target Previous X
Target Previous Y
Target Previous Z
```

The same source notes that camera values must be modified gradually frame by frame; direct replacement teleports the camera. ([PokeHacking][2])

---

# 9. Map file organization

Project Pokémon’s Gen 4 map-structure documentation describes Diamond/Pearl map files as being stored in:

```text
fielddata\landdata
```

It describes each map as divided into four parts:

```text
1. Movement Permission
2. 3D Objects
3. NSBMD Model Map
4. BDHC
```

The first `0x10` bytes of the map file contain section sizes:

```text
0x00 Permission Size
0x04 Object Size
0x08 Model Size
0x0C BDHC Size
```

([Project Pokemon Forums][1])

---

# 10. Movement permission data

The first map section represents movement permission. Project Pokémon documents it as a sequence of `XX YY XX YY ...`, where:

```text
XX = special permissions
YY = movement flag
```

The same document lists many permission/movement values, including free passage, grass, snow, bike-related movement, and interactable-object text behavior. ([Project Pokemon Forums][1])

The PokeHacking dynamic-camera tutorial states that the collision file loaded in RAM has a fixed size of:

```text
0x800 bytes = 2048 bytes
```

It describes this as corresponding to:

```text
32 tiles × 32 tiles × 2 permission layers
```

([PokeHacking][2])

The Platinum decompilation also defines:

```text
MAP_TILES_COUNT_X = 32
MAP_TILES_COUNT_Z = 32
TERRAIN_ATTRIBUTES_SIZE = 0x800
```



---

# 11. 3D object data

The second map section contains 3D objects shown in the map. Project Pokémon describes each 3D object record as a sequence of `0x30` bytes and lists fields for:

```text
model number
X coordinate
Y coordinate
Z coordinate
model width
model height
model length
```

([Project Pokemon Forums][1])

---

# 12. NSBMD model map

The third map section is the NSBMD model map. Project Pokémon identifies this section as the map’s NSBMD model data. ([Project Pokemon Forums][1])

In Gen 4 DS map workflows, NSBMD is the Nintendo DS 3D model format used for map model data. PDSMS explicitly supports exporting `.nsbmd` files, requiring `g3dcvtr.exe` and `xerces-c_2_5_0.dll` in its converter folder for NSBMD export. ([GitHub][3])

---

# 13. BDHC terrain-height data

The fourth map section is BDHC. Project Pokémon states that BDHC is currently known to have two parts:

```text
Part 1: controls base height of the entire map
Part 2: exists only in maps with changes in height, such as stairs
```

([Project Pokemon Forums][1])

The PokeHacking dynamic-camera tutorial also discusses BDHC as a map section loaded into RAM and describes modifying BDHC sizing when adding custom camera data to the end of BDHC. ([PokeHacking][2])

---

# 14. Map object coordinate system

Platinum defines:

```text
MAP_OBJECT_TILE_SIZE = 16 * FX32_ONE
```



The map-object header defines conversion macros:

```text
MAP_OBJECT_COORD_CENTER_TO_FX32(coord)
MAP_OBJECT_COORD_EDGE_TO_FX32(coord)
```

The center macro corresponds to:

```text
(coord << 4) * FX32_ONE + half tile size
```

Since `coord << 4` is `coord * 16`, map-object grid coordinates convert to world-space positions using 16 world units per tile. 

---

# 15. Map object state

The `MapObjectSave` structure contains persistent or serializable object data, including:

```text
status
localID
movementType
movementRangeX
movementRangeZ
initialDir
facingDir
movingDir
mapID
graphicsID
trainerType
flag
script
initial X/Y/Z
current X/Y/Z
```



The map-object API exposes functions for:

```text
setting and reading local ID
map ID
graphics ID
movement type
trainer type
script ID
initial direction
facing direction
moving direction
movement range
current tile behavior
previous tile behavior
current X/Y/Z
previous X/Y/Z
world position
sprite jump offset
sprite position offset
sprite terrain offset
```



---

# 16. Elevation and bridge-related object state

The map-object API includes flags and accessors for:

```text
height calculation disabled
do not sink into terrain
elevated bridge status
dynamic height calculation enabled
```

These names indicate that object height can be calculated dynamically from terrain/map state, and that bridge/elevated-terrain state is represented separately from ordinary flat tile occupancy. 

---

# 17. Player movement control

The player movement function `PlayerAvatar_MoveControl` takes:

```text
PlayerAvatar
LandDataManager
direction
held key state
pressed key state
additional boolean parameter
```

It calculates facing direction from input when direction is not explicitly provided, handles cycling gear changes, checks whether movement can start, initializes movement, requests avatar state changes, checks tile/terrain behavior, and plays walking sound effects. 

The same file defines cardinal direction offset tables containing `-1`, `0`, and `+1` offsets for X/Y/Z movement. This matches a grid/cardinal-direction movement model. 

The decompiled movement-control code demonstrates that Gen 4 player movement is handled as a state-machine process, not as free analog movement. 

---

# 18. Camera relationship to movement

The camera tracks a target `VecFx32`. The camera code itself does not contain walking, running, or biking logic. It follows changes in the tracked target position after the player/object movement system updates that target. 

The camera supports axis-specific follow behavior. The field camera’s history path uses a 6-frame delay on the Y axis when history is enabled. 

Exact walk/run/bike frames-per-tile were not established from the cited camera and map-object files.

---

# 19. Rendering composition

The documented Gen 4 map format contains:

```text
movement permissions
3D objects
NSBMD model map
BDHC height data
```

This establishes that map rendering and movement/collision data are stored separately. ([Project Pokemon Forums][1])

Map objects have graphics IDs, movement types, scripts, current tile behavior, previous tile behavior, world positions, and sprite offsets. 

The currently cited sources establish that overworld characters/NPCs are represented as map objects with sprite-related offsets and graphics IDs. The exact low-level billboard transform used to draw character sprites was not established from the cited files.

---

# 20. Interior vs exterior camera behavior

Platinum has an explicit camera preset named:

```text
CAMERA_TYPE_INTERIOR_ORTHOGRAPHIC
```

Its settings are:

```text
projection: orthographic
distance: 1563.537841796875
pitch: -50.086669921875°
yaw: 0°
roll: 0°
verticalFov parameter: 3.5211181640625°
near clip: default 150
far clip: 1735
```



The existence of this preset establishes that at least some Platinum field maps use orthographic projection, while the default field camera uses perspective projection. 

---

# 21. Special camera cases

Platinum’s camera table includes multiple non-default camera types for specific areas or map categories, including gyms, caves, Spear Pillar, Mt. Coronet exterior areas, Stark Mountain, Hall of Origin, and Lake Acuity. 

The camera system supports:

```text
setting FOV
adjusting FOV
setting distance
adjusting distance
setting angle around self
setting angle around target
adjusting angle around self
adjusting angle around target
setting target
setting position
tracking target
releasing target
```

 

This establishes that the engine has camera functionality beyond a single hardcoded fixed view, even though the normal overworld camera behaves as a target-following fixed-angle camera.

---

# 22. PDSMS / Pokémon DS Map Studio facts

Pokémon DS Map Studio is a tool for creating Gen 4 and Gen 5 Pokémon DS maps. Its README says it is designed to be used alongside SDSME. ([GitHub][3])

The tool does not require 3D modeling. It provides a tilemap-like interface that is automatically converted into a 3D model. ([GitHub][3])

The README states that the tool does **not** import maps from the original games and cannot modify them directly. ([GitHub][3])

The README lists supported games as:

```text
Pokémon Diamond/Pearl
Pokémon Platinum
Pokémon HeartGold/SoulSilver
Pokémon Black/White
Pokémon Black 2/White 2
```

([GitHub][3])

For exporting `.nsbmd` files, PDSMS requires:

```text
g3dcvtr.exe
xerces-c_2_5_0.dll
```

placed in the `bin/converter` folder. ([GitHub][3])

PDSMS release 2.2 added BIN map export. The release notes state that the BIN file can include:

```text
PER
BLD
BGS
BDHC
BDHCAM
NSBMD
```

and can be imported using DSPRE. ([GitHub][4])

The same release notes mention improved rendering performance, especially on large maps, through frustum culling. ([GitHub][4])

---

# 23. Established unknowns

The exact default camera constants for **Diamond/Pearl** and **HeartGold/SoulSilver** were not established from the Platinum decompilation. The dynamic-camera tutorial states that camera-box parameter properties are the same in Platinum and HGSS, but that does not establish identical default camera preset values. ([PokeHacking][2])

The exact frames-per-tile for walking, running, biking, stair traversal, and other movement states were not established from the cited files.

The exact low-level sprite billboard transform for Gen 4 overworld actors was not established from the cited files.

The exact per-map rules for choosing between default, interior orthographic, cave, gym, and special camera presets were not established from the cited files.

The exact BDHC binary format beyond the documented high-level two-part height description was not established from the cited sources. Project Pokémon documents BDHC as controlling base height and height-change data such as stairs, but the detailed binary schema is not fully described in the cited lines. ([Project Pokemon Forums][1])

[1]: https://projectpokemon.org/home/docs/gen-4/map-structure-r29/ "Map Structure - Generation 4 - Project Pokemon Forums"
[2]: https://pokehacking.com/tutorials/dynamiccameras/ "Dynamic cameras in Gen IV: How to implement and how to use them - PokeHacking"
[3]: https://github.com/Trifindo/Pokemon-DS-Map-Studio "GitHub - Trifindo/Pokemon-DS-Map-Studio · GitHub"
[4]: https://github.com/Trifindo/Pokemon-DS-Map-Studio/releases "Releases · Trifindo/Pokemon-DS-Map-Studio · GitHub"
