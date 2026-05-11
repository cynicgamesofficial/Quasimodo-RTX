/*
 * Quasimodo RTX — terrain subsystem (Phase 3: CPU heightfield + seams + internal traces).
 */

#include "terrain.h"
#include "terrain_internal.h"

extern "C" {
#include "common/cmd.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/files.h"
}

#include <string.h>

extern bool TerrainJungle_LoadFromVFS(const char *vfs_path, char *errbuf, size_t errbuf_sz);
extern void TerrainJungle_UnloadAll(void);
extern void TerrainJungle_PrintLoaded(void);
extern const jungle_document_t *TerrainJungle_GetLoaded(void);

static bool terrain_registered = false;
static bool terrain_loaded = false;

static char terrain_last_jungle_vfs[MAX_OSPATH];

#if QUASIMODO_TERRAIN

static bool terrain_try_load_seam_meshes(void)
{
    const jungle_document_t *d = TerrainJungle_GetLoaded();
    if (!d || !d->seam_patch_paths || d->seam_patch_paths_count <= 0)
        return true;

    if (!TerrainSeam_LoadFromPatchPaths((const char *const *)d->seam_patch_paths,
                                        d->seam_patch_paths_count)) {
        Com_EPrintf("[TERRAIN] seam CPU tessellation failed (patch errors logged above)\n");
        return false;
    }
    return true;
}

static void terrain_phase4_refresh_chunks(void)
{
    if (!TerrainChunks_Build()) {
        Com_EPrintf("[TERRAIN] chunk grid build failed\n");
        return;
    }

    const jungle_document_t *d = TerrainJungle_GetLoaded();
    if (d && d->terrain_scale_xy > 0.f && d->terrain_width >= 2 && d->terrain_height >= 2) {
        vec3_t ref;
        ref[0] = d->terrain_origin[0] + 0.5f * (float)(d->terrain_width - 1) * d->terrain_scale_xy;
        ref[1] = d->terrain_origin[1] + 0.5f * (float)(d->terrain_height - 1) * d->terrain_scale_xy;
        ref[2] = d->terrain_origin[2];
        TerrainLOD_SetReferenceWorld(ref);
    }
}

bool Terrain_Internal_GetActiveHeightfield(terrain_heightfield_cpu_t *out)
{
    if (!out || !terrain_registered || !terrain_enable || !terrain_enable->integer)
        return false;

    const jungle_document_t *d = TerrainJungle_GetLoaded();
    if (!d || !d->cpu_heightmap || d->terrain_width < 2 || d->terrain_height < 2)
        return false;

    if (!TerrainHeightmap_ValidateCpuBuffer(d->terrain_width, d->terrain_height,
                                            d->terrain_height_format, d->cpu_heightmap_bytes))
        return false;

    out->width = d->terrain_width;
    out->height = d->terrain_height;
    Q_strlcpy(out->height_format, d->terrain_height_format, sizeof(out->height_format));
    out->scale_xy = d->terrain_scale_xy;
    out->scale_z = d->terrain_scale_z;
    VectorCopy(d->terrain_origin, out->origin);
    out->pixels = d->cpu_heightmap;
    out->pixel_bytes = d->cpu_heightmap_bytes;
    return true;
}

bool Terrain_SampleHeight(float world_x, float world_y, float *out_z)
{
    if (!out_z)
        return false;
    terrain_heightfield_cpu_t hf;
    if (!Terrain_Internal_GetActiveHeightfield(&hf))
        return false;
    return TerrainHeightmap_SampleHeight(&hf, world_x, world_y, out_z);
}

bool Terrain_SampleNormal(float world_x, float world_y, vec3_t out_normal)
{
    terrain_heightfield_cpu_t hf;
    if (!Terrain_Internal_GetActiveHeightfield(&hf))
        return false;
    return TerrainHeightmap_SampleNormal(&hf, world_x, world_y, out_normal);
}

#if QUASIMODO_TERRAIN && defined(QUASIMODO_JOLT_PHYSICS) && QUASIMODO_JOLT_PHYSICS
#include "physics_jolt.h"

