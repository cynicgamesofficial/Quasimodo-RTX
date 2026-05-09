# Terrain + Water — Implementation Handoff

This file is the primary context document for cheaper models executing
later phases. It is intentionally concise and authoritative. Read it
in full before any phase prompt.

For deeper background, see:
- `.cursor/docs/terrain-phase0-audit.md`
- `.cursor/docs/terrain-phase-prompts.md`
- `.cursor/docs/terrain-reference-patterns.md`
- `.cursor/rules/04-terrain-water-system.mdc`
- `.cursor/rules/02-vulkan-rtx-rendering.mdc`
- `.cursor/skills/terrain-water-implementation/SKILL.md`
- `.cursor/skills/vulkan-rendering-review/SKILL.md`

External reference repositories are read-only conceptual references
only and are referred to here strictly as `REFERENCE_OPENQ4` and
`REFERENCE_LUMBERYARD`. Do not write their local paths into any
repository file. Do not re-inspect them unless a task explicitly
requires it.

---

## 1. Non-negotiable invariants

1. `terrain_enable 0` produces zero behavioral change.
2. `terrain_collision 0` produces pure BSP collision behavior.
3. `terrain_water 0` disables the water subsystem entirely.
4. `ui_rmlui 0` continues to restore the legacy UI path.
5. Existing BSP maps load and run unchanged.
6. No copied code from external reference engines.
7. No absolute local paths in any committed file.
8. No new Vulkan abstraction layer; extend `vkpt_*` patterns.
9. No per-frame `vkCreate*` / `vkDestroy*` / `vkDeviceWaitIdle`.
10. Build must succeed at the end of every phase.
11. Phase-bounded edits only. No mixed concerns per commit.

---

## 2. Fallback CVar contract

| CVar | OFF behavior |
|---|---|
| `terrain_enable 0` | Terrain subsystem dormant; `Terrain_LoadJungle` is not called; no Vulkan objects created |
| `terrain_collision 0` | Collision dispatcher returns the original `CM_BoxTrace` / `CM_PointContents` result unchanged |
| `terrain_water 0` | Water rendering and water collision disabled regardless of `.jungle` content |
| `ui_rmlui 0` | Existing legacy UI path is unchanged by terrain work |

These four CVars must always be checked at the entry point of any
terrain code path. Calling any `Terrain_*` function when the
relevant CVar is `0` must short-circuit to the appropriate no-op.

---

## 3. Repository paths

Existing (do not break):
- `src/common/bsp.c`, `src/common/cmodel.c`, `src/common/pmove.c`
- `src/client/precache.c`, `src/client/main.c`, `src/client/predict.c`,
  `src/client/entities.c`
- `src/server/init.c`, `src/server/commands.c`, `src/server/main.c`,
  `src/server/world.c`, `src/server/save.c`, `src/server/mvd/parse.c`
- `src/refresh/vkpt/main.c`, `src/refresh/vkpt/path_tracer.c`,
  `src/refresh/vkpt/bsp_mesh.c`, `src/refresh/vkpt/material.c`,
  `src/refresh/vkpt/transparency.c`,
  `src/refresh/vkpt/uniform_buffer.c`,
  `src/refresh/vkpt/vertex_buffer.c`, `src/refresh/vkpt/textures.c`,
  `src/refresh/vkpt/draw.c`, `src/refresh/vkpt/vk_util.c`
- `src/refresh/vkpt/shader/water.glsl`,
  `src/refresh/vkpt/shader/global_ubo.h`,
  `src/refresh/vkpt/shader/path_tracer.rchit`
- `src/client/ui_rmlui*.{cpp,h}`
- `inc/common/files.h`, `inc/common/zone.h`, `inc/common/cvar.h`,
  `inc/common/cmd.h`
- `cmake/compileShaders.cmake`, `src/CMakeLists.txt`,
  `extern/CMakeLists.txt`, `CMakeLists.txt`

New (created across the phases):
- `src/client/terrain/terrain.h`
- `src/client/terrain/terrain.cpp`
- `src/client/terrain/terrain_stub.cpp`
- `src/client/terrain/terrain_jungle.cpp`
- `src/client/terrain/terrain_asset.cpp`
- `src/client/terrain/terrain_heightmap.cpp`
- `src/client/terrain/terrain_chunk.cpp`
- `src/client/terrain/terrain_lod.cpp`
- `src/client/terrain/terrain_seam.cpp`
- `src/client/terrain/terrain_collision.cpp`
- `src/client/terrain/terrain_render.cpp`
- `src/client/terrain/terrain_water.cpp`
- `src/client/terrain/terrain_debug.cpp`
- `src/client/terrain/jungle_json.h`  (small internal JSON helper)
- `baseq2/maps/<name>.jungle`         (per-map asset, optional)
- `baseq2/terrain/<name>/...`         (per-map heightmap/splat/etc.)

---

## 4. Architecture summary

