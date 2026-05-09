# Terrain + Water — Reference Pattern Notes

Concept-only summary of architectural patterns extracted from the
external reference repositories during Phase 0. No code is copied or
quoted from the references. No external paths are recorded.

References, conceptual only:
- REFERENCE_OPENQ4 — id Tech 4 / Quake 4 lineage
- REFERENCE_LUMBERYARD — Amazon Lumberyard / CryEngine lineage

These patterns inform the Quasimodo-native architecture in
`.cursor/docs/terrain-phase0-audit.md` and the implementation rules
in `.cursor/rules/04-terrain-water-system.mdc`.

Cheaper models executing later phases should not need to re-read
the reference engines. This file captures everything Phase 1–9
needs.

---

## 1. Terrain organization

Pattern (REFERENCE_LUMBERYARD `Cry3DEngine` terrain module):

- The world is split into a regular 2D grid of sectors (chunks).
- Sectors are organized in a node tree; deeper nodes cover smaller
  areas. The tree depth is capped (16 levels in the reference; the
  Quasimodo target is 4 levels).
- Each sector owns:
  - A range of heightmap samples
  - A min/max range used for ray rejection and occlusion
  - A list of surface-type weights (splatmap channels)
  - One render mesh per LOD level (3 LODs in the reference)
  - Per-sector AABB in world space
- A separate `CHeightMap` exposes scalar height-and-normal lookups
  by world-unit coordinates and surface-type lookup at the same.

Adapted Quasimodo design:

- One flat 2D array of `terrain_chunk_t`. No node tree; the chunk
  count is small enough that a flat grid plus a per-chunk AABB
  frustum cull is sufficient.
- A single `terrain_heightmap_t` owns the heightmap, splatmap,
  optional mask textures, and a per-chunk min/max table for fast
  reject.
- LOD count is per-`.jungle` (default 4). One vertex format per
  LOD; index buffers are shared per LOD level.

Why simpler is better here: Q2RTX is a path tracer. The dominant
cost is BLAS instances in the TLAS, not raster batching. Tree-based
adaptive subdivision adds management cost without payoff.

---

## 2. LOD and morphing

Pattern (REFERENCE_LUMBERYARD CLOD-style):

- Adjacent sectors must differ by at most one LOD level. The
  per-sector LOD is updated by a distance-and-error metric.
- Geomorphing blends vertex height between LOD `n` and LOD `n+1`
  using a per-vertex morph weight, sampled in the vertex shader.
- Skirts (vertical strips) hide cracks between sectors of
  different LODs.

Adapted Quasimodo design:

- Per-chunk current and target LOD. Lock to neighbor difference
  ≤ 1 by clamping during the LOD update.
- Optional `lod_morph: true` in `.jungle` enables the per-vertex
  morph weight; otherwise pop transitions.
- Skirts are generated as edge geometry per chunk during chunk
  creation. They are part of the static chunk vertex/index data.
- BLAS implication: morph is evaluated in the vertex shader; the
  underlying BLAS geometry does not change with LOD. This avoids
  per-frame BLAS rebuilds.

---

## 3. Splatmap and material blending

Pattern (REFERENCE_LUMBERYARD `terrain_det_tex.cpp` and
`terrain_sector_tex.cpp` flow):

- Up to 8 surface layers, sourced from one or two RGBA splatmaps.
- A per-pixel surface-type ID indexes into a material table.
- Detail textures are per-layer high-frequency normal maps.
- A macro texture provides large-scale color variation.
- Final albedo blends the per-layer base color with detail and
  macro textures.

Adapted Quasimodo design:

- Two RGBA splatmaps in `.jungle` → up to 8 channels.
- A small `materials.json` per map binds each channel to a PBR
  material entry from the existing material registry.
- Contrast-based blending in the path-tracer rchit terrain branch
  (see `.cursor/rules/05-pbr-materials-lighting.mdc` for the exact
  formula).
- Detail and macro textures are optional fields under
  `terrain.detail_normals` and `terrain.macro_texture`.

---

## 4. Ocean and volume water

Pattern (REFERENCE_LUMBERYARD `WaterVolumeRenderNode`,
`WaterWaveRenderNode`, `CREWaterOcean`, `terrain_water_quad.cpp`):

- Ocean is a single height plane with FFT-style displacement and
  caustics, plus a fog plane that defines the underwater volume.
- Water volumes are arbitrary closed shapes with their own surface
  mesh and physics contour.
- Each volume carries fog color, fog density, fog plane, depth,
  caustic intensity/tiling/height, and a shadowing factor.