static void terrain_physics_jolt_destroy_hf(void)
{
    PhysicsJolt_DestroyTerrainHeightfield();
}

static void terrain_physics_jolt_sync_heightfield(void)
{
    terrain_physics_jolt_destroy_hf();
    PhysicsJolt_ResetTerrainCompareStats();
    if (!PhysicsJolt_Init())
        return;

    terrain_heightfield_cpu_t hf;
    if (!Terrain_Internal_GetActiveHeightfield(&hf))
        return;

    if (hf.width != hf.height) {
        static bool s_warned_rect;
        if (!s_warned_rect) {
            Com_Printf("[TERRAIN] Jolt heightfield compare: CPU heightfield %dx%d is not square — Jolt mesh disabled.\n",
                       hf.width, hf.height);
            s_warned_rect = true;
        }
        return;
    }

    const int dim = hf.width;
    float *const heights = new float[(size_t)dim * (size_t)dim];
    for (int iy = 0; iy < dim; iy++) {
        for (int ix = 0; ix < dim; ix++) {
            float z = 0.f;
            if (!TerrainHeightmap_SampleTexel(&hf, ix, iy, &z)) {
                delete[] heights;
                return;
            }
            heights[(size_t)ix + (size_t)iy * (size_t)dim] = z;
        }
    }

    const int ok = PhysicsJolt_BuildTerrainHeightfieldSquare(dim, heights, hf.origin, hf.scale_xy);
    delete[] heights;

    if (ok)
        Com_Printf("[TERRAIN] Jolt heightfield built (%d x %d) for diagnostic compare\n", dim, dim);
    else
        Com_EPrintf("[TERRAIN] Jolt heightfield build failed (compare will skip until fixed)\n");
}
#elif QUASIMODO_TERRAIN
static void terrain_physics_jolt_destroy_hf(void) {}
static void terrain_physics_jolt_sync_heightfield(void) {}
#endif

#endif /* QUASIMODO_TERRAIN */

cvar_t *terrain_enable;
cvar_t *terrain_collision;
cvar_t *terrain_collision_backend;
cvar_t *terrain_water;
cvar_t *terrain_debug;
cvar_t *terrain_wireframe;
cvar_t *terrain_show_chunks;
cvar_t *terrain_show_lod;
cvar_t *terrain_show_seams;
cvar_t *terrain_water_debug;
cvar_t *terrain_water_level;
cvar_t *terrain_lod_bias;
cvar_t *terrain_rtx_instance;
cvar_t *terrain_uv_scale;
cvar_t *terrain_build_blas_on_load;

static bool Terrain_IsSubsystemEnabled(void)
{
    return terrain_enable && terrain_enable->integer;
}

static bool Terrain_IsCollisionEnabled(void)
{
    return terrain_collision && terrain_collision->integer;
}

static void Terrain_RegisterVarsAndCommands(void);

static void Terrain_BuildMapsJunglePath(const char *mapname, char *out, size_t out_sz)
{
    char stripped[MAX_QPATH];
    COM_StripExtension(stripped, mapname, sizeof stripped);
    const char *leaf = COM_SkipPath(stripped);
    Q_snprintf(out, out_sz, "maps/%s.jungle", leaf);
}

static void Terrain_ClearLastPath(void)
{
    terrain_last_jungle_vfs[0] = '\0';
}

#if QUASIMODO_TERRAIN
bool Terrain_Debug_IsSubsystemRegistered(void)
{
    return terrain_registered;
}

const char *Terrain_Debug_GetLastJunglePath(void)
{
    return terrain_last_jungle_vfs;
}

/*
 * Reject Windows drive letters, UNC, and absolute POSIX-/Win-root paths so terrain_load stays VFS-relative.
 */
static bool terrain_path_is_rejected_absolute(const char *p)
{
    if (!p || !p[0])
        return true;
    if (p[0] == '/' || p[0] == '\\')
        return true;
    if (p[0] == '\\' && p[1] == '\\')
        return true;
    if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':' && (p[2] == '/' || p[2] == '\\' || p[2] == '\0'))
        return true;
    return false;
}

