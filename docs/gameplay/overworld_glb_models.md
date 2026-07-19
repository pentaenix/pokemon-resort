# Overworld props — GLB

The Operations Desk map editor stores overworld props as **GLB** (glTF 2.0 binary). Preview uses **Three.js `GLTFLoader`** with the same material/texture bindings as standard online viewers.

## Import

Upload via **Import GLB…**:

| Input | Result |
|--------|--------|
| `.glb` file | Stored as-is |
| `.zip` containing one `.glb` | Extracted and stored |
| `.zip` with `.obj` + `.mtl` + textures | Converted server-side to GLB, then stored |

Output:

```
assets/overworld/models/<modelId>/
  <modelId>.glb
  model.json
```

## OBJ → GLB conversion (server)

1. Resolve `map_Kd` (or `<materialName>.png`) with Unicode-safe path matching.
2. Require authored OBJ `vt` on ≥85% of faces. UVs are preserved (no bake, normalize, or clamp) except for a **V flip** (`v → 1 − v`): OBJ texcoord origin is bottom-left, while glTF and the SDL renderer use top-left, so the flip makes UVs sample the same texels external OBJ viewers show.
3. **One glTF material per MTL material**; each gets its own embedded PNG/JPEG.
4. glTF **sampler**: nearest filtering, **repeat** wrap (DS UVs are often outside 0–1).
5. **Alpha** (`lib/texture-alpha.mjs`): each base-color PNG's alpha channel is **decoded** (inflate + unfilter) and measured. A material is exported `alphaMode: MASK` (with `alphaCutoff: 0.5`) only when a meaningful fraction (≥0.5%) of texels are actually transparent; otherwise it stays `alphaMode: OPAQUE`. This matters because DS rips ship RGBA PNGs whose alpha is *usually* fully opaque (so a naive "has-alpha-channel" check punched holes in roofs/walls) but where genuine cutout art — banners, signs, glass, lights — has ~⅓ transparent texels. The 0.5 cutoff matches DS 1-bit alpha and gives crisp edges, so the banner reads as a cutout instead of the black square that tools which flatten everything to OPAQUE produce. Combined with the V flip (step 2), opaque regions (e.g. the orange roof) keep full alpha and are never cut.

## Preview (browser)

- Loads the on-disk GLB with `GLTFLoader`.
- Clones cached scenes with Three's `SkeletonUtils.clone` through `model-scene-clone.js`, so skinned RAE/apicula props are rebound to their cloned bones and obey each map placement instead of rendering at the cached scene origin.
- Tunes existing materials: nearest + repeat on maps, double-sided faces. Alpha is taken verbatim from the GLB material (`alphaMode`); the preview does **not** re-derive transparency from the texture's alpha channel.
- Renders with neutral image-based lighting (`RoomEnvironment` IBL + ambient/directional, sRGB output, no tone mapping) so PBR materials read like online glTF viewers. Without lights/environment, `MeshStandardMaterial` renders black.

## Placement (map editor)

- In the **3D props** tab, pick a model and click **Place**, then click tiles on the paint grid. Each click adds an instance. While placing, a translucent **ghost** (footprint rect + top/roof image) follows the cursor so you can see the exact cells and shape before committing.
- Placed props highlight **every footprint cell** they occupy (catalog `footprintTiles`, w/d swapped for 90/270 yaw) and overlay the model's **top/roof snapshot** image over those cells, so the 2D grid reads as a real top-down layout rather than a single origin marker.
- A **2D / 3D** toggle in the tool rail switches the workspace. **2D** is the paint grid (above). **3D** is a *view-only* three.js scene (`map-3d-view.js`): a height-shaded terrain mesh built from the height/special grids plus every placed prop loaded as its **actual GLB**, under an orbit camera. Painting/placement stays in 2D.
- Model **orientation** can be re-baked in the GLB viewer modal (X/Y/Z 90° buttons + Save): the server applies the rotation to the GLB vertex data, re-centers/reseats it, and re-ingests, so models that import on their side/front (e.g. a truck) are corrected once at the asset level.
- Each placement is editable in the **Placed props** list: rotate 90°, scale ±, remove.
- Placements are stored in the `.owmap` metadata `models` array and persisted on **Save .owmap**.

