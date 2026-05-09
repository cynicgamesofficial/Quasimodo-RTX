# Terrain + Water — Phase Prompts

Reusable prompts for routing terrain/water work to specific models.
Each prompt is small, phase-bounded, and references the durable
context files. Do not duplicate the master prompt here.

Common preamble for every phase:
- Read `.cursor/docs/terrain-implementation-handoff.md` first.
- Obey `.cursor/rules/01-q2rtx-engine-safety.mdc`,
  `.cursor/rules/02-vulkan-rtx-rendering.mdc`,
  `.cursor/rules/04-terrain-water-system.mdc`,
  `.cursor/rules/06-git-vendor-dependency-rules.mdc`,
  `.cursor/rules/07-external-reference-repos.mdc`,
  `.cursor/rules/08-ai-workflow-audit-first.mdc`,
  `AGENTS.md`.
- Use `.cursor/skills/terrain-water-implementation/SKILL.md` and
  `.cursor/skills/vulkan-rendering-review/SKILL.md`.
- Build command:
  `cmake --build build --config RelWithDebInfo --target client`.
- Stop and report blockers if any allowed file cannot be edited
  safely or any forbidden file would need to change.

---

## P1-IMPL — Phase 1 implementation (Cursor Auto)

> Implement Phase 1 of the terrain plan. Read
> `.cursor/docs/terrain-implementation-handoff.md` section 6 (Phase 1)
> before editing.
>
> Allowed files:
> - `src/client/terrain/terrain.h` (new)
> - `src/client/terrain/terrain.cpp` (new)
> - `src/client/terrain/terrain_stub.cpp` (new)
> - `src/CMakeLists.txt` (only to add `SRC_TERRAIN`,
>   `HEADERS_TERRAIN`, source group, and link sources into the
>   `client` target inside the existing `IF(CONFIG_VKPT_RENDERER)`
>   block)
> - `CMakeLists.txt` (only to add
>   `OPTION(QUASIMODO_TERRAIN "..." ON)`)
>
> Forbidden files: anything else.
>
> Deliverables:
> 1. CVars from handoff section 4.7 / rules 04 are registered with
>    `Cvar_Get`.
> 2. Commands from handoff section 4 ("Commands" table) are
>    registered with `Cmd_AddCommand` and currently print
>    `terrain not implemented yet` style messages.
> 3. `Terrain_Init`, `Terrain_Shutdown`, `Terrain_LoadJungle`,
>    `Terrain_Unload`, `Terrain_IsLoaded` exist and are no-ops.
> 4. `terrain_stub.cpp` provides the same symbols when
>    `QUASIMODO_TERRAIN` is `OFF`.
> 5. Build succeeds.
>
> Stop condition: Phase 1 deliverables met, build succeeds, files
> outside the allowed list are untouched, and forbidden-string scan
> on changed files passes. Report exact files added/changed.

---

## P1-REVIEW — Phase 1 review (reviewer model)

> Review the Phase 1 diff for the terrain skeleton. Confirm:
>
> 1. Only files allowed by Phase 1 in
>    `.cursor/docs/terrain-implementation-handoff.md` section 6 are
>    touched.
> 2. CVars and commands match the handoff list and the rules in
>    `.cursor/rules/04-terrain-water-system.mdc`.
> 3. `terrain_enable 0` invariant holds (no Vulkan calls, no asset
>    loads, no collision changes).
> 4. No absolute local paths.
> 5. No external reference paths or names.
> 6. No new third-party dependencies.
> 7. Build still succeeds with `QUASIMODO_TERRAIN OFF` (stub path).
> 8. `git status --short` is clean apart from the listed files.
>
> Output: pass/fail per check + a short list of required changes.

---

## P2-IMPL — Phase 2 implementation (Cursor Auto)