static void terrain_refresh_gpu_after_cpu_change(const char *ctx_tag)
{
    Terrain_BuildBLAS();
    Com_Printf("[TERRAIN] %s: GPU positions/BLAS upload issued (includes water plane when enabled)\n", ctx_tag);
}
#endif /* QUASIMODO_TERRAIN */

static void Terrain_Cmd_Load_f(void)
{
    if (!terrain_registered) {
        Com_Printf("[TERRAIN] terrain system not initialized\n");
        return;
    }
    if (!Terrain_IsSubsystemEnabled()) {
        Com_Printf("[TERRAIN] terrain_enable is 0\n");
        return;
    }

    const char *arg = Cmd_Argv(1);
    if (!arg || !arg[0]) {
        Com_Printf("[TERRAIN] usage: terrain_load <mapname | path/to/file.jungle>\n");
        return;
    }
#if QUASIMODO_TERRAIN
    if (Cmd_Argc() > 2)
        Com_Printf("[TERRAIN] terrain_load: extra arguments ignored\n");
#endif

    char vfs[MAX_OSPATH];
    if (strstr(arg, ".jungle")) {
        Q_strlcpy(vfs, arg, sizeof vfs);
    } else {
        Terrain_BuildMapsJunglePath(arg, vfs, sizeof vfs);
    }

#if QUASIMODO_TERRAIN
    if (terrain_path_is_rejected_absolute(vfs)) {
        Com_Printf("[TERRAIN] terrain_load: rejected non-VFS path (use repository-relative path under baseq2; "
                   "absolute paths disallowed)\n");
        return;
    }
#endif

    char err[512];
    TerrainJungle_UnloadAll();
#if QUASIMODO_TERRAIN
    TerrainChunks_Free();
#endif
    terrain_loaded = false;

    if (!TerrainJungle_LoadFromVFS(vfs, err, sizeof err)) {
        Com_EPrintf("[TERRAIN] load failed (%s): %s\n", vfs, err);
        Terrain_ClearLastPath();
        return;
    }

    Q_strlcpy(terrain_last_jungle_vfs, vfs, sizeof terrain_last_jungle_vfs);
    terrain_loaded = true;
#if QUASIMODO_TERRAIN
    terrain_try_load_seam_meshes();
    terrain_phase4_refresh_chunks();
    TerrainWater_OnMapLoadedVk(NULL);
    terrain_refresh_gpu_after_cpu_change("terrain_load");
    terrain_physics_jolt_sync_heightfield();
#endif
    Com_Printf("[TERRAIN] loaded \"%s\"\n", vfs);
}

static void Terrain_Cmd_Unload_f(void)
{
    if (!terrain_registered) {
        Com_Printf("[TERRAIN] terrain system not initialized\n");
        return;
    }
    Terrain_Unload();
    Com_Printf("[TERRAIN] unloaded (CPU jungle cleared; GPU terrain freed when Vulkan terrain was active)\n");
}

static void Terrain_Cmd_Reload_f(void)
{
    if (!terrain_registered) {
        Com_Printf("[TERRAIN] terrain system not initialized\n");
        return;
    }
    if (!Terrain_IsSubsystemEnabled()) {
        Com_Printf("[TERRAIN] terrain_enable is 0\n");
        return;
    }

    if (!terrain_last_jungle_vfs[0]) {
        Com_Printf("[TERRAIN] terrain_reload: no previous .jungle path\n");
        return;
    }

    char saved[MAX_OSPATH];
    Q_strlcpy(saved, terrain_last_jungle_vfs, sizeof saved);

    TerrainJungle_UnloadAll();
#if QUASIMODO_TERRAIN
    TerrainChunks_Free();
#endif
    terrain_loaded = false;

    char err[512];
    if (!TerrainJungle_LoadFromVFS(saved, err, sizeof err)) {
        Com_EPrintf("[TERRAIN] reload failed (%s): %s\n", saved, err);
        Terrain_ClearLastPath();
        return;
    }

    Q_strlcpy(terrain_last_jungle_vfs, saved, sizeof terrain_last_jungle_vfs);
    terrain_loaded = true;
#if QUASIMODO_TERRAIN
    terrain_try_load_seam_meshes();
    terrain_phase4_refresh_chunks();
    TerrainWater_OnMapLoadedVk(NULL);
    terrain_refresh_gpu_after_cpu_change("terrain_reload");
    terrain_physics_jolt_sync_heightfield();
#endif
    Com_Printf("[TERRAIN] reloaded \"%s\"\n", saved);
}