Pattern (REFERENCE_OPENQ4 `Model_liquid.cpp`):

- Liquid is a separate model type with a deformable surface mesh,
  rendered as a render entity inside the existing scene graph.

Adapted Quasimodo design:

- One ocean plane at `water.level`, clipped by water mask.
- Optional `water.volumes[]` for explicit volume bodies (Phase 6b
  stretch goal). Each volume carries fog, depth, normal map.
- Surface normals reuse `src/refresh/vkpt/shader/water.glsl`. The
  Phase 6 work is parameter wiring, not new shader code.
- Refraction reuses the existing `reflect_refract.rgen` path.
- Reflection: `reflection_mode == "rtx"` with
  `rtx_reflection_hook == true` is a TODO_RTX stub for a future
  pass. Until then, the path tracer's existing reflection handles
  it.

---

## 5. Patch surfaces (seam strategy)

Pattern (REFERENCE_OPENQ4 `idSurface_Patch` /
`Surface_SweptSpline`):

- A patch is a control-point grid (typically 3×3 cells) defining
  a Bezier patch surface.
- Subdivision is error-driven (`Subdivide`) or explicit
  (`SubdivideExplicit`), producing a regular triangle mesh.
- Generated normals and indexes are computed during subdivision.

Adapted Quasimodo design:

- `.patch` files use a simple control-point grid (bilinear is the
  default; bezier is allowed but optional).
- Tessellation runs once at load time on the CPU.
- Each patch becomes a static `VkBuffer` (vertex + index) and one
  static BLAS.
- Patches participate in collision when both `terrain_enable 1`
  and `terrain_collision 1` (Phase 7 wiring).
- `bsp_face_align: true` projects the top control row onto the
  nearest BSP face plane at load time, removing visible seams at
  the BSP boundary.

---

## 6. BSP coexistence

Pattern (REFERENCE_OPENQ4 RenderWorld + collision model):

- BSP areas are loaded by the engine; arbitrary entity-supplied
  meshes coexist as render entities with their own collision
  models.
- The collision system queries BSP and entity-collision separately
  and merges results.

Adapted Quasimodo design:

- BSP loads first via the existing
  `src/refresh/vkpt/main.c::R_BeginRegistration_RTX` path.
- Terrain loads second, only if `terrain_enable 1`. Terrain BLAS
  joins the TLAS as parallel instances.
- Collision dispatcher (Phase 7) wraps `CM_BoxTrace` /
  `CM_PointContents` at known call sites. When
  `terrain_collision 1`, it merges BSP trace and heightfield trace.
  When `terrain_collision 0`, it returns BSP trace unchanged.

---

## 7. Vulkan / RTX adaptation notes

There is no direct Vulkan/RTX pattern in the references that maps
to Q2RTX. The renderer-specific design is taken from existing
Quasimodo code, not external references. Specifically:

- BLAS instancing follows
  `src/refresh/vkpt/path_tracer.c::vkpt_pt_instance_model_blas`.
- Buffer staging follows the pattern in
  `src/refresh/vkpt/transparency.c` and
  `src/refresh/vkpt/vertex_buffer.c`.
- Init/destroy is registered in `vkpt_initialization[]` in
  `src/refresh/vkpt/main.c`, with the appropriate lifetime flag.
- Debug naming uses `vkSetDebugUtilsObjectNameEXT` from
  `src/refresh/vkpt/vk_util.c`.

External Vulkan references (Khronos samples, NVIDIA nvpro, Sascha
Willems) may be inspected later for SBT layout questions or
ray-tracing extension feature flags. They are not consulted in
Phase 1–4. If consulted later, the same audit-first / no-copy
rules from `.cursor/rules/07-external-reference-repos.mdc` apply.

---

## 8. What is intentionally not adopted

- CryEngine `_smart_ptr` reference counting and the
  `IRenderMesh` ownership model.
- CryEngine SVO global illumination integration.
- CryEngine vegetation system.
- Crytek FFT ocean (`CREWaterOcean`) — Quasimodo already has
  analytic / noise water.
- CryEngine `CTerrainUpdateDispatcher` job system — terrain loads
  once per map.
- id Tech 4 idMaterial shader stage scripting.
- id Tech 4 area portal sky.
- id Tech 4 shadow volume pipeline.
- id Tech 4 `idDeclManager` — Quasimodo uses its own material
  registry plus a small `materials.json` for terrain layers.

---

## 9. Final reminder

These notes are concept-only. No code is copied. No external file
paths are recorded. References stay outside this repository.
Implementation must adapt patterns to Q2RTX/vkpt architecture.
This repository's existing architecture wins.