> Implement Phase 2: `.jungle` parser + asset loading. Read
> `.cursor/docs/terrain-implementation-handoff.md` sections 5 and 6
> (Phase 2) before editing.
>
> Allowed files:
> - `src/client/terrain/terrain_jungle.cpp` (new)
> - `src/client/terrain/terrain_asset.cpp` (new)
> - `src/client/terrain/jungle_json.h` (new, internal small JSON
>   helper, no third-party dependency)
> - `src/CMakeLists.txt`
>
> Forbidden files: vkpt sources, shaders, RmlUi, collision sites,
> any other terrain file. (You may extend the public API in
> `terrain.h` only by ADDING `Terrain_LoadJungleFromBuffer` and
> internal asset helpers; do not change existing signatures.)
>
> Deliverables:
> 1. `.jungle` files load via `FS_LoadFileEx` and are parsed into
>    an in-memory `terrain_jungle_t` struct.
> 2. Missing `.jungle`: silent (`Com_DPrintf` only).
> 3. Invalid `.jungle`: `Com_EPrintf` with field+value, terrain
>    disables for the current map.
> 4. Heightmap and splatmap assets are loaded into CPU buffers
>    using stb_image (already linked via `stb` interface). No
>    Vulkan upload yet.
> 5. `.patch` file format spec lives in a comment block in
>    `terrain_seam.cpp` (placeholder header only) and a parser
>    stub in `terrain_jungle.cpp` validates the field but does not
>    tessellate yet.
> 6. Build succeeds.
>
> Stop condition: Phase 2 deliverables met. Forbidden-string scan
> passes. Report exact files added/changed.

---

## P3-IMPL — Phase 3 implementation (Cursor Auto)

> Implement Phase 3: CPU heightfield + seam patches + opt-in trace
> internals. Read handoff section 6 (Phase 3) before editing.
>
> Allowed files:
> - `src/client/terrain/terrain_heightmap.cpp` (new)
> - `src/client/terrain/terrain_seam.cpp` (new)
> - `src/client/terrain/terrain_collision.cpp` (new)
> - `src/CMakeLists.txt`
>
> Forbidden files: collision call sites in
> `src/server/world.c` and `src/client/predict.c` (those move in
> Phase 7), any vkpt source, shaders, RmlUi.
>
> Deliverables:
> 1. `Terrain_SampleHeight(x, y)` and `Terrain_SampleNormal(x, y)`
>    return correct values for a loaded heightmap.
> 2. `Terrain_RaycastHeightfield(start, end, mins, maxs, contentmask,
>    out)` returns a partial trace. When terrain is not loaded it
>    returns a clean miss.
> 3. `Terrain_PointContents(p)` returns 0 when terrain is not loaded.
> 4. Seam `.patch` files tessellate to vertex/index arrays in CPU
>    memory; no Vulkan upload yet.
> 5. `terrain_probe` console command (already registered in Phase 1)
>    now prints height, normal, and material slot under the camera.
> 6. Build succeeds.
>
> Stop condition: Phase 3 deliverables met. `terrain_collision 0`
> invariant holds (the dispatcher is internal and not yet wired to
> game code). Report exact files added/changed.

---

## P4-IMPL — Phase 4 implementation (Cursor Auto)

> Implement Phase 4: chunk grid + LOD + debug overlay. Read handoff
> section 6 (Phase 4) before editing.
>
> Allowed files:
> - `src/client/terrain/terrain_chunk.cpp` (new)
> - `src/client/terrain/terrain_lod.cpp` (new)
> - `src/client/terrain/terrain_debug.cpp` (new)
> - `src/CMakeLists.txt`
>
> Forbidden files: vkpt sources, shaders, RmlUi, any collision call
> site.
>
> Deliverables:
> 1. CPU-built chunk grid with per-chunk AABB and frustum-culled
>    visible list.
> 2. Distance LOD with optional vertex-shader morph weight stored
>    per vertex.
> 3. Shared LOD index buffers in CPU memory; one buffer per LOD
>    level. Skirt indices generated for each chunk.
> 4. Debug overlay via existing `vkpt_debugdraw_*` line API:
>    - `terrain_show_chunks 1`: chunk AABB lines
>    - `terrain_show_lod 1`: color-coded LOD per chunk
>    - `terrain_show_seams 1`: seam patch outlines
> 5. Build succeeds.
>
> Stop condition: Phase 4 deliverables met. `terrain_enable 0`
> invariant holds. Report exact files added/changed.