- Source layout under `src/client/terrain/`.
- Public API in `terrain.h`. Every function is safe to call when
  `terrain_enable 0`; calls become no-ops.
- A `vkpt_initialization[]` entry registers terrain init/destroy in
  `src/refresh/vkpt/main.c` (Phase 5).
- Terrain renders by emitting BLAS instances into the existing TLAS
  built by `vkpt_pt_create_toplevel`. Insertion point: the world-BLAS
  block in `R_RenderFrame_RTX`.
- Water reuses `MATERIAL_FLAG_WATER` and the existing
  `src/refresh/vkpt/shader/water.glsl`.
- Collision adds an opt-in dispatcher around `CM_BoxTrace` /
  `CM_PointContents` at known call sites, gated by
  `terrain_collision`.

---

## 5. .jungle loading flow

```
CL_PrepRefresh
  -> R_BeginRegistration_RTX(name)
       BSP loads first (existing path; do not modify)
       if (terrain_enable->integer) {
           Terrain_Unload();
           if (Terrain_LoadJungle(name)) {
               // .jungle parsed, assets loaded, BLAS ready
           }
           // else: silent fallback to BSP-only
       }
```

Discovery:
- `baseq2/maps/<mapname>.jungle` is the auto-discovery path.
- `baseq2/worlds/<worldname>.jungle` is allowed for standalone
  terrain worlds (Phase 7+).

Failure handling:
- Missing `.jungle`: silent (one `Com_DPrintf`).
- Invalid `.jungle`: `Com_EPrintf` with field/value, then disable
  terrain for this map. No crash. No partial state.

---

## 6. Phase-by-phase allowed and forbidden files

Notation: `+` may add, `~` may modify, `-` must not touch.

**Phase 1 — Skeleton**
- `+ src/client/terrain/terrain.{h,cpp}`
- `+ src/client/terrain/terrain_stub.cpp`
- `~ src/CMakeLists.txt`  (only to add `SRC_TERRAIN` and source group)
- `~ CMakeLists.txt`      (only to add `OPTION(QUASIMODO_TERRAIN ON)`)
- `- everything else`

**Phase 2 — `.jungle` parser + assets**
- `+ src/client/terrain/terrain_jungle.cpp`
- `+ src/client/terrain/terrain_asset.cpp`
- `+ src/client/terrain/jungle_json.h`
- `~ src/CMakeLists.txt`
- `- everything else`

**Phase 3 — CPU heightfield + seams + opt-in trace internals**
- `+ src/client/terrain/terrain_heightmap.cpp`
- `+ src/client/terrain/terrain_seam.cpp`
- `+ src/client/terrain/terrain_collision.cpp`
- `~ src/CMakeLists.txt`
- `- collision call sites in server/world.c, client/predict.c`
- `- everything else`

**Phase 4 — Chunks + LOD + debug overlay**
- `+ src/client/terrain/terrain_chunk.cpp`
- `+ src/client/terrain/terrain_lod.cpp`
- `+ src/client/terrain/terrain_debug.cpp`
- `~ src/CMakeLists.txt`
- `- everything else`

**Phase 5 — Vulkan terrain rendering (BLAS-first, raster optional)**
- `+ src/client/terrain/terrain_render.cpp`
- `+ src/refresh/vkpt/shader/terrain.vert` (only if raster validation)
- `+ src/refresh/vkpt/shader/terrain.frag` (only if raster validation)
- `~ src/refresh/vkpt/main.c` (two insertion points only:
  registration in `vkpt_initialization[]`, hook in
  `R_BeginRegistration_RTX`, hook in `R_RenderFrame_RTX`)
- `~ src/CMakeLists.txt` (only to add new shaders)
- `- src/refresh/vkpt/path_tracer.c, bsp_mesh.c, vkpt_*_destroy
  outside the registration table, RmlUi`

**Phase 6 — Water**
- `+ src/client/terrain/terrain_water.cpp`
- `~ src/refresh/vkpt/shader/water.glsl` (only if absolutely needed;
  prefer parameter-only changes via UBO)
- `- everything else`

**Phase 7 — BSP + terrain hybrid**
- `~ src/refresh/vkpt/main.c` (cleanup symmetry only)
- `~ src/server/world.c` (collision dispatch wrapper, gated)
- `~ src/client/predict.c` (collision dispatch wrapper, gated)
- `- src/common/pmove.c, src/common/cmodel.c`

**Phase 8 — Materials**
- `~ src/refresh/vkpt/shader/path_tracer.rchit` (terrain branch only)
- `~ src/client/terrain/terrain_render.cpp`
- `+ baseq2/terrain/<map>/materials.json`
- `- everything else`

**Phase 9 — Tooling**
- `~ src/client/terrain/terrain_debug.cpp`
- `- everything else`

**Phase 10 — Hygiene**
- Pure inspection. Output a markdown report under `.cursor/docs/`.

**Phase 11 — Docs**
- `+ docs/terrain_system.md`
- `- source/CMake/shader files`