static void Terrain_Cmd_Info_f(void)
{
    if (!terrain_registered) {
        Com_Printf("[TERRAIN] terrain system not initialized\n");
        return;
    }
#if QUASIMODO_TERRAIN
    TerrainDebug_PrintInfo();
#else
    Com_Printf("[TERRAIN] terrain_enable: %d\n", Terrain_IsSubsystemEnabled() ? 1 : 0);
    Com_Printf("[TERRAIN] loaded: %s\n", terrain_loaded ? "yes" : "no");
    if (terrain_last_jungle_vfs[0])
        Com_Printf("[TERRAIN] last path: %s\n", terrain_last_jungle_vfs);
    if (terrain_loaded)
        TerrainJungle_PrintLoaded();
#endif
}

static void Terrain_Cmd_Probe_f(void)
{
    if (!terrain_registered) {
        Com_Printf("[TERRAIN] terrain system not initialized\n");
        return;
    }

    Com_Printf("[TERRAIN] --- terrain_probe ---\n");
    Com_Printf("[TERRAIN] subsystem registered: %s\n", Terrain_Debug_IsSubsystemRegistered() ? "yes" : "no");
    Com_Printf("[TERRAIN] loaded: %s\n", terrain_loaded ? "yes" : "no");
    Com_Printf("[TERRAIN] terrain_enable: %d\n", Terrain_IsSubsystemEnabled() ? 1 : 0);
    Com_Printf("[TERRAIN] terrain_collision: %d\n", Terrain_IsCollisionEnabled() ? 1 : 0);
    Com_Printf("[TERRAIN] collision mode: hull-bottom heightfield + footprint support (no full OBB walls)\n");
#if QUASIMODO_TERRAIN
    Com_Printf("[TERRAIN] dedicated process (COM_DEDICATED): %d\n", COM_DEDICATED ? 1 : 0);
#endif
    Com_Printf("[TERRAIN] terrain_water cvar: %d\n", terrain_water && terrain_water->integer ? 1 : 0);

    if (terrain_last_jungle_vfs[0])
        Com_Printf("[TERRAIN] last jungle path: %s\n", terrain_last_jungle_vfs);
    else
        Com_Printf("[TERRAIN] last jungle path: (none)\n");

#if QUASIMODO_TERRAIN
    const jungle_document_t *d = TerrainJungle_GetLoaded();
    if (d) {
        static const char *modes[] = { "terrain_only", "bsp_terrain", "bsp_only" };
        const int mi = (int)d->mode;
        if (mi >= 0 && mi < 3)
            Com_Printf("[TERRAIN] jungle mode: %s\n", modes[mi]);
        else
            Com_Printf("[TERRAIN] jungle mode: unknown (%d)\n", mi);
        Com_Printf("[TERRAIN] water (jungle): enabled=%d level=%.3f\n", d->water_enabled ? 1 : 0, d->water_level);
        Com_Printf("[TERRAIN] seam patch refs: %d\n", d->seam_patch_paths_count);
    } else {
        Com_Printf("[TERRAIN] jungle document: (not resident)\n");
    }

    float px = 0.f, py = 0.f;
    bool have_xy = false;
    if (Terrain_Debug_TryGetClientViewXY(&px, &py)) {
        have_xy = true;
        Com_Printf("[TERRAIN] probe XY source: cl.refdef.vieworg (active client view)\n");
    }
    if (!have_xy && d && d->cpu_heightmap && d->terrain_width >= 2 && d->terrain_height >= 2 && d->terrain_scale_xy > 0.f) {
        px = d->terrain_origin[0] + 0.5f * (float)(d->terrain_width - 1) * d->terrain_scale_xy;
        py = d->terrain_origin[1] + 0.5f * (float)(d->terrain_height - 1) * d->terrain_scale_xy;
        have_xy = true;
        Com_Printf("[TERRAIN] probe XY source: heightfield center (fallback - no reliable active view position)\n");
    }

    if (!Terrain_IsSubsystemEnabled()) {
        Com_Printf("[TERRAIN] sampling skipped (terrain_enable is 0)\n");
    } else if (!have_xy) {
        Com_Printf("[TERRAIN] probe: no sample XY (no heightfield fallback)\n");
    } else {
        Com_Printf("[TERRAIN] probe world XY: %.3f %.3f\n", px, py);

        terrain_heightfield_cpu_t hf;
        bool in_bounds = false;
        if (Terrain_Internal_GetActiveHeightfield(&hf)) {
            const float gx = (px - hf.origin[0]) / hf.scale_xy;
            const float gy = (py - hf.origin[1]) / hf.scale_xy;
            if (gx >= 0.f && gy >= 0.f && gx <= (float)(hf.width - 1) && gy <= (float)(hf.height - 1))
                in_bounds = true;
            Com_Printf("[TERRAIN] heightfield bounds (XY): %s\n", in_bounds ? "inside" : "outside");
        } else {
            Com_Printf("[TERRAIN] heightfield bounds (XY): unknown (no active CPU heightfield)\n");
        }

        float z = 0.f;
        vec3_t n;
        const bool hz = Terrain_SampleHeight(px, py, &z);
        if (hz)
            Com_Printf("[TERRAIN] terrain height at XY: %.3f\n", z);
        else
            Com_Printf("[TERRAIN] terrain height at XY: (out of bounds or unavailable)\n");

        if (Terrain_SampleNormal(px, py, n))
            Com_Printf("[TERRAIN] terrain normal: %.5f %.5f %.5f\n", n[0], n[1], n[2]);
        else
            Com_Printf("[TERRAIN] terrain normal: (unavailable)\n");

        if (terrain_water && terrain_water->integer && hz) {
            vec3_t pw;
            pw[0] = px;
            pw[1] = py;
            pw[2] = z;
            const bool under = TerrainWater_IsUnderwater(pw);
            Com_Printf("[TERRAIN] water test (TerrainWater_IsUnderwater @ sampled Z): %s\n",
                       under ? "under plane (relative to water.level)" : "over / inactive");
        } else if (terrain_water && terrain_water->integer && !hz) {
            Com_Printf("[TERRAIN] water test: skipped (no terrain height sample at XY)\n");
        }

        Com_Printf("[TERRAIN] point contents (terrain): deferred (returns 0 in builds)\n");

        TerrainDebug_ProbeExtra(px, py);
    }
#else
    Com_Printf("[TERRAIN] probe detail requires QUASIMODO_TERRAIN build\n");
#endif
}