---

## P5-PREFLIGHT — Phase 5 Vulkan preflight (reviewer model)

> Phase 5 is the first Vulkan-touching phase. Before implementation,
> produce a preflight review that:
>
> 1. Re-reads `src/refresh/vkpt/main.c` around the
>    `vkpt_initialization[]` table (~lines 244–281),
>    `R_BeginRegistration_RTX` (~5252), and `R_RenderFrame_RTX`
>    world-BLAS block (~3700).
> 2. Re-reads `src/refresh/vkpt/path_tracer.c` around
>    `vkpt_pt_instance_model_blas` and `vkpt_pt_create_toplevel`.
> 3. Confirms the Phase 5 plan in
>    `.cursor/docs/terrain-implementation-handoff.md` section 6 is
>    still accurate (BLAS-first, raster optional).
> 4. Lists every Vulkan object that the terrain subsystem will own
>    and its lifetime: chunk vertex buffer, chunk index buffer,
>    chunk BLAS, seam vertex/index buffer, seam BLAS.
> 5. Confirms no swapchain-dependent objects are required for the
>    BLAS-only path.
> 6. Verifies that `Terrain_InstanceBLAS` insertion is inside
>    `if (bsp_world_model && render_world)` and uses the same
>    `vkpt_pt_instance_model_blas` API as the existing
>    `bsp_mesh_world.geom_*` instances.
> 7. Lists exact insertion sites with line ranges (will move with
>    the file; recompute at edit time).
>
> Output: a short "go / no-go" decision with the list of risks.
> Do not edit code.

---

## P5-IMPL — Phase 5 implementation (Cursor Auto)

> Implement Phase 5: Vulkan terrain rendering, BLAS-first.
> Read the preflight (P5-PREFLIGHT) and handoff section 6 (Phase 5)
> before editing.
>
> Allowed files:
> - `src/client/terrain/terrain_render.cpp` (new)
> - `src/refresh/vkpt/main.c` (only the three insertion points
>   below)
> - `src/CMakeLists.txt` (only to add new shaders if used)
> - Optional only when raster validation is required:
>   `src/refresh/vkpt/shader/terrain.vert`,
>   `src/refresh/vkpt/shader/terrain.frag`
>
> Forbidden files: `src/refresh/vkpt/path_tracer.c`,
> `src/refresh/vkpt/bsp_mesh.c`, every other vkpt source, RmlUi,
> any collision file.
>
> Insertion sites in `src/refresh/vkpt/main.c`:
> 1. Add `{"terrain", Terrain_InitVk, Terrain_DestroyVk,
>    VKPT_INIT_DEFAULT, 0}` (and a `terrain|` entry only if pipeline
>    objects are introduced) to `vkpt_initialization[]`.
> 2. After `vkpt_vertex_buffer_upload_bsp_mesh` in
>    `R_BeginRegistration_RTX`, call
>    `Terrain_OnMapLoaded(name)` (which performs
>    `Terrain_LoadJungle(name)` if `terrain_enable->integer`).
> 3. In `R_RenderFrame_RTX` world-BLAS block, after the existing
>    five `vkpt_pt_instance_model_blas` calls, add
>    `if (terrain_enable->integer) Terrain_InstanceBLAS();`.
>
> Deliverables:
> 1. Per-chunk static BLAS built at load time. Uses the
>    `accel_struct_t` / `model_geometry_t` pattern from
>    `src/refresh/vkpt/path_tracer.c`. No new Vulkan abstraction.
> 2. Per-frame: visible chunks emit one `vkpt_pt_instance_model_blas`
>    each, gated by `terrain_enable`.
> 3. Debug names on every new Vulkan object via
>    `vkSetDebugUtilsObjectNameEXT`.
> 4. Resize/alt-tab safe: BLAS data is independent of swapchain.
> 5. Shutdown safe: full destroy from the registered destroy
>    callback.
> 6. Zero Vulkan validation errors at: cold start, map load with and
>    without `.jungle`, alt-tab, shader reload, shutdown.
> 7. Build succeeds.
>
> Stop condition: Phase 5 deliverables met. `terrain_enable 0`
> invariant holds (no terrain Vulkan objects created). Report exact
> files added/changed.