**Phase 12 — Build + validate**
- No source changes. Build + smoke run.

---

## 7. Build commands (Windows MSVC)

Configure (default project layout):
```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```

Build the client:
```
cmake --build build --config RelWithDebInfo --target client
```

Build everything:
```
cmake --build build --config RelWithDebInfo
```

If a Ninja preset is set up locally:
```
cmake --build build --config RelWithDebInfo --target client
```

Outputs land at the repository root because of the
`RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}` setting on the
`client` target.

Shader compilation is part of the `shaders` custom target and is a
build dependency of `client`.

---

## 8. Validation checklist (every phase)

Before edits:
- [ ] `git status --short`
- [ ] Confirm phase scope; reject any out-of-phase file.

After edits:
- [ ] Build succeeds with the command in section 7.
- [ ] No new compiler warnings in the touched files.
- [ ] No new Vulkan validation errors at:
      cold start, map load, alt-tab (swapchain recreate),
      shader reload (if applicable), shutdown.
- [ ] `terrain_enable 0` results in no behavior change vs main.
- [ ] `ui_rmlui 0` still restores legacy UI.
- [ ] Old BSP map (e.g. `demo1`) loads and plays unchanged.
- [ ] `.jungle` missing for a map = silent skip.
- [ ] Invalid `.jungle` = clean error, no crash.
- [ ] Forbidden-path scan on changed files passes: Windows
      drive-letter prefixes, POSIX user-home prefixes, the
      workspace parent folder name, the external reference parent
      folder name, and any external reference local path.
- [ ] No copied reference code in new files.
- [ ] No NVIDIA SDK binaries or build outputs staged.
- [ ] Files added / changed / intentionally untouched are reported.

---

## 9. Known risks (compact)

- TLAS instance count budget — coalesce chunk BLAS if needed.
- LOD-driven BLAS rebuild cost — prefer vertex-shader morph.
- 16-bit heightmap loading via stb_image — verify with
  `terrain_probe`.
- Splatmap channel sum > 1 — visualize in
  `terrain_material_debug 1`.
- Collision dispatch must be a true no-op when
  `terrain_collision 0`.
- Seam Z-fight at BSP boundary — small along-normal offset.
- Per-frame allocations are forbidden; allocate at load time only.
- Shutdown ordering — Vulkan resources must be destroyed before
  the device.
- Path leakage — never write external reference local paths into
  the repository.

---

## 10. Source-of-truth files for cheaper models

- `.cursor/docs/terrain-implementation-handoff.md` (this file)
- `.cursor/docs/terrain-phase-prompts.md`
- `.cursor/skills/terrain-water-implementation/SKILL.md`
- `.cursor/skills/vulkan-rendering-review/SKILL.md`
- `.cursor/rules/04-terrain-water-system.mdc`
- `.cursor/rules/02-vulkan-rtx-rendering.mdc`
- `.cursor/rules/01-q2rtx-engine-safety.mdc`

Cheaper models executing implementation phases should not need to
re-inspect REFERENCE_OPENQ4 or REFERENCE_LUMBERYARD. The Phase 0
audit and the reference patterns file capture every concept needed
for Phase 1–9.

---

## 11. Naming, prefixes, and tags

- C/C++ functions: `Terrain_*`, `TerrainWater_*` (or in the file
  scope, `terrain_*` for static helpers).
- CVars: `terrain_*`, `terrain_water_*`.
- Source files: `terrain_*` and live under `src/client/terrain/`.
- Shaders: `terrain*.vert/frag/glsl`, `water*.vert/frag/glsl` and
  live under `src/refresh/vkpt/shader/`.
- Memory tag: define `TAG_TERRAIN` in `terrain.cpp` for
  `Z_TagMalloc` allocations. Use `Z_FreeTags(TAG_TERRAIN)` at
  shutdown for safety.
- Vulkan debug names: `Terrain.Chunk[i].Vertices`,
  `Terrain.Chunk[i].Indices`, `Terrain.Chunk[i].BLAS`,
  `Terrain.Seam[i].Vertices`, `Terrain.Water.Ocean.BLAS`, etc.
- Profiler markers: `TERRAIN_INSTANCE`, `TERRAIN_WATER_INSTANCE`.

---

## 12. Do not

- Re-read REFERENCE_OPENQ4 or REFERENCE_LUMBERYARD unless a phase
  explicitly says "consult the reference for X".
- Replace BSP loading. Terrain is additive only.
- Touch RmlUi files for terrain work.
- Touch the path tracer pipeline construction. Reuse the BLAS
  instancing API.
- Add new third-party libraries without a `third_party/<lib>/`
  vendoring plan that follows
  `.cursor/rules/06-git-vendor-dependency-rules.mdc`.
- Write reference engine names (CryEngine, Lumberyard, OpenQ4,
  id Tech 4) into source code, asset paths, debug labels, or
  user-visible UI strings.
- Use `git add .`. Stage explicit files only.