static void Terrain_Cmd_DumpChunks_f(void)
{
    if (!terrain_registered) {
        Com_Printf("[TERRAIN] terrain system not initialized\n");
        return;
    }
    if (!Terrain_IsSubsystemEnabled()) {
        Com_Printf("[TERRAIN] terrain_enable is 0\n");
        return;
    }
#if QUASIMODO_TERRAIN
    TerrainDebug_DumpChunks();
#else
    Com_Printf("[TERRAIN] terrain_dump_chunks requires QUASIMODO_TERRAIN build\n");
#endif
}

static void Terrain_Cmd_Rebuild_f(void)
{
    if (!terrain_registered) {
        Com_Printf("[TERRAIN] terrain system not initialized\n");
        return;
    }
    if (!Terrain_IsSubsystemEnabled()) {
        Com_Printf("[TERRAIN] terrain_rebuild: no-op (terrain_enable is 0)\n");
        return;
    }
#if QUASIMODO_TERRAIN
    if (!Terrain_IsLoaded()) {
        Com_Printf("[TERRAIN] terrain_rebuild: no-op (no .jungle loaded)\n");
        return;
    }
    terrain_phase4_refresh_chunks();
    terrain_refresh_gpu_after_cpu_change("terrain_rebuild");
    Com_Printf("[TERRAIN] terrain_rebuild: CPU chunk/LOD state rebuilt\n");
#else
    Com_Printf("[TERRAIN] terrain_rebuild requires QUASIMODO_TERRAIN build\n");
#endif
}

