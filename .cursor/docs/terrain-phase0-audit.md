# Terrain + Water — Phase 0 Audit

Status: Phase 0 complete. No implementation code written.
Scope: deep engine audit + adapted plan for a Q2RTX-native, Lumberyard-inspired
outdoor terrain and water system.

References, conceptual only:
- REFERENCE_OPENQ4 — id Tech 4 patch/surface and BSP+geometry coexistence concepts
- REFERENCE_LUMBERYARD — CryEngine-derived outdoor terrain and water architecture

External references are read-only conceptual references. No code copied.
No external paths recorded. All repository paths below are repository-relative.

---

## 1. Files inspected in Quasimodo RTX

Project rules and skills:
- `.cursor/rules/00-project-prime-directive.mdc`
- `.cursor/rules/01-q2rtx-engine-safety.mdc`
- `.cursor/rules/02-vulkan-rtx-rendering.mdc`
- `.cursor/rules/03-rmlui-gui-system.mdc`
- `.cursor/rules/04-terrain-water-system.mdc`
- `.cursor/rules/05-pbr-materials-lighting.mdc`
- `.cursor/rules/06-git-vendor-dependency-rules.mdc`
- `.cursor/rules/07-external-reference-repos.mdc`
- `.cursor/rules/08-ai-workflow-audit-first.mdc`
- `.cursor/rules/rmlui-porting.mdc`
- `.cursor/skills/terrain-water-implementation/SKILL.md`
- `.cursor/skills/vulkan-rendering-review/SKILL.md`
- `.cursor/skills/external-reference-audit/SKILL.md`
- `.cursor/skills/vendor-sdk-hygiene/SKILL.md`
- `.cursor/skills/git-safe-commit-push/SKILL.md`
- `AGENTS.md`

BSP / map / collision flow:
- `src/common/bsp.c`, `inc/common/bsp.h`, `inc/format/bsp.h`
- `src/common/cmodel.c`, `inc/common/cmodel.h`
- `src/common/pmove.c`, `inc/common/pmove.h`
- `src/client/precache.c`, `src/client/main.c`
- `src/server/init.c`, `src/server/commands.c`, `src/server/main.c`,
  `src/server/world.c`, `src/server/save.c`, `src/server/mvd/parse.c`
- `src/client/predict.c`, `src/client/entities.c`, `src/client/locs.c`

Renderer / Vulkan / RTX:
- `src/refresh/vkpt/main.c` (frame loop, init/destroy table, swapchain,
  `R_BeginRegistration_RTX`, `R_RenderFrame_RTX`, `R_BeginFrame_RTX`,
  `R_EndFrame_RTX`)
- `src/refresh/vkpt/path_tracer.c`
  (`vkpt_pt_create_all_dynamic`, `vkpt_pt_instance_model_blas`,
   `vkpt_pt_create_toplevel`, `build_tlas`, `append_blas`)
- `src/refresh/vkpt/bsp_mesh.c` (`bsp_mesh_create_from_bsp`,
  `bsp_mesh_destroy`, `bsp_mesh_register_textures`)
- `src/refresh/vkpt/transparency.c` (particles/beams/sprites buffer
  pipeline, host-staging upload pattern)
- `src/refresh/vkpt/material.c` (PBR material registry,
  `load_material_file`, map vs global materials)
- `src/refresh/vkpt/textures.c`, `src/refresh/vkpt/uniform_buffer.c`,
  `src/refresh/vkpt/vertex_buffer.c`, `src/refresh/vkpt/vk_util.c`
- `src/refresh/vkpt/shader/water.glsl` (existing path-traced water
  surface shader fragment) and `src/refresh/vkpt/shader/global_ubo.h`
  (UBO layout) — read for context only
- `src/refresh/vkpt/draw.c` (raster overlays / stretch_pic / debug)
- `src/refresh/stb/stb.c` (stb_image / stb_image_resize2 / stb_image_write
  already integrated through `Z_Malloc` overrides)

Build system:
- `CMakeLists.txt`
- `src/CMakeLists.txt`
- `extern/CMakeLists.txt`
- `cmake/compileShaders.cmake`

Asset loading:
- `inc/common/files.h` (`FS_LoadFile`, `FS_FreeFile`, `FS_LoadFileEx`)
- `inc/common/zone.h` (`Z_Malloc`, `Z_Free`, `Z_TagMalloc`)
- `inc/common/cvar.h`, `inc/common/cmd.h`

UI surface (read-only context, no Phase 0 changes):
- `src/client/ui_rmlui.cpp`, `src/client/ui_rmlui.h`,
  `src/client/ui_rmlui_render_vk.cpp`, `src/client/ui_rmlui_input.cpp`,
  `src/client/ui_rmlui_system.cpp`

---

## 2. External references inspected

REFERENCE_OPENQ4:
- Top-level subsystems: aas, bse, cm, framework, idlib, MayaImport,
  renderer, sound, sys, tools, ui
- Collision: `cm/CollisionModel*` family
  (load, trace, contacts, contents, debug)
- Renderer: `renderer/tr_*` family
  (tr_main, tr_render, tr_subview, tr_trisurf, tr_backend),
  RenderWorld_*, Model_*, Material, ImageManager, Cinematic
- Patch surfaces: `idlib/geometry/Surface_Patch.{h,cpp}`,
  `Surface_SweptSpline.{h,cpp}`, `Surface_Polytope.{h,cpp}`
- Liquid concept: `renderer/Model_liquid.cpp`

REFERENCE_LUMBERYARD:
- Engine module: `Code/CryEngine/Cry3DEngine/`
- Terrain: `terrain.{h,cpp}`, `terrain_init.cpp`, `terrain_load.cpp`,
  `terrain_compile*.cpp`, `terrain_node.cpp`, `terrain_node_compile.cpp`,
  `terrain_sector.{h,cpp}`, `terrain_sector_render.cpp`,
  `terrain_sector_tex.cpp`, `terrain_sector_beach.cpp`,
  `terrain_sector_info.h`, `terrain_render.cpp`, `terrain_hmap.cpp`,
  `terrain_hmap_occlusion.cpp`, `terrain_light.cpp`,
  `terrain_det_tex.cpp`, `TerrainModifications.{h,cpp}`,
  `terrain_damage.cpp`
- Water: `terrain_water.h`, `terrain_water_quad.cpp`,
  `WaterVolumeRenderNode.{h,cpp}`, `WaterWaveRenderNode.{h,cpp}`
- Ocean render element: `RenderDll/.../CREWaterOcean.{h,cpp}`

Inspection was conceptual only. No content was copied. No external paths
are recorded in this report.

---

## 3. External references unavailable

None during this Phase 0 session. Both REFERENCE_OPENQ4 and
REFERENCE_LUMBERYARD were accessible as runtime-only inputs. Neither
location is recorded in any committed file.

---

## 4. Current Q2RTX world / map loading flow

Server side (`SV_Map` → `SV_SpawnServer` → `CM_LoadMap`):
- `src/server/commands.c::SV_Map` parses the map command
- `src/server/init.c::SV_SpawnServer` calls
  `CM_LoadMap(&sv.cm, expanded)` and stores the BSP in `sv.cm`