---

## P6-IMPL — Phase 6 implementation (Cursor Auto)

> Implement Phase 6: water. Read handoff section 6 (Phase 6) before
> editing.
>
> Allowed files:
> - `src/client/terrain/terrain_water.cpp` (new)
> - `src/refresh/vkpt/shader/water.glsl` (parameter-only changes;
>   prefer extending the UBO in
>   `src/refresh/vkpt/shader/global_ubo.h` only if absolutely needed)
>
> Forbidden files: `src/refresh/vkpt/path_tracer.c`,
> `src/refresh/vkpt/bsp_mesh.c`, RmlUi.
>
> Deliverables:
> 1. One static BLAS for the ocean plane at `water.level`, clipped
>    by water mask.
> 2. Optional volume water bodies (Phase 6b stretch goal) emit
>    additional BLAS instances.
> 3. Refraction reuses existing `reflect_refract.rgen`. No new
>    pipeline created.
> 4. `terrain_water 0` produces zero water rendering and zero water
>    collision contribution.
> 5. `Terrain_PointContents` returns `CONTENTS_WATER` when below
>    `water.level` inside the mask, only when `terrain_water 1`.
> 6. Build succeeds.
>
> Stop condition: Phase 6 deliverables met. Report exact files
> added/changed.

---

## P7-IMPL — Phase 7 implementation (Cursor Auto)

> Implement Phase 7: BSP + terrain hybrid. Wire the collision
> dispatcher at the known call sites. Read handoff section 6
> (Phase 7) before editing.
>
> Allowed files:
> - `src/server/world.c` (only at the existing `CM_BoxTrace` and
>   `CM_PointContents` call sites; wrap with the dispatcher)
> - `src/client/predict.c` (same pattern)
> - `src/refresh/vkpt/main.c` (cleanup symmetry only — never the
>   render path)
>
> Forbidden files: `src/common/cmodel.c`, `src/common/pmove.c`,
> RmlUi, any other vkpt source.
>
> Deliverables:
> 1. `CM_BoxTrace` and `CM_PointContents` calls in `world.c` and
>    `predict.c` are wrapped by `Terrain_DispatchTrace` /
>    `Terrain_DispatchPointContents`.
> 2. The dispatcher returns the original BSP result when
>    `terrain_collision 0`, byte-identical to pre-change behavior.
> 3. When `terrain_collision 1`, the dispatcher merges BSP and
>    heightfield trace results, returning the closer hit.
> 4. Old BSP maps load and play unchanged.
> 5. Build succeeds.
>
> Stop condition: Phase 7 deliverables met. Report exact files
> added/changed and the exact list of wrapped call sites.

---

## P8-IMPL — Phase 8 implementation (Cursor Auto)

> Implement Phase 8: terrain materials and splatmap blending.
> Read handoff section 6 (Phase 8) and
> `.cursor/rules/05-pbr-materials-lighting.mdc` before editing.
>
> Allowed files:
> - `src/refresh/vkpt/shader/path_tracer.rchit` (terrain branch
>   only; do not modify the BSP branch)
> - `src/client/terrain/terrain_render.cpp` (uniform/material wiring)
> - `baseq2/terrain/<map>/materials.json` (per-map asset)
>
> Forbidden files: `src/refresh/vkpt/path_tracer.c` (apart from
> the rchit entry), `src/refresh/vkpt/bsp_mesh.c`, RmlUi.
>
> Deliverables:
> 1. `materials.json` parses per the schema in
>    `.cursor/rules/05-pbr-materials-lighting.mdc`.
> 2. Splatmap channels select up to 8 layers; contrast-based
>    blending implemented in the rchit terrain branch.
> 3. Slope and height auto-overrides work as described in rule 05.
> 4. `terrain_material_debug 1` visualizes the splatmap channels.
> 5. Build succeeds.
>
> Stop condition: Phase 8 deliverables met. Report exact files
> added/changed.