static void Terrain_RegisterVarsAndCommands(void)
{
    terrain_enable = Cvar_Get("terrain_enable", "0", CVAR_ARCHIVE);
    terrain_collision = Cvar_Get("terrain_collision", "0", CVAR_ARCHIVE);
    terrain_collision_backend = Cvar_Get("terrain_collision_backend", "0", CVAR_ARCHIVE);
    terrain_water = Cvar_Get("terrain_water", "0", CVAR_ARCHIVE);
    terrain_debug = Cvar_Get("terrain_debug", "0", 0);
    terrain_wireframe = Cvar_Get("terrain_wireframe", "0", 0);
    terrain_show_chunks = Cvar_Get("terrain_show_chunks", "0", 0);
    terrain_show_lod = Cvar_Get("terrain_show_lod", "0", 0);
    terrain_show_seams = Cvar_Get("terrain_show_seams", "0", 0);
    terrain_water_debug = Cvar_Get("terrain_water_debug", "0", 0);
    terrain_water_level = Cvar_Get("terrain_water_level", "0", CVAR_ARCHIVE);
    terrain_lod_bias = Cvar_Get("terrain_lod_bias", "0", CVAR_ARCHIVE);
    terrain_rtx_instance = Cvar_Get("terrain_rtx_instance", "0", 0);
    terrain_uv_scale = Cvar_Get("terrain_uv_scale", "0.0078125", CVAR_ARCHIVE);
    terrain_build_blas_on_load = Cvar_Get("terrain_build_blas_on_load", "0", CVAR_ARCHIVE);

    Cmd_AddCommand("terrain_load", Terrain_Cmd_Load_f);
    Cmd_AddCommand("terrain_unload", Terrain_Cmd_Unload_f);
    Cmd_AddCommand("terrain_reload", Terrain_Cmd_Reload_f);
    Cmd_AddCommand("terrain_info", Terrain_Cmd_Info_f);
    Cmd_AddCommand("terrain_probe", Terrain_Cmd_Probe_f);
    Cmd_AddCommand("terrain_dump_chunks", Terrain_Cmd_DumpChunks_f);
    Cmd_AddCommand("terrain_rebuild", Terrain_Cmd_Rebuild_f);
}

static void Terrain_UnregisterCommands(void)
{
    Cmd_RemoveCommand("terrain_load");
    Cmd_RemoveCommand("terrain_unload");
    Cmd_RemoveCommand("terrain_reload");
    Cmd_RemoveCommand("terrain_info");
    Cmd_RemoveCommand("terrain_probe");
    Cmd_RemoveCommand("terrain_dump_chunks");
    Cmd_RemoveCommand("terrain_rebuild");
}

void Terrain_Init(void)
{
    if (terrain_registered) {
        return;
    }

    Terrain_RegisterVarsAndCommands();
    terrain_registered = true;
    Terrain_ClearLastPath();
#if defined(QUASIMODO_JOLT_PHYSICS) && QUASIMODO_JOLT_PHYSICS
    PhysicsJolt_Init();
#endif
    Com_Printf("[TERRAIN] terrain system initialized\n");
}

void Terrain_Shutdown(void)
{
    if (!terrain_registered) {
        return;
    }

    Terrain_Unload();
    Terrain_UnregisterCommands();
#if defined(QUASIMODO_JOLT_PHYSICS) && QUASIMODO_JOLT_PHYSICS
    PhysicsJolt_Shutdown();
#endif
    terrain_registered = false;
}