- `src/server/main.c` and `src/server/save.c` free the cm at shutdown
  with `CM_FreeMap(&sv.cm)`
- MVD demos use a parallel `CM_LoadMap(&mvd->cm, ...)` path in
  `src/server/mvd/parse.c`

Common collision model (`src/common/cmodel.c`):
- `CM_LoadMap` calls `BSP_Load(name, &cm->cache)` and runs
  `FloodAreaConnections`
- `CM_FreeMap` releases area / portal arrays and calls `BSP_Free`
- `CM_LoadOverrides` runs before `CM_LoadMap` and may load
  `*.ent` / `*.bsp` overrides via `FS_LoadFile`

Client side (`CL_PrepRefresh` → `R_BeginRegistration`):
- `src/client/precache.c::CL_PrepRefresh` is the entry point each level
- It calls `R_BeginRegistration(cl.mapname)` then registers entity
  models, pics, and skins, then `R_EndRegistration` runs
- For VKPT renderer, `R_BeginRegistration` is bound to
  `R_BeginRegistration_RTX` in `src/refresh/vkpt/main.c`
- `R_BeginRegistration_RTX`:
  - waits idle
  - resets `vkpt_fog`
  - loads `maps/default.cfg` and `maps/<map>.cfg`
  - destroys previous `bsp_mesh_world` and `bsp_world_model`
  - calls `BSP_Load("maps/<name>.bsp", &bsp)`
  - asserts `bsp->vis` (Q2RTX requires a vis'd BSP)
  - calls `bsp_mesh_register_textures(bsp)`
  - calls `bsp_mesh_create_from_bsp(&vkpt_refdef.bsp_mesh_world, bsp, name)`
  - calls `vkpt_light_buffers_create` and
    `vkpt_vertex_buffer_upload_bsp_mesh`
  - resets sky, bloom, tonemap, restir, lights
- `R_EndRegistration_RTX` calls `vkpt_physical_sky_endRegistration` and
  the `IMG_FreeUnused` / `MOD_FreeUnused` / `MAT_FreeUnused` cleanup

Implication for terrain:
- The single best client-side hook for `Terrain_LoadJungle(map)` is
  inside `R_BeginRegistration_RTX`, immediately after
  `bsp_mesh_create_from_bsp` and `vkpt_vertex_buffer_upload_bsp_mesh`.
- The cleanup mirror is inside the early section of the same function
  where the previous `bsp_mesh_world` is destroyed; a parallel
  `Terrain_Unload()` call must be added next to it.

---

## 5. Current renderer insertion points

Phase-by-phase command-buffer structure inside `R_RenderFrame_RTX`
(`src/refresh/vkpt/main.c` ~lines 3614–4153):

1. Frame setup, `vkpt_freecam_update`, `process_render_feedback`,
   `BSP_PointLeaf`
2. `vkpt_pt_reset_instances`, `vkpt_shadow_map_reset_instances`,
   `prepare_viewmatrix`, `prepare_entities`
3. World BLAS instancing block (BSP only; opaque, transparent, masked,
   sky, custom_sky):
   ```
   vkpt_pt_instance_model_blas(&vkpt_refdef.bsp_mesh_world.geom_opaque, ...);
   vkpt_pt_instance_model_blas(&vkpt_refdef.bsp_mesh_world.geom_transparent, ...);
   vkpt_pt_instance_model_blas(&vkpt_refdef.bsp_mesh_world.geom_masked, ...);
   vkpt_pt_instance_model_blas(&vkpt_refdef.bsp_mesh_world.geom_sky, ...);
   vkpt_pt_instance_model_blas(&vkpt_refdef.bsp_mesh_world.geom_custom_sky, ...);
   ```
4. UBO upload, transfer command buffer with light/IQM staging
5. Trace command buffer:
   `update_transparency`, `vkpt_uniform_buffer_copy_from_staging`,
   `vkpt_physical_sky_record_cmd_buffer`, `vkpt_instance_geometry`,
   `vkpt_pt_create_all_dynamic`, `vkpt_pt_create_toplevel`,
   `vkpt_pt_update_descripter_set_bindings`,
   `vkpt_shadow_map_render`, `vkpt_pt_trace_primary_rays`
6. Optional debug-line submit (`vkpt_debugdraw_*`)
7. Reflection / refraction / god rays / ASVGF gradient reproject /
   `vkpt_pt_trace_lighting`
8. Post pass: ASVGF / NRD / interleave / DLSS or TAA / bloom /
   tonemapping / FSR / readback copy / final blit

Insertion points for terrain/water:

- **Terrain RTX hook** (Phase 5+): inside the world-BLAS instancing
  block, after the existing five `vkpt_pt_instance_model_blas` calls,
  inside `if (bsp_world_model && render_world)`. A
  `Terrain_InstanceBLAS()` helper would emit one BLAS instance per
  visible terrain chunk, gated by `terrain_enable->integer` and
  `Terrain_IsLoaded()`.
- **Terrain raster pre-pass** (Phase 5 first deliverable): for early
  validation, terrain may be drawn into a debug overlay via the
  existing debug-line / debug command buffer path. This is for visual
  verification only; the production path is BLAS instancing into the
  TLAS, since Q2RTX is a path-traced renderer.
- **Water RTX hook** (Phase 6): at the same location as terrain BLAS
  instancing. Water is an additional BLAS (ocean plane and/or volume
  meshes), with material flags routed through the existing
  `MATERIAL_FLAG_WATER` / `MATERIAL_FLAG_FLOWING` machinery and the
  shader code in `src/refresh/vkpt/shader/water.glsl`.
- **Resize / swapchain destruction**: in
  `recreate_swapchain` / `vkpt_destroy_all(VKPT_INIT_SWAPCHAIN_RECREATE)`.
  Terrain swapchain-dependent objects (none required for BLAS-only
  path) are destroyed here; pure BLAS data persists across swapchain
  recreation and only needs reupload if the chunk geometry changes.
- **Pipeline reload**: in
  `vkpt_destroy_all(VKPT_INIT_RELOAD_SHADER)`. Any future raster
  terrain pipeline must register `terrain` and `terrain|` entries in
  the `vkpt_initialization[]` table in `src/refresh/vkpt/main.c`
  (~lines 244–281).

---

## 6. Current render order

Quasimodo RTX is a path-traced renderer. There is no traditional
opaque-then-transparent forward order. Instead, the world is converted
into BLAS instances, then primary rays / reflection / refraction /
indirect lighting passes evaluate the scene in the path tracer.

Effective execution order (from `R_RenderFrame_RTX`):

```
1.  Reset PT instance lists
2.  Instance world BLAS (opaque, transparent, masked, sky, custom_sky)
3.  Instance per-entity BLAS (handled in prepare_entities / update_transparency)
4.  Instance dynamic geometry (particles, beams, sprites, explosions)
5.  Build dynamic BLAS (vkpt_pt_create_all_dynamic)
6.  Build TLAS (vkpt_pt_create_toplevel + update_descripter_set_bindings)
7.  Shadow map render (god rays)
8.  Primary ray trace
9.  Reflection / refraction trace (multiple passes)
10. ASVGF gradient reproject
11. Path trace lighting (bounce rays)
12. Denoiser: ASVGF / NRD
13. Interleave
14. DLSS (or TAA fallback)
15. Bloom
16. Tonemapping
17. FSR (if not DLSS, not menu mode)
18. Readback copy
19. Final blit / present
20. UI pass via existing draw.c stretch_pic + RmlUi pipeline
```

Terrain insertion is at step 2/3 (world-class BLAS instances) plus
optional step 6 contribution. Water inserts at the same step. There is
no per-pass raster pipeline ordering decision required, because the
path tracer consumes the TLAS holistically.

The `02-vulkan-rtx-rendering.mdc` rule documents an idealized raster
ordering for `bsp_terrain` mode. That ordering is reserved for a
future raster-debug path and is not mandatory for the path-traced
production path.

---

## 7. Current Vulkan resource lifetime model

Init/destroy is centralized in `src/refresh/vkpt/main.c` via
`vkpt_initialization[]` (~line 244). Each entry has:

- name
- initialize callback
- destroy callback
- flags: `VKPT_INIT_DEFAULT`, `VKPT_INIT_SWAPCHAIN_RECREATE`,
  `VKPT_INIT_RELOAD_SHADER`
- `is_initialized` bookkeeping

Lifecycle paths:
- One-time startup: every entry initialized with `VKPT_INIT_DEFAULT`
- Swapchain recreate: entries with `VKPT_INIT_SWAPCHAIN_RECREATE` are
  destroyed/recreated (`recreate_swapchain` ~line 4162)
- Shader reload: entries with `VKPT_INIT_RELOAD_SHADER` are destroyed/
  recreated (`vkpt_reload_shaders` ~line 879)
- Shutdown: full destroy at engine shutdown

Per-frame Vulkan work uses a fixed pool of command buffers
(`qvk.cmd_buffers_transfer`, `qvk.cmd_buffers_graphics`) with
`vkpt_begin_command_buffer` and `vkpt_submit_command_buffer*`
helpers. No per-frame `vkCreate*` / `vkDestroy*` is allowed in the
hot path.

Terrain implication:
- A new `terrain` entry in the table will manage subsystem state and
  static resources; flags must be `VKPT_INIT_DEFAULT`.
- A `terrain|` entry (note the trailing pipe naming convention used
  for `pt|`, `draw|`, etc.) must own pipeline objects and be flagged
  `VKPT_INIT_RELOAD_SHADER`. Only required when raster pipelines are
  introduced (Phase 5+ if/when needed).
- BLAS for terrain chunks is built once per chunk (or on dirty
  rebuild) using the same `accel_struct_t` / `model_geometry_t`
  pattern as `vkpt_pt_create_all_dynamic`.

---

## 8. Current descriptor set / pipeline layout findings

- Descriptor sets are owned per subsystem.
- There is no global descriptor allocator.
- Existing path-traced descriptors are documented inside
  `vkpt_pt_create_pipelines` and `vkpt_pt_update_descripter_set_bindings`.
- The renderer uses `VK_KHR_acceleration_structure`,
  `VK_KHR_ray_tracing_pipeline`, and SBT-based dispatch
  (`vkpt_pt_trace_primary_rays`, `vkpt_pt_trace_lighting`,
  `vkpt_pt_trace_reflections`).
- Shaders are in `src/refresh/vkpt/shader/` and compiled by
  `cmake/compileShaders.cmake` invoked from `src/CMakeLists.txt`.
- Dual variants are produced for `.rgen` shaders: standard pipeline
  (`*.pipeline`) and ray-query compute variant (`*.query`).
- Existing `MATERIAL_FLAG_WATER`, `MATERIAL_FLAG_SLIME`,
  `MATERIAL_FLAG_LAVA`, `MATERIAL_FLAG_FLOWING` flags are evaluated in
  `path_tracer.rchit` / `reflect_refract.rgen` and consume
  `water.glsl` for normals and extinction.

Terrain implication:
- Terrain BLAS uses the same `model_geometry_t` interface that
  `vkpt_pt_instance_model_blas` consumes. No new descriptor layout is
  required for the BLAS path.
- Material lookup for terrain layers is the cleanest extension:
  define a new `MATERIAL_FLAG_TERRAIN_*` (or reuse generic PBR) and
  add a small terrain shading branch in `path_tracer.rchit` that
  reads splatmap weights from a per-chunk material table.
- Splatmap and heightmap textures register through the existing
  `IMG_Find` / image registration path; they appear as ordinary
  `image_t` entries in the global texture table accessed by shaders.

---

## 9. Current material / image loading path

- PBR materials live in `src/refresh/vkpt/material.c` /
  `src/refresh/vkpt/material.h`.
- Two namespaces: global materials and map materials, both deduped
  by `compare_materials` and `sort_and_deduplicate_materials`.
- Material files use a custom non-JSON text format
  (see `load_material_file`); not JSON.
- Material names look up a `pbr_material_t` with image references for
  base color / normal / roughness / emissive / etc.
- Images are loaded via `inc/refresh/images.h` and the stb-based
  loader in `src/refresh/stb/stb.c` (stb_image, stb_image_resize2,
  stb_image_write are already pulled in through `Z_Malloc` overrides).
- File IO uses `FS_LoadFileEx` / `FS_FreeFile` from `inc/common/files.h`.
- Memory is `Z_Malloc` / `Z_TagMalloc` / `Z_Free` from
  `inc/common/zone.h`.

Terrain implication:
- `.jungle` parsing must use `FS_LoadFileEx` to load the buffer and
  `FS_FreeFile` to release it.
- All allocations (parser scratch, chunk arrays, splatmap descriptors)
  must use `Z_TagMalloc` with a dedicated `TAG_TERRAIN` (define one)
  so leaks can be diagnosed via the existing zone reporting.
- Terrain material definitions (per-layer PBR) will reuse the existing
  `pbr_material_t` structure plus a small terrain-side
  `terrain_material_layer_t` index that points into the existing
  material registry by name. This avoids duplicating material storage.
- Heightmap / splatmap / water-mask textures load via stb_image
  directly (PNG, TGA, etc.) into staging, then upload to a
  device-local `VkImage`. No new image system required.

---

## 10. Current shader compilation path

`cmake/compileShaders.cmake` exposes `compile_shader(...)`.
`src/CMakeLists.txt` (~lines 346–427) compiles every shader listed in
`SRC_SHADERS` and `SRC_RT_SHADERS`. Each `.rgen` is compiled twice
(standard and `KHR_RAY_QUERY` define).

Outputs go to a generated `shaderbin/` directory and are loaded at
runtime by the renderer via `vkpt_load_shader` (helpers in
`src/refresh/vkpt/main.c` / `vkpt.h`).

Terrain implication:
- A new shader is added by appending its file to `SRC_SHADERS` (for a
  raster terrain debug pass) or to the path-traced shader chain
  through `path_tracer.rchit` (for production terrain shading).
- Initial validation of the terrain chunk grid is best done by
  visualizing chunks via the existing `vkpt_debugdraw_*` line API.
  That requires no new shader and no pipeline.
- Adding a true terrain raster pipeline (terrain.vert / terrain.frag,
  terrain_seam.vert / .frag) is allowed only in Phase 5b after the
  CPU mesh and chunk subsystem are validated.

---

## 11. Current path tracing / RTX insertion points

- TLAS instances are appended via
  `append_blas(g_instances, &g_num_instances, &accel, ...)` and
  `vkpt_pt_instance_model_blas(geom, transform, buffer_idx, ...)`
  inside `R_RenderFrame_RTX`.
- TLAS is built each frame in
  `vkpt_pt_create_toplevel(cmd_buf, idx, upload_info, weapon_left_handed)`
  (`src/refresh/vkpt/path_tracer.c` ~line 881).
- BLAS for dynamic geometry is built in
  `vkpt_pt_create_all_dynamic(cmd_buf, idx, upload_info)` (~line 677).
- Static BLAS for the world is built once per map load in
  `bsp_mesh_create_from_bsp` and `vkpt_vertex_buffer_upload_bsp_mesh`.

Terrain RTX hook strategy:
- Each terrain chunk is treated as a static-per-load mesh.
- One BLAS per chunk, built once at `Terrain_LoadJungle` time, freed
  at `Terrain_Unload`. No per-frame BLAS rebuild for unchanged chunks.
- Each visible chunk emits one `vkpt_pt_instance_model_blas` call
  per frame at the world-BLAS instancing site.
- LOD changes do not require BLAS rebuild if the LOD is implemented
  as a vertex-shader morph against a single base mesh; if LOD changes
  geometry on the CPU, the new chunk vertex buffer triggers a BLAS
  rebuild flagged via a dirty bit.
- Seam patches share the same lifecycle but are always full
  resolution and are tessellated once at load.
- Water (ocean plane) is one BLAS for the entire ocean if a single
  plane covers the world; volume water is one BLAS per volume mesh.

These insertions are TODO_RTX stubs in Phase 1–4 and become real in
Phase 5 (terrain) and Phase 6 (water).

---

## 12. Current collision insertion points

Player/predicted/server traces flow through:
- `src/common/cmodel.c::CM_BoxTrace` and `CM_PointContents`
- Server side: `src/server/world.c::SV_Trace` (via
  `CM_BoxTrace(&trace, ..., sv.cm.cache->nodes, contentmask)`)
- Client side: `src/client/predict.c` (CL_PMTrace and PointContents
  thunks that call `CM_BoxTrace(tr, ..., cl.bsp->nodes, contentmask)`)
- Common pmove: `src/common/pmove.c` calls `pm->trace(...)` which is
  the function pointer set by client/server before calling `Pmove`.
- Other call sites: `src/client/entities.c`, `src/client/locs.c`,
  `src/server/mvd/game.c`

Terrain collision strategy (Phase 3, gated by `terrain_collision`):
- Provide `Terrain_RaycastHeightfield(start, end, mins, maxs, &out)`
  that returns a partial trace using DDA over the heightmap grid,
  returning surface normal sampled from the heightmap.
- Wrap existing BSP trace at the call sites listed above with a
  small dispatcher that runs both BSP trace and terrain trace and
  returns the closer hit when `terrain_collision 1`.
- The wrap MUST be a no-op when `terrain_collision 0` — falling
  through to `CM_BoxTrace` unchanged.
- For Phase 3, the dispatcher exists only on the client (predict
  and entities) and the server (world.c). The pmove function
  pointer is supplied by client/server and remains untouched.
- Seam patches participate in collision only when both
  `terrain_enable 1` and `terrain_collision 1`.

`PointContents` mirroring: when standing inside terrain water (Phase 6),
add a terrain water height test that reports `CONTENTS_WATER` if the
camera origin is below the water level inside the water mask. Gate by
`terrain_water 1`.

---

## 13. Current CMake / source integration model

- `CMakeLists.txt` (root) sets project version, build options, MSVC
  runtime, and orchestrates `add_subdirectory(extern)` and
  `add_subdirectory(src)`.
- `src/CMakeLists.txt` defines all source list groups
  (`SRC_GAME`, `SRC_CLIENT`, `SRC_SERVER`, `SRC_COMMON`, `SRC_REFRESH`,
  `SRC_VKPT`, `SRC_SHADERS`, `SRC_RT_SHADERS`, etc.) and links them
  into the `client`, `server`, and `game` targets.
- `extern/CMakeLists.txt` configures bundled deps: zlib, glslang
  (optional), SDL2, curl, openal-soft, and exposes interface targets
  `stb` and `tinyobjloader`.
- Shaders compiled in `IF(IS_64_BIT) ... include(../cmake/compileShaders.cmake)`.

Adding `src/client/terrain/` safely:
- Define a `SRC_TERRAIN` list inside `src/CMakeLists.txt` and a
  `HEADERS_TERRAIN` list.
- Add to `client` target via `TARGET_SOURCES(client PRIVATE ...)`,
  gated by `IF(CONFIG_VKPT_RENDERER)` and a new
  `OPTION(QUASIMODO_TERRAIN "..." ON)` switch in the root
  `CMakeLists.txt`. When `OFF`, only a stub header is compiled.
- Define `TERRAIN_ENABLED=1` for the client target when the option
  is `ON`; otherwise compile a tiny `terrain_stub.cpp` that exports
  no-op `Terrain_Init`/`Terrain_Shutdown`/`Terrain_LoadJungle`/
  `Terrain_Unload`/`Terrain_IsLoaded` so that all callers remain
  link-clean.
- Source group: `SOURCE_GROUP("client\\terrain" FILES ${SRC_TERRAIN})`.
- No new third-party libraries are required for Phase 1–4. JSON for
  `.jungle` is small enough to be handled by a local in-repo parser
  in C/C++, using existing `FS_LoadFileEx` and `Z_Malloc`. If a
  vendored JSON header is added later, it must follow
  `06-git-vendor-dependency-rules.mdc` (`third_party/<lib>/` with
  LICENSE and pinned version).

---

## 14. Lumberyard-inspired patterns that apply

- Outdoor terrain organized as a 2D grid of sectors/chunks with a
  per-sector AABB.
- A node-pyramid LOD tree where adjacent sectors hold neighbor
  pointers and their LODs differ by at most one.
- Heightmap stored as a regular 16-bit grid with a per-sector min/max
  range used for occlusion and ray rejection.
- Surface-type sampling as a separate channel that drives splatmap
  blending across multiple material layers.
- Detail textures and macro textures for far/near color variation.
- Water as a separate render-element family: one ocean plane plus
  N volume bodies, with fog/extinction/caustics parameters.
- A dedicated update dispatcher for terrain that runs streaming and
  data-driven jobs separately from rendering. In our context this is
  only conceptual: per-chunk loading is run in the asset thread and
  upload happens via the existing transfer command buffer pattern.

---

## 15. Lumberyard-inspired patterns that do not apply

- The full CryEngine `IRenderMesh` ownership model and the
  `_smart_ptr` reference counting do not transfer to the C/Vulkan
  Q2RTX engine. All terrain resources will be owned explicitly by the
  terrain subsystem, mirroring the existing `bsp_mesh_t` ownership.
- The Cry3DEngine `CTerrainUpdateDispatcher` job system is replaced
  by simple synchronous load + the existing transfer command buffer
  pattern, since terrain is loaded once per map.
- The CryEngine SVO global illumination integration is not relevant.
  Lighting comes from the existing path tracer.
- The Crytek FFT-based ocean (CREWaterOcean) is not adopted as-is;
  Quasimodo already has analytic / noise water in `water.glsl`.
  The `.jungle` water block reuses that machinery and adds optional
  textured normal and depth-fade parameters.
- The CryEngine vegetation system is out of scope for terrain Phase
  1–9. It remains a future feature.
- Terrain modifications (deformation, holes, damage) are out of
  scope for the production milestone; design hooks may be left as
  TODO comments.

---

## 16. OpenQ4 / idTech-inspired patterns that apply

- The seam patch surface concept maps directly to a `.patch` file:
  bilinear or bezier control-point grid, CPU-tessellated at load
  time into a static `VkBuffer`.
- BSP-and-additional-geometry coexistence: id Tech 4 already loads
  BSP areas alongside arbitrary entity render meshes. Quasimodo's
  hybrid `bsp_terrain` mode keeps `bsp_world_model` unchanged and
  appends terrain as a parallel render entity in the TLAS.
- Liquid as a separate model type: id Tech 4 has `Model_liquid.cpp`,
  conceptually similar to a separate water render entity in the
  TLAS.
- BSP collision remains canonical; additional geometry (terrain,
  patches) extends collision rather than replacing BSP — same as id
  Tech 4 separate collision models.

---

## 17. OpenQ4 / idTech-inspired patterns that do not apply

- The id Tech 4 light-interaction surface model and shadow volume
  pipeline do not map onto a path tracer. Q2RTX shadowing is the
  TLAS path tracer plus optional shadow map for sun god rays.
- The id Tech 4 `idMaterial` shader stage script language is not
  adopted. Materials use the existing Q2RTX PBR material file
  format plus the new `materials.json` for terrain layers.
- The id Tech 4 portal sky and area portal system is not directly
  reused; Q2RTX uses BSP visdata + sky cubemap, and terrain reuses
  that path for sky.

---

## 18. Proposed Quasimodo-native terrain architecture

Source layout (Phase 1 introduces empty stubs only):

```
src/client/terrain/
    terrain.h
    terrain.cpp                — init/shutdown/cvars/commands, public API
    terrain_jungle.cpp         — .jungle parser, asset discovery
    terrain_asset.cpp          — heightmap/splatmap/mask/material asset IO
    terrain_heightmap.cpp      — CPU heightfield, sampling, raycast
    terrain_chunk.cpp          — chunk grid, AABB, frustum cull
    terrain_lod.cpp            — distance LOD, optional morph weight
    terrain_seam.cpp           — .patch tessellation, seam upload
    terrain_collision.cpp      — heightfield trace dispatcher
    terrain_render.cpp         — Vulkan resources, BLAS instancing
    terrain_water.cpp          — water level, mask, BLAS instancing
    terrain_debug.cpp          — terrain_show_* commands and overlays
```

Public API (header `terrain.h`, all functions safe to call when
disabled):

```
void  Terrain_Init(void);
void  Terrain_Shutdown(void);

bool  Terrain_LoadJungle(const char* mapname);
void  Terrain_Unload(void);
bool  Terrain_IsLoaded(void);

void  Terrain_PerFrameBegin(void);
void  Terrain_InstanceBLAS(void);   // called from the world-BLAS block
void  Terrain_PerFrameEnd(void);

bool  Terrain_TraceLine(const vec3_t start, const vec3_t end,
                        const vec3_t mins, const vec3_t maxs,
                        int contentmask, trace_t* out);
int   Terrain_PointContents(const vec3_t p);

void  Terrain_OnSwapchainRecreate(void);
void  Terrain_OnPipelineReload(void);
```

CVars (registered in `terrain.cpp`):

```
terrain_enable          0/1  — master switch
terrain_collision       0/1  — opt-in terrain trace
terrain_water           0/1  — opt-in water subsystem
terrain_debug           0/1  — generic debug
terrain_wireframe       0/1  — wireframe overlay (raster pass only)
terrain_show_chunks     0/1  — chunk AABB lines
terrain_show_lod        0/1  — color-coded LOD
terrain_show_seams      0/1  — seam patch lines
terrain_show_collision  0/1  — collision overlay
terrain_material_debug  0/1  — splatmap channels visualizer
terrain_water_debug     0/1
```

Commands:

```
terrain_load <path>        — explicitly load a .jungle
terrain_unload             — drop the current terrain
terrain_reload             — reload current .jungle
terrain_info               — print stats (chunks, LODs, materials)
terrain_probe              — print height/normal at camera
terrain_dump_chunks        — print AABB list
terrain_rebuild            — force rebuild of CPU mesh (debug)
```

Behavioral guarantees:

- `Terrain_*` functions are safe to call at any time. When
  `terrain_enable 0` they short-circuit to the appropriate no-op.
- No Vulkan object is created until `Terrain_LoadJungle` succeeds.
- All Vulkan objects are destroyed in `Terrain_Unload` and on
  shutdown.
- `Terrain_OnSwapchainRecreate` is empty for the BLAS-only path; it
  exists for symmetry with future raster pipeline objects.

---

## 19. Proposed .jungle loading flow

```
CL_PrepRefresh
  └── R_BeginRegistration_RTX(name)
       ├── BSP_Load("maps/<name>.bsp")
       ├── bsp_mesh_register_textures
       ├── bsp_mesh_create_from_bsp
       ├── vkpt_vertex_buffer_upload_bsp_mesh
       └── if (terrain_enable->integer) {
              Terrain_Unload();                         // safety
              if (Terrain_LoadJungle(name)) {
                  // OK — chunk grid, splatmaps, materials, water
                  // are now loaded and BLAS objects are ready
              } else {
                  // .jungle missing or invalid — quiet/clean fallback,
                  // engine continues as BSP-only
              }
           }
```

Discovery rules:

- `baseq2/maps/<mapname>.jungle` is the auto-discovery path (gated
  by `terrain_enable`).
- `baseq2/worlds/<worldname>.jungle` is allowed for standalone
  terrain worlds (Phase 7+).
- Missing `.jungle` is silent (no error, no warning beyond a single
  `Com_DPrintf`) — this is the bsp_only mode.
- Invalid `.jungle` is reported with `Com_EPrintf` including the
  field name and offending value, then the subsystem disables for
  that map and falls back to bsp_only behavior.

`.jungle` minimum schema (Phase 2 will parse only the required
fields; optional fields no-op until later phases):

```jsonc
{
  "version": 1,
  "format": "jungle",
  "mode": "bsp_terrain",        // or "terrain_only" or "bsp_only"
  "world": {
    "origin": [0, 0, 0]
  },
  "terrain": {
    "heightmap": "terrain/<name>/height.png",
    "width": 1024, "height": 1024,
    "height_format": "r16_unorm",
    "scale_xy": [256.0, 256.0],
    "scale_z": 8192.0,
    "chunk_size": 64,
    "lod_count": 4,
    "lod_morph": true,
    "splatmap": ["terrain/<name>/splat0.png",
                 "terrain/<name>/splat1.png"],
    "materials": "terrain/<name>/materials.json",
    "seam_patches": ["terrain/<name>/seam_north.patch"]
  },
  "water": {
    "enabled": true,
    "level": 64.0,
    "mask": "terrain/<name>/water_mask.png",
    "normal_map": "terrain/<name>/water_n.png",
    "normal_scroll": [0.02, 0.01],
    "color": [0.10, 0.18, 0.20, 1.0],
    "roughness": 0.06,
    "refraction_strength": 0.02,
    "reflection_mode": "rtx",
    "rtx_reflection_hook": true
  },
  "sky": { "preset": "default" },
  "collision": {
    "terrain_collision": true,
    "water_collision": true,
    "seam_collision": true
  },
  "debug": {
    "draw_chunks": false,
    "draw_lod": false,
    "draw_seams": false,
    "draw_water_mask": false
  }
}
```

---

## 20. Proposed BSP + terrain hybrid strategy

- `bsp_terrain` mode is the production target. BSP loads first via
  the existing path. Terrain loads after, only if `terrain_enable 1`.
- `bsp_only` mode is the legacy default and the implicit mode when no
  `.jungle` is found. No terrain is loaded.
- `terrain_only` mode is supported in Phase 7+; for Phase 1–6 the
  engine still loads a minimal BSP placeholder so existing renderer
  paths assume `bsp_world_model != NULL`. This keeps invariants of
  `R_RenderFrame_RTX` intact.
- Visibility: terrain chunks are culled by the camera frustum on the
  CPU; PVS is not used for terrain. The chunk grid is large enough
  that PVS would not help and the BLAS instancing pipeline expects
  small numbers of large meshes.
- Lighting: terrain chunks contribute to TLAS and are lit by the
  existing path tracer. Sun light works through the existing
  physical sky. No extra light passes are added.
- Decals: out of scope.
- Footstep / sound surface: out of scope for Phase 1–9.

---

## 21. Proposed water strategy

- One ocean plane: a quad at `water.level` covering the chunk grid,
  clipped against the water mask in the fragment / hit shader.
- N volume water bodies: explicit meshes from the `.jungle`
  `water.volumes[]` array (Phase 6b stretch goal).
- Water material flags reuse `MATERIAL_FLAG_WATER` and the existing
  `water.glsl` analytic and FBM normal modes. The `.jungle` adds a
  per-water-surface normal map / scroll / color / roughness override
  via a new `terrain_water_material_t` struct that maps to a
  terrain-specific material entry.
- Refraction: starts with the existing path-traced refraction in
  `reflect_refract.rgen`. No new pass.
- Reflection: when `reflection_mode == "rtx"` and
  `rtx_reflection_hook == true`, a TODO_RTX stub documents the
  intended ray-spawn hook. No new pipeline is created in Phase 6.
- Underwater fog and extinction reuse `get_extinction_factors` in
  `water.glsl`. The `.jungle` may add a per-volume extinction factor
  later.

---

## 22. Proposed seam patch strategy

- Per-`.patch` file format (UTF-8 text, header `JUNGLE_PATCH 1`):

  ```
  JUNGLE_PATCH 1
  control_grid <w> <h>
  tess <u> <v>
  bsp_face_align <0|1>
  material <name>
  v <x> <y> <z>
  v <x> <y> <z>
  ...
  ```

- Tessellated on CPU at load time using bilinear interpolation
  between control points. Bezier patch tessellation is allowed but
  optional in Phase 3.
- Vertex count = `(tessU + 1) * (tessV + 1)`. Indices are a
  precomputed shared triangle list per `(tessU, tessV)`.
- Result is uploaded as a static `VkBuffer` (vertex + index)
  through the existing staging-buffer pattern in
  `vkpt_vertex_buffer.c`.
- One BLAS per patch. One TLAS instance per visible patch.
- Optional `bsp_face_align: true` projects the top control row onto
  the nearest BSP face plane at load time — implemented in Phase 7
  using `BSP_PointLeaf` and `cm_traceWork.plane` evaluation.
- Depth bias for raster overlays is documented in the rules but is
  irrelevant for the BLAS path.

---

## 23. Proposed RTX future-hook strategy

All Vulkan/RTX hookups follow this pattern in Phase 1–4 (no real
Vulkan calls yet, only stubs):

```c
void Terrain_BuildBLAS(void)
{
    // TODO_RTX: Build VkAccelerationStructureKHR BLAS per terrain chunk.
    // - One BLAS per chunk; rebuild only when chunk dirty flag set.
    // - Geometry input: chunk vertex buffer + LOD 0 index buffer.
    // - Use existing accel_struct_t pattern from
    //   src/refresh/vkpt/path_tracer.c (vkpt_pt_create_all_dynamic).
    // - Integration point: just before vkpt_pt_create_toplevel(...) in
    //   src/refresh/vkpt/main.c::R_RenderFrame_RTX.
}

void Terrain_InstanceBLAS(void)
{
    // TODO_RTX: For each visible chunk, call vkpt_pt_instance_model_blas
    // with the chunk's accel_struct_t and identity transform.
    // Mirror the bsp_mesh_world.geom_opaque instance call.
}

void Terrain_BuildSeamBLAS(void)
{
    // TODO_RTX: Build static BLAS per seam patch at load time.
}

void Terrain_WaterRTXReflection(void)
{
    // TODO_RTX: Spawn dedicated reflection rays from the water surface
    // when reflection_mode == "rtx". Until then, use existing
    // reflect_refract.rgen path.
}
```

Phase 5 (terrain) and Phase 6 (water) replace these stubs with real
calls. No other vkpt subsystem is touched.

---

## 24. Risk matrix

| ID | Area | Risk | Likelihood | Impact | Mitigation |
|----|------|------|-----------|--------|-----------|
| R1 | BSP loading hook | Adding terrain load inside `R_BeginRegistration_RTX` could affect map loading even when `terrain_enable 0` | Med | High | Wrap entire hook in `if (terrain_enable->integer) { Terrain_Unload(); if (Terrain_LoadJungle(name)) ... }`; verify map loads when option is OFF |
| R2 | TLAS budget | Adding many small terrain BLAS could blow `MAX_TLAS_INSTANCES` | Med | High | Cap chunk count; coalesce visible chunks by AABB cluster if needed; document in rules |
| R3 | BLAS rebuild cost | Per-frame BLAS rebuild for LOD changes | Med | High | Use vertex-shader morph for LOD; only rebuild on chunk dirty flag |
| R4 | JSON parser | No JSON parser exists in repo; rolling our own risks fragility | High | Med | Implement a minimal subset parser in Phase 2 (numbers, strings, arrays, objects, booleans, null); document the subset; reject unknown structures cleanly |
| R5 | Image format | Heightmap as 16-bit PNG must round-trip without lossy conversion | High | Med | Use stb_image with `desired_channels = 1` and 16-bit support; verify with terrain_probe |
| R6 | Splatmap channels | Authors must keep RGBA channels meaningful and normalized | Med | Med | Add `terrain_material_debug 1` to visualize each channel; reject splatmaps that sum > 1.05 in any pixel during validation |
| R7 | Seam Z-fighting | Patches at BSP boundary may Z-fight | Med | Med | Use depth bias in raster path (later); offset slightly along normal in BLAS path; document |
| R8 | Collision dispatch | Wrapping `CM_BoxTrace` call sites adds risk to gameplay | High | High | Only wrap at known sites; preserve original `CM_BoxTrace` behavior when `terrain_collision 0`; add unit-test maps |
| R9 | Water render contention | Adding water BLAS may collide with existing water surfaces in BSP maps | Med | Med | Disable terrain water when BSP map already has water surfaces, or merge masks; gated by `terrain_water` |
| R10 | Vulkan validation | New BLAS/buffer creation must not produce validation errors | Med | High | Run validation in dev build; debug-name all new objects |
| R11 | Swapchain recreate | Terrain BLAS not affected, but raster pipelines (if added) must recreate | Low | Med | Register `terrain|` entry in `vkpt_initialization[]` with `VKPT_INIT_SWAPCHAIN_RECREATE` only when raster path is enabled |
| R12 | Shutdown ordering | New Vulkan objects must be destroyed before device | Med | High | All terrain destruction happens in the registered `destroy` callback in `vkpt_initialization[]` |
| R13 | External code drift | Risk of accidentally pasting reference code | Med | High | This rule already enforced by `.cursor/rules/07-external-reference-repos.mdc`; phase prompts remind reviewers |
| R14 | Path leakage | Risk of writing local reference paths into repo | Med | High | All committed reports use `REFERENCE_OPENQ4` / `REFERENCE_LUMBERYARD`; CI scan blocks Windows drive-letter prefixes, POSIX user-home prefixes, the workspace parent folder name, and the external reference parent folder name |
| R15 | RmlUi side effects | UI focus or input change while introducing terrain debug commands | Low | Med | All terrain commands go through `Cmd_AddCommand`; no input pump changes |

---

## 25. Files likely to change

By phase. None are touched in Phase 0.

Phase 1 — skeleton:
- ADD `src/client/terrain/terrain.h`
- ADD `src/client/terrain/terrain.cpp`
- ADD `src/client/terrain/terrain_stub.cpp`
- MODIFY `src/CMakeLists.txt` (add `SRC_TERRAIN`, hook into client target)
- MODIFY `CMakeLists.txt` (add `OPTION(QUASIMODO_TERRAIN ... ON)`)
- ADD `.cursor/docs/terrain-implementation-handoff.md` (this Phase 0)
- ADD `.cursor/docs/terrain-phase-prompts.md` (this Phase 0)

Phase 2 — `.jungle` parser:
- ADD `src/client/terrain/terrain_jungle.cpp`
- ADD `src/client/terrain/terrain_asset.cpp`
- ADD `src/client/terrain/jungle_json.h` (small JSON parser, internal)
- MODIFY `src/CMakeLists.txt` to include new sources

Phase 3 — CPU heightfield + seam patches:
- ADD `src/client/terrain/terrain_heightmap.cpp`
- ADD `src/client/terrain/terrain_seam.cpp`
- ADD `src/client/terrain/terrain_collision.cpp`
- MODIFY `src/CMakeLists.txt`

Phase 4 — chunks + LOD:
- ADD `src/client/terrain/terrain_chunk.cpp`
- ADD `src/client/terrain/terrain_lod.cpp`
- ADD `src/client/terrain/terrain_debug.cpp`
- MODIFY `src/CMakeLists.txt`

Phase 5 — Vulkan terrain rendering (TODO_RTX → real):
- ADD `src/client/terrain/terrain_render.cpp`
- ADD `src/refresh/vkpt/shader/terrain.vert` (only if raster validation
  pass is needed)
- ADD `src/refresh/vkpt/shader/terrain.frag` (same)
- MODIFY `src/refresh/vkpt/main.c` — register terrain in
  `vkpt_initialization[]`; wire `Terrain_LoadJungle` into
  `R_BeginRegistration_RTX`; wire `Terrain_InstanceBLAS` into
  `R_RenderFrame_RTX` world-BLAS block
- MODIFY `src/CMakeLists.txt` — add new shaders

Phase 6 — water:
- ADD `src/client/terrain/terrain_water.cpp`
- ADD optional water shader updates in `src/refresh/vkpt/shader/`
  (only if path tracer needs them; existing `water.glsl` may suffice)

Phase 7 — bsp + terrain hybrid:
- MODIFY `src/refresh/vkpt/main.c` (additional cleanup paths only)
- MODIFY `src/server/world.c` (collision dispatch wrapper, gated)
- MODIFY `src/client/predict.c` (collision dispatch wrapper, gated)

Phase 8 — materials:
- MODIFY `src/refresh/vkpt/shader/path_tracer.rchit` (terrain branch)
- ADD `baseq2/terrain/<map>/materials.json` per authoring map

Phase 9 — tooling:
- MODIFY `src/client/terrain/terrain_debug.cpp` (additional commands)

Phase 10 — hygiene:
- No source changes; pure repo audit.

Phase 11 — docs:
- ADD `docs/terrain_system.md`

Phase 12 — build & validate:
- No source changes; build + smoke run only.

---

## 26. Exact phased implementation plan

(All phase rules preserved from the master prompt; the Allowed/
Forbidden lists below are duplicated into
`.cursor/docs/terrain-implementation-handoff.md` for cheaper models.)

**Phase 0 — Audit (this report).**
Allowed files: this audit, the handoff, the phase prompts file, an
optional reference patterns file. No source code.

**Phase 1 — Skeleton.**
Allowed: `src/client/terrain/*` (new), `src/CMakeLists.txt`,
`CMakeLists.txt`. Forbidden: any vkpt source, any shader, any
collision call site, any RmlUi file. Exit: build succeeds; CVars
present; commands print stubs; `terrain_enable 0` zero-touch.

**Phase 2 — `.jungle` parser + assets.**
Allowed: `src/client/terrain/terrain_jungle.cpp`,
`src/client/terrain/terrain_asset.cpp`,
`src/client/terrain/jungle_json.h`, `src/CMakeLists.txt`. Forbidden:
vkpt, shaders, collision sites, RmlUi. Exit: empty `.jungle` loads
silently; valid `.jungle` parses to in-memory struct; invalid
`.jungle` reports field-level error and disables.

**Phase 3 — CPU heightfield + seam patches + opt-in trace.**
Allowed: `src/client/terrain/terrain_heightmap.cpp`,
`src/client/terrain/terrain_seam.cpp`,
`src/client/terrain/terrain_collision.cpp`, `src/CMakeLists.txt`.
Forbidden: vkpt, shaders, server/client trace call sites (only the
internal terrain functions are added — wrapping at call sites lands
in Phase 7). Exit: `terrain_probe` reports height/normal at camera;
`terrain_collision 0` is unchanged BSP behavior.

**Phase 4 — Chunks + LOD + debug overlays.**
Allowed: `src/client/terrain/terrain_chunk.cpp`,
`src/client/terrain/terrain_lod.cpp`,
`src/client/terrain/terrain_debug.cpp`, `src/CMakeLists.txt`.
Forbidden: vkpt source, shaders, collision sites, RmlUi. Exit:
chunks visible via `vkpt_debugdraw_*` overlay when
`terrain_show_chunks 1`; chunks toggle LOD with distance cvar.

**Phase 5 — Vulkan terrain rendering (BLAS-first).**
Phase 5 preflight prompt is required for Opus/reviewer model.
Allowed: `src/client/terrain/terrain_render.cpp`,
`src/refresh/vkpt/shader/terrain.vert` /
`terrain.frag` (only if raster debug needed),
`src/refresh/vkpt/main.c` (only the two insertion points), and
`src/CMakeLists.txt`. Forbidden: bsp_mesh.c, path_tracer.c,
swapchain code, RmlUi, every other vkpt source. Exit: BLAS instances
appear in TLAS when `terrain_enable 1`; zero validation errors;
`terrain_enable 0` produces zero new Vulkan objects.

**Phase 6 — Water.**
Allowed: `src/client/terrain/terrain_water.cpp`,
`src/refresh/vkpt/shader/water.glsl` (only if needed; prefer
parameter-only changes via the UBO). Forbidden: bsp_mesh.c,
path_tracer.c, RmlUi. Exit: water surface visible when
`terrain_water 1`; flickering and ordering checks pass.

**Phase 7 — BSP + terrain hybrid.**
Allowed: `src/refresh/vkpt/main.c` (only cleanup symmetry),
`src/server/world.c`, `src/client/predict.c`, `src/server/init.c`
(only the collision dispatch wrap). Forbidden: pmove.c, cmodel.c,
RmlUi. Exit: BSP-only maps unchanged; `bsp_terrain` mode loads BSP
plus terrain; `terrain_collision 0` is pure BSP behavior.

**Phase 8 — Materials.**
Allowed: `src/refresh/vkpt/shader/path_tracer.rchit`,
`src/client/terrain/terrain_render.cpp`,
`baseq2/terrain/<map>/materials.json`. Forbidden: bsp_mesh.c,
path_tracer.c (apart from the rchit entry-point), RmlUi. Exit:
splatmap blending visible; debug viewer works.

**Phase 9 — Tooling.**
Allowed: `src/client/terrain/terrain_debug.cpp`. Forbidden: vkpt,
shaders, RmlUi. Exit: `terrain_info`, `terrain_dump_chunks`,
`terrain_probe`, `terrain_rebuild` all functional.

**Phase 10 — Hygiene.**
Allowed: pure inspection. Outputs go to a markdown report in
`.cursor/docs/`. No source changes.

**Phase 11 — Docs.**
Allowed: `docs/terrain_system.md`. Forbidden: source/CMake/shader.

**Phase 12 — Build + validate.**
Allowed: build commands, runtime smoke checks. No source changes.

---

## 27. Validation strategy

For every implementation phase:

1. `git status --short` before edits.
2. Edit only phase-allowed files. Reject phase-forbidden files.
3. Build using:
   - Configure (Windows MSVC, repository default):
     `cmake -S . -B build -G "Visual Studio 17 2022" -A x64`
   - Build:
     `cmake --build build --config RelWithDebInfo --target client`
   - Alternative if a Ninja preset is in use locally:
     `cmake --build build --config RelWithDebInfo --target client`
4. Report exact build command and exact build result (success/fail
   with line counts and any warnings introduced by the change).
5. List files added, files changed, files intentionally not
   touched.
6. Scan changed files for forbidden path patterns: Windows
   drive-letter prefixes, POSIX user-home prefixes, the workspace
   parent folder name, the external reference parent folder name,
   the local paths used for REFERENCE_OPENQ4 / REFERENCE_LUMBERYARD,
   and any other external reference path.
7. Confirm no copied reference code (no `idTech` / `Crytek` /
   `Lumberyard` / `OpenQ4` text in new source).
8. Confirm no unrelated RmlUi changes.
9. Confirm no NVIDIA SDK binaries or build outputs were staged
   (`git diff --cached --name-only` and human review).
10. Runtime smoke check (when applicable):
    - Launch `q2rtx`, observe console banner.
    - Run `map demo1` (or a known BSP map) with `terrain_enable 0`;
      verify identical behavior to pre-change.
    - Run the same map with `terrain_enable 1`; if a `.jungle`
      exists, verify load message; otherwise verify silent skip.
    - Toggle `ui_rmlui 0` and `ui_rmlui 1`; verify both work.
    - Toggle `terrain_collision 0` and `1` (Phase 3+);
      verify pure BSP behavior with `0`.
    - Toggle `terrain_water 0` and `1` (Phase 6+);
      verify pure terrain behavior with `0`.

Vulkan validation:
- Build with validation layers enabled.
- Walk through map load, alt-tab (swapchain recreate), shader
  reload (if applicable), shutdown.
- Zero validation errors required for the phase to be considered
  complete.

---

## 28. Markdown handoff files created or updated

Created in this Phase 0:

- `.cursor/docs/terrain-phase0-audit.md` (this file)
- `.cursor/docs/terrain-implementation-handoff.md`
- `.cursor/docs/terrain-phase-prompts.md`
- `.cursor/docs/terrain-reference-patterns.md` (concept-only summary
  of patterns extracted from REFERENCE_OPENQ4 and REFERENCE_LUMBERYARD)

These four files are the durable context for cheaper models. They
contain no absolute local paths and no external reference paths.

LIMITATION (honestly reported per `08-ai-workflow-audit-first.mdc`):
The repository `.gitignore` line 52 contains `*/docs`, which causes
git to ignore every directory named `docs` at any depth, including
`.cursor/docs/`. Both these new handoff files and the existing
`.cursor/docs/q2rtx-codebase-map.md` are therefore git-ignored on
disk and will not survive a fresh clone unless a small `.gitignore`
exception is added.

Suggested minimal follow-up (not performed in Phase 0; surface to
the user for explicit approval before editing `.gitignore`):

```
# allow committed agent context under .cursor/docs/
!.cursor/docs/
!.cursor/docs/**
```

Until that exception is accepted, the four handoff files exist on
disk for the current Cursor session only.

---

## 29. Implementation code statement

No implementation code was written in Phase 0. No source file under
`src/`, no shader under `src/refresh/vkpt/shader/`, no RmlUi file,
no terrain implementation file, no CMakeLists.txt, and no build
script was modified in Phase 0. Only the four markdown files listed
in section 28 were created. Phase 1 was not started.