### owmap `models` schema

```jsonc
"models": [
  {
    "id": "pokemon_center",                              // catalog id (folder name)
    "glb": "assets/overworld/models/pokemon_center/pokemon_center.glb", // path relative to the game project root
    "position": [worldX, worldY, worldZ],                // world units; tile center = (tileX+0.5)*tileSize, worldY = heightTiles*tileSize
    "yawDeg": 0,                                          // rotation about +Y, degrees
    "scale": 1.0                                          // uniform scale
  }
]
```

Models are stored once under `assets/overworld/models/<id>/` and referenced by path from each placement — adding the same prop many times costs only one placement record per instance.

## API

- List: `GET /api/overworld-models/list`
- Manifest: `GET /api/overworld-models/manifest?id=<modelId>`
- Asset: `GET /api/overworld-models/glb?id=<modelId>`
- Import: `POST /api/overworld-models/compile` (multipart: `glb` or `archive` zip)

## Game runtime (C++)

Placed props are loaded and rendered by the overworld screen:

- `gameplay/world3d/data/GlbModelLoader` — parses a self-contained `.glb` (JSON + BIN chunks via `parseJsonText`), walks the node graph baking world transforms into vertices, and produces a triangulated `GlbMesh` grouped per material with embedded base-color image bytes. Supports indices `u8/u16/u32`, `POSITION`/`TEXCOORD_0`, `baseColorFactor`, and `alphaMode`. External buffers/URIs are rejected (re-export self-contained).
- `gameplay/world3d/rendering/GlbModelRenderer` — uploads embedded textures lazily (`IMG_Load_RW`, nearest scaling, alpha blend when the material declares MASK/BLEND), applies the per-placement transform (scale → yaw about +Y → translate), projects via `Gen4FollowCamera::worldToScreen`, **clips each triangle against the integer UV grid** to emulate REPEAT wrap (see note), and draws with `SDL_RenderGeometry`. **Two-pass transparency order:** within a model the pieces are sorted so all *opaque* triangles draw first (back-to-front), then all *cutout* (MASK/BLEND) triangles draw last (back-to-front). Without a depth buffer, a cutout banner triangle nearly coplanar with the opaque wall behind it could tie/flip on centroid depth and let the wall paint over it ("cut-off" banner sections); forcing cutout faces last guarantees they composite over the body. The cutout flag comes from `GlbMaterial.alpha_blend`. It also exposes `anchorDepth()` (camera depth of the placement ground anchor) for scene-level ordering.
- `Overworld3DTestScreen` reads `SceneConfig.models` (parsed from the owmap `models` array; `glb` path with legacy `mesh` fallback) and builds one `GlbModelRenderer` per placement. **Render order:** it does *not* draw all models first; instead it depth-sorts the placed models together with the player and follower by their ground-anchor camera depth and draws far→near, so a character behind a building is correctly occluded by it. Each model's anchor depth is biased toward the camera by `SceneConfig.model_behind_bias_tiles * tile_size` (loaded from `config/gameplay/world3d/render.json`, key `occlusion.modelBehindBiasTiles`, default `1.0`) so a building starts hiding the character *before* they reach its footprint center — e.g. a character entering a doorway is occluded by the building instead of drawing over its roof. Larger value = occludes further forward.

Note: `SDL_RenderGeometry` *clamps* texcoords to 0–1 — it does **not** honour the sampler's `repeat` wrap — and DS UVs routinely exceed 1 (some faces span several tiles, e.g. a roof at `v≈1.15`). The renderer therefore clips every triangle to the integer UV grid (`clipAxis`/`emitPiece`) and re-bases each piece into 0–1, so each piece samples a single texture tile and the clamp is a no-op. This makes wrapping textures look identical to the glTF preview. Do **not** revert to passing UVs through unchanged or to a single per-triangle `floor` offset: the former black-stains props, the latter cannot fix multi-tile faces. `OverworldMapRenderer` solves the same problem for terrain via `wrapUv()`.
