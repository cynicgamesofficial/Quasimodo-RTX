# Q2RTX Codebase Map

Reference this when auditing before any engine edit.
Always verify against actual source — this map may lag behind.

---

## Top-Level Structure

```
src/
  client/          ← client-side game logic, UI, input, prediction
  server/          ← server-side game logic, entities
  common/          ← shared: BSP, collision, filesystem, memory, cvars
  refresh/
    vkpt/          ← Vulkan RTX renderer (primary rendering system)
  game/            ← game DLL (entities, items, weapons, AI)
  client/terrain/  ← NEW: terrain subsystem (to be created)
baseq2/
  maps/            ← .bsp map files
  terrain/         ← NEW: terrain assets (.jungle configs, heightmaps)
  ui/              ← RmlUi documents (.rml), stylesheets (.rcss), fonts
  materials/       ← material definition files
```

---

## Key Files by Subsystem

### BSP / World Loading
```
src/common/bsp.c              ← BSP load, lump parsing
src/common/collision.c        ← CM_LoadMap, CM_Trace, CM_PointContents
src/client/cl_main.c          ← CL_PrepRefresh, client map load sequence
src/server/sv_main.c          ← SV_Map, server map load
```

### Vulkan Renderer
```
src/refresh/vkpt/main_vk.c        ← Frame loop, swapchain, command buffers
src/refresh/vkpt/vkpt_draw.c      ← World draw submission
src/refresh/vkpt/vkpt_geometry.c  ← Geometry buffers, BLAS
src/refresh/vkpt/vkpt_image.c     ← Image/texture loading, transitions
src/refresh/vkpt/vkpt_pipeline.c  ← Pipeline creation helpers
src/refresh/vkpt/vkpt_uniform.c   ← Global UBO, per-frame constants
src/refresh/vkpt/vkpt_pt.c        ← Path tracing, TLAS, RT pipeline
src/refresh/vkpt/vkpt_textures.c  ← Texture array, material textures
```

### Shader Files
```
src/refresh/vkpt/shader/
  main.vert / main.frag       ← Primary raster shaders
  path_tracer.rgen/rmiss/rchit ← RT shaders
  post_process.comp           ← Postprocess chain
  [NEW] terrain.vert/frag     ← Terrain shaders (to be created)
  [NEW] water_ocean.vert/frag ← Water shaders (to be created)
```

### Material System
```
src/refresh/vkpt/vkpt_textures.c  ← Material/image loading
src/common/material.c             ← Material definition parsing
```

### Filesystem
```
src/common/files.c            ← FS_LoadFile, FS_FreeFile, FS_OpenFile
src/common/packfile.c         ← PAK/ZIP file support
```

### CVar / Command System
```
src/common/cvar.c             ← Cvar_Get, Cvar_Set, Cvar_Find
src/common/cmd.c              ← Cmd_AddCommand, Cmd_RemoveCommand
```

### RmlUi Integration
```
src/client/ui_rmlui.cpp       ← RmlUi context, document management
src/client/ui_rmlui_render.cpp ← Vulkan render interface for RmlUi
src/client/ui_rmlui_input.cpp ← Input forwarding to RmlUi
baseq2/ui/                    ← .rml documents, .rcss stylesheets
```

### Memory
```
src/common/zone.c             ← Z_Malloc, Z_Free, Z_TagMalloc
src/common/hunk.c             ← Hunk_Alloc (large persistent allocs)
```

---

## Vulkan Descriptor Set Index Convention

VERIFY this against actual source before using:
- Set 0: Global/per-frame data (camera, lights, time)
- Set 1: World/BSP geometry textures
- Set 2: AVAILABLE for terrain (verify no collision)
- Set 3: AVAILABLE for water (verify no collision)

**Always audit `vkpt_pipeline.c` and `vkpt_draw.c` before assigning set indices.**

---

## Key CVars (existing)

```
r_fullscreen         Screen mode
r_vsync              VSync
pt_enable            Path tracing master toggle
ui_rmlui             0=legacy UI, 1=RmlUi
```

## New CVars (terrain system, to be added)

```
terrain_enable       0=off (default), 1=load terrain
terrain_collision    0=BSP only (default), 1=terrain queries active
terrain_water        0=off (default), 1=water enabled
terrain_debug        Debug overlay
terrain_wireframe    Wireframe mode
terrain_show_chunks  Chunk AABB overlay
terrain_show_lod     LOD heatmap
terrain_show_seams   Seam patch highlight
terrain_material_debug Splat channel visualization
```

---

## Resize / Alt-Tab Pattern

Q2RTX handles swapchain recreation via:
```c
// In main_vk.c
vkpt_*_destroy();   // destroy swapchain-dependent objects
// ... recreate swapchain ...
vkpt_*_create();    // recreate
```

**Any new pipeline must follow this exact pattern.**
Find the existing `vkpt_*_destroy` / `vkpt_*_create` call sites
and add terrain equivalents in the same locations.

---

## RTX Integration Points

```
src/refresh/vkpt/vkpt_geometry.c  ← BLAS build per mesh
src/refresh/vkpt/vkpt_pt.c        ← TLAS build, RT dispatch
```

Terrain BLAS integration point: `vkpt_pt_create_toplevel()` in `vkpt_pt.c`.
Add terrain chunk BLAS instances here when TODO_RTX is implemented.