bool Terrain_LoadJungle(const char *mapname)
{
    if (!terrain_registered || !Terrain_IsSubsystemEnabled()) {
        return false;
    }

    char path[MAX_OSPATH];
    Terrain_BuildMapsJunglePath(mapname, path, sizeof path);

    /*
     * Always detach prior map CPU terrain before attempting this map's optional .jungle,
     * including when the file is absent (BSP-only maps must not keep another map's jungle).
     */
    TerrainJungle_UnloadAll();
#if QUASIMODO_TERRAIN
    TerrainChunks_Free();
#endif
    terrain_loaded = false;
    Terrain_ClearLastPath();

    if (!FS_FileExists(path)) {
        Com_DPrintf("[TERRAIN] missing optional %s\n", path);
        return false;
    }

    char err[512];
    if (!TerrainJungle_LoadFromVFS(path, err, sizeof err)) {
        Com_EPrintf("[TERRAIN] failed parsing %s: %s\n", path, err);
        return false;
    }

    Q_strlcpy(terrain_last_jungle_vfs, path, sizeof terrain_last_jungle_vfs);
    terrain_loaded = true;
#if QUASIMODO_TERRAIN
    terrain_try_load_seam_meshes();
    terrain_phase4_refresh_chunks();
    TerrainWater_OnMapLoadedVk(NULL);
    terrain_physics_jolt_sync_heightfield();
#endif
    return true;
}

void Terrain_Unload(void)
{
#if QUASIMODO_TERRAIN
    terrain_physics_jolt_destroy_hf();
    Terrain_OnMapUnload_Vk();
    TerrainChunks_Free();
    TerrainSeam_FreeAll();
#endif
    TerrainJungle_UnloadAll();
    terrain_loaded = false;
    Terrain_ClearLastPath();
}

bool Terrain_IsLoaded(void)
{
    return terrain_loaded;
}

void Terrain_PerFrameBegin(void)
{
    if (!terrain_registered || !Terrain_IsSubsystemEnabled()) {
        return;
    }
#if QUASIMODO_TERRAIN
    TerrainDebug_OnFrameBegin();
    TerrainChunks_UpdateVisibility();
    TerrainLOD_Update();
#endif
}

void Terrain_PerFrameEnd(void)
{
    if (!terrain_registered || !Terrain_IsSubsystemEnabled()) {
        return;
    }
}

void Terrain_InstanceBLAS(void)
{
    if (!terrain_registered || !Terrain_IsSubsystemEnabled()) {
        return;
    }
#if QUASIMODO_TERRAIN
    if (!terrain_rtx_instance || !terrain_rtx_instance->integer) {
        return;
    }
    Terrain_InstanceBLAS_Vk();
#endif
}

void Terrain_PreFrameRenderHook(void)
{
#if QUASIMODO_TERRAIN
    Terrain_RunDeferredGpuUploadIfAny();
#endif
}

void Terrain_TraceLine(trace_t *trace,
                       const vec3_t start, const vec3_t end,
                       const vec3_t mins, const vec3_t maxs,
                       int brushmask)
{
    (void)mins;
    (void)maxs;

    if (!trace)
        return;

    memset(trace, 0, sizeof(*trace));
    trace->fraction = 1.f;

    if (!terrain_registered || !Terrain_IsSubsystemEnabled() || !Terrain_IsCollisionEnabled())
        return;
    if (!(brushmask & CONTENTS_SOLID))
        return;

#if QUASIMODO_TERRAIN
    Terrain_Internal_TraceHeightfieldHull(start, end, mins, maxs, trace);
#endif
}

int Terrain_PointContents(const vec3_t p)
{
    (void)p;

    if (!terrain_registered || !Terrain_IsSubsystemEnabled() || !Terrain_IsCollisionEnabled()) {
        return 0;
    }

    return 0;
}

void Terrain_OnSwapchainRecreate(void)
{
    if (!terrain_registered || !Terrain_IsSubsystemEnabled()) {
        return;
    }
}

void Terrain_OnPipelineReload(void)
{
    if (!terrain_registered || !Terrain_IsSubsystemEnabled()) {
        return;
    }
}