---

## P9-IMPL — Phase 9 implementation (Cursor Auto)

> Implement Phase 9: tooling and debug. Read handoff section 6
> (Phase 9).
>
> Allowed files:
> - `src/client/terrain/terrain_debug.cpp`
>
> Forbidden files: vkpt, shaders, RmlUi.
>
> Deliverables:
> 1. `terrain_load <path>`, `terrain_unload`, `terrain_reload`,
>    `terrain_info`, `terrain_probe`, `terrain_dump_chunks`,
>    `terrain_rebuild` all functional.
> 2. Debug overlay stats reachable via `terrain_info` (chunk count,
>    LOD distribution, seam count, water enabled, collision enabled,
>    materials count, memory tag size).
> 3. Build succeeds.
>
> Stop condition: Phase 9 deliverables met.

---

## VERIFY — Generic verify-phase-result prompt

> Verify the result of the most recent phase. Use
> `.cursor/docs/terrain-implementation-handoff.md` section 8
> (validation checklist).
>
> Steps:
> 1. `git status --short` and `git diff --stat`.
> 2. Confirm only phase-allowed files are modified.
> 3. Re-run the build:
>    `cmake --build build --config RelWithDebInfo --target client`.
> 4. Scan changed files for forbidden path patterns: Windows
>    drive-letter prefixes, POSIX user-home prefixes, the workspace
>    parent folder name, the external reference parent folder name,
>    and any external reference local path.
> 5. Confirm no copied reference code (no `idTech`, `Crytek`,
>    `Lumberyard`, `OpenQ4` text in new source).
> 6. Confirm `terrain_enable 0`, `terrain_collision 0`,
>    `terrain_water 0`, `ui_rmlui 0` invariants still hold by
>    cross-checking the diff.
> 7. Output: pass/fail per check, list of files changed,
>    list of files intentionally untouched.

---

## FIX-COMPILE — Generic compile-error fix prompt

> Compile error encountered. Fix only the compile errors. Do not
> add new features, refactor, or change architecture.
>
> Constraints:
> - Edit only files needed to make the build pass.
> - Stay within the current phase's allowed file list.
> - Do not introduce new third-party dependencies.
> - Do not invent APIs; use only those already present in
>   `src/refresh/vkpt/vkpt.h`, `src/refresh/vkpt/vk_util.h`,
>   `inc/common/files.h`, `inc/common/zone.h`, `inc/common/cvar.h`,
>   `inc/common/cmd.h`, `inc/shared/shared.h`.
> - If an unknown symbol is used, search the codebase before
>   inventing one.
> - Re-run the build and report exact result.

---

## GIT-HYGIENE — Generic git/vendor hygiene check

> Run the Git Safe Commit Push skill
> (`.cursor/skills/git-safe-commit-push/SKILL.md`) and the
> Vendor SDK Hygiene skill
> (`.cursor/skills/vendor-sdk-hygiene/SKILL.md`).
>
> Confirm:
> 1. No SDK binaries staged.
> 2. No DLL/LIB/PDB staged.
> 3. No build output folders staged.
> 4. No external reference paths in any committed file.
> 5. No absolute local paths anywhere in the diff.
> 6. `.gitignore`, `.cursorignore`, `.cursorindexingignore` not
>    modified unless explicitly requested.
>
> Output: list of staged files, list of files that were rejected,
> push readiness verdict.
