/*
 * Quasimodo RTX — terrain debug reporting (Phase 4).
 */

#include "terrain.h"
#include "terrain_internal.h"

#include <math.h>
#include <string.h>

#if defined(QUASIMODO_JOLT_PHYSICS) && QUASIMODO_JOLT_PHYSICS
#include "physics_jolt.h"
#endif

#if QUASIMODO_TERRAIN

extern "C" {
#include "common/common.h"
#include "common/cvar.h"
}

extern const jungle_document_t *TerrainJungle_GetLoaded(void);

extern cvar_t *terrain_enable;
extern cvar_t *terrain_water;
extern cvar_t *terrain_water_level;
extern cvar_t *terrain_collision;
extern cvar_t *terrain_collision_backend;
extern cvar_t *terrain_show_chunks;
extern cvar_t *terrain_show_lod;
extern cvar_t *terrain_show_seams;

static int s_dbg_show_chunks = 0;
static int s_dbg_show_lod = 0;
static int s_dbg_show_seams = 0;
static bool s_logged_visual_deferred = false;

void TerrainDebug_OnFrameBegin(void)
{
    if (!terrain_show_chunks || !terrain_show_lod || !terrain_show_seams)
        return;

    s_dbg_show_chunks = terrain_show_chunks->integer;
    s_dbg_show_lod = terrain_show_lod->integer;
    s_dbg_show_seams = terrain_show_seams->integer;

    if ((s_dbg_show_chunks || s_dbg_show_lod || s_dbg_show_seams) && !s_logged_visual_deferred) {
        Com_Printf("[TERRAIN] visual chunk/LOD/seam overlay draw deferred (no renderer hook in Phase 4)\n");
        s_logged_visual_deferred = true;
    }

    if (!s_dbg_show_chunks && !s_dbg_show_lod && !s_dbg_show_seams)
        s_logged_visual_deferred = false;
}

static int terrain_effective_lod_cap(const jungle_document_t *d)
{
    if (!d)
        return 1;
    int n = d->terrain_lod_count;
    if (n < 1)
        n = 1;
    if (n > TERRAIN_LOD_LEVEL_CAP)
        n = TERRAIN_LOD_LEVEL_CAP;
    return n;
}

extern cvar_t *terrain_rtx_instance;
extern cvar_t *terrain_build_blas_on_load;

static int terrain_dbg_chunk_tri_count(const terrain_chunk_t *c)
{
    const int nx = c->sample_x1_ex - c->sample_x0;
    const int ny = c->sample_y1_ex - c->sample_y0;
    if (nx < 2 || ny < 2)
        return 0;
    return 2 * (nx - 1) * (ny - 1);
}

void TerrainDebug_PrintInfo(void)
{
    TerrainDebug_OnFrameBegin();

    const char *last_path = Terrain_Debug_GetLastJunglePath();

    Com_Printf("[TERRAIN] --- terrain_info ---\n");
    Com_Printf("[TERRAIN] subsystem registered: %s\n", Terrain_Debug_IsSubsystemRegistered() ? "yes" : "no");
    Com_Printf("[TERRAIN] jungle resident (Terrain_IsLoaded): %s\n", Terrain_IsLoaded() ? "yes" : "no");
    Com_Printf("[TERRAIN] terrain_enable: %d\n", terrain_enable && terrain_enable->integer ? 1 : 0);
    Com_Printf("[TERRAIN] terrain_collision: %d\n", terrain_collision && terrain_collision->integer ? 1 : 0);
    Com_Printf("[TERRAIN] terrain_collision_backend: %d (0=legacy, 1=jolt_compare diagnostics)\n",
               terrain_collision_backend ? terrain_collision_backend->integer : 0);
    Com_Printf("[TERRAIN] terrain_water: %d\n", terrain_water && terrain_water->integer ? 1 : 0);
    Com_Printf("[TERRAIN] terrain_water_level (cvar): %.3f\n", terrain_water_level ? terrain_water_level->value : 0.f);
    Com_Printf("[TERRAIN] terrain_rtx_instance: %d\n", terrain_rtx_instance && terrain_rtx_instance->integer ? 1 : 0);
    Com_Printf("[TERRAIN] terrain_build_blas_on_load: %d\n",
               terrain_build_blas_on_load && terrain_build_blas_on_load->integer ? 1 : 0);
    Com_Printf("[TERRAIN] terrain_debug (verbose dumps): %d\n", terrain_debug && terrain_debug->integer ? 1 : 0);
    Com_Printf("[TERRAIN] dedicated process (COM_DEDICATED): %d\n", COM_DEDICATED ? 1 : 0);

    Com_Printf("[TERRAIN] current .jungle path: %s\n", last_path && last_path[0] ? last_path : "(none)");

    const jungle_document_t *d = TerrainJungle_GetLoaded();
    if (d) {
        static const char *modes[] = { "terrain_only", "bsp_terrain", "bsp_only" };
        const int mi = (int)d->mode;
        if (mi >= 0 && mi < 3)
            Com_Printf("[TERRAIN] terrain mode: %s\n", modes[mi]);
        else
            Com_Printf("[TERRAIN] terrain mode: unknown (%d)\n", mi);

        Com_Printf("[TERRAIN] heightmap dimensions: %d x %d\n", d->terrain_width, d->terrain_height);
        Com_Printf("[TERRAIN] height format: \"%s\"\n", d->terrain_height_format);
        Com_Printf("[TERRAIN] origin: %.3f %.3f %.3f\n", d->terrain_origin[0], d->terrain_origin[1], d->terrain_origin[2]);
        Com_Printf("[TERRAIN] scale_xy: %.6f  scale_z: %.6f\n", d->terrain_scale_xy, d->terrain_scale_z);

        terrain_heightfield_cpu_t hf;
        if (Terrain_Internal_GetActiveHeightfield(&hf)) {
            const float maxx = hf.origin[0] + (float)(hf.width - 1) * hf.scale_xy;
            const float maxy = hf.origin[1] + (float)(hf.height - 1) * hf.scale_xy;
            float zmin = 0.f, zmax = 0.f;
            if (TerrainHeightmap_ComputeChunkZBounds(&hf, 0, 0, hf.width, hf.height, &zmin, &zmax))
                Com_Printf("[TERRAIN] world bounds (approx over heightfield samples): XY [%.3f..%.3f] x [%.3f..%.3f]  Z "
                           "[%.3f..%.3f]\n",
                           hf.origin[0], maxx, hf.origin[1], maxy, zmin, zmax);
            else
                Com_Printf("[TERRAIN] world bounds: XY min (%.3f %.3f) max (%.3f %.3f)  Z (unknown)\n", hf.origin[0],
                           hf.origin[1], maxx, maxy);
        }

        Com_Printf("[TERRAIN] chunk_size (jungle): %d samples per axis per chunk (shared boundary verts)\n",
                   d->terrain_chunk_size);
        Com_Printf("[TERRAIN] jungle water.enabled: %d  water.level: %.3f\n", d->water_enabled ? 1 : 0, d->water_level);
        Com_Printf("[TERRAIN] jungle collision: terrain=%d water=%d seam=%d\n", d->collision_terrain ? 1 : 0,
                   d->collision_water ? 1 : 0, d->collision_seam ? 1 : 0);
        Com_Printf("[TERRAIN] seam patch refs (jungle): %d\n", d->seam_patch_paths_count);
        Com_Printf("[TERRAIN] seam collision (jungle flags): %s\n", d->collision_seam ? "enabled in document" : "disabled in document");
        Com_Printf("[TERRAIN] jungle terrain.splatmap path: %s\n", d->terrain_splatmap[0] ? d->terrain_splatmap : "(none)");
        Com_Printf("[TERRAIN] jungle terrain.materials count: %d\n", d->terrain_materials_count);
        if (d->cpu_splat && d->cpu_splat_bytes > 0)
            Com_Printf("[TERRAIN] CPU splatmap loaded: yes (%dx%d comp=%d)\n", d->cpu_splat_w, d->cpu_splat_h,
                       d->cpu_splat_comp);
        else
            Com_Printf("[TERRAIN] CPU splatmap loaded: no (material channel 0 / flat fallback)\n");
    } else {
        Com_Printf("[TERRAIN] jungle document: (not resident)\n");
    }

    const bool col_active = terrain_enable && terrain_enable->integer && terrain_collision && terrain_collision->integer
        && Terrain_IsLoaded();
    Com_Printf("[TERRAIN] collision active: %s\n", col_active ? "yes" : "no");
    Com_Printf("[TERRAIN] gameplay terrain collision backend: legacy (authoritative); Jolt never replaces traces in "
               "J2A\n");
    Com_Printf("[TERRAIN] jolt_compare active (diagnostics only): %s\n",
               (terrain_collision_backend && terrain_collision_backend->integer == 1) ? "yes" : "no");
#if defined(QUASIMODO_JOLT_PHYSICS) && QUASIMODO_JOLT_PHYSICS
    Com_Printf("[TERRAIN] Jolt compiled into client: yes\n");
    Com_Printf("[TERRAIN] Jolt core initialized (PhysicsJolt_IsAvailable): %d\n", PhysicsJolt_IsAvailable());
    Com_Printf("[TERRAIN] Jolt terrain heightfield built: %s\n", PhysicsJolt_IsTerrainHeightfieldReady() ? "yes" : "no");
    {
        physics_jolt_compare_stats_t s;
        PhysicsJolt_GetTerrainCompareStats(&s);
        Com_Printf("[TERRAIN] J2A compare counters: total=%llu skipped_unavail=%llu legacy_hit_jolt_miss=%llu "
                   "jolt_hit_legacy_miss=%llu frac_mismatch=%llu normal_mismatch=%llu startsolid_mismatch=%llu "
                   "legacy_synth_jolt_ray_miss=%llu\n",
                   (unsigned long long)s.compare_total, (unsigned long long)s.compare_skipped_unavailable,
                   (unsigned long long)s.legacy_hit_jolt_miss, (unsigned long long)s.jolt_hit_legacy_miss,
                   (unsigned long long)s.frac_mismatch, (unsigned long long)s.normal_mismatch,
                   (unsigned long long)s.startsolid_mismatch, (unsigned long long)s.legacy_synth_jolt_ray_miss);
    }
#else
    Com_Printf("[TERRAIN] Jolt compiled into client: no (QUASIMODO_JOLT_PHYSICS off at build)\n");
#endif
    Com_Printf("[TERRAIN] note: legacy synthetic floor / hull-bottom traces remain authoritative for movement\n");
    Com_Printf("[TERRAIN] collision mode: hull-bottom heightfield segment + footprint ground-support (no full swept "
               "OBB walls)\n");
    Com_Printf("[TERRAIN] dedicated server terrain collision: follow terrain_collision + loaded jungle; SV-only "
               "ded console has no camera probe\n");

    Com_Printf("[TERRAIN] point contents (Terrain_PointContents): deferred (returns 0)\n");

    int gw = 0, gh = 0;
    TerrainChunks_GetGridDims(&gw, &gh);
    const int total = TerrainChunks_GetNumChunks();
    Com_Printf("[TERRAIN] chunk grid: %d x %d cells  total chunks: %d\n", gw, gh, total);

    const terrain_chunk_t *arr = TerrainChunks_GetArray();
    int vis = 0;
    const int cap_lod = d ? terrain_effective_lod_cap(d) : 1;
    int lod_hist[TERRAIN_LOD_LEVEL_CAP];
    memset(lod_hist, 0, sizeof(lod_hist));

    if (arr && total > 0 && cap_lod > 0) {
        for (int i = 0; i < total; i++) {
            if (arr[i].visible)
                vis++;
            const int lc = arr[i].lod_current;
            if (lc >= 0 && lc < cap_lod && lc < TERRAIN_LOD_LEVEL_CAP)
                lod_hist[lc]++;
        }
    }

    Com_Printf("[TERRAIN] visible chunks: %d\n", vis);
    Com_Printf("[TERRAIN] LOD histogram (current lod per chunk): ");
    if (!arr || total <= 0 || cap_lod <= 0) {
        Com_Printf("(no chunk grid)\n");
    } else {
        for (int L = 0; L < cap_lod; L++)
            Com_Printf("L%d=%d ", L, lod_hist[L]);
        Com_Printf("\n");
    }

    Com_Printf("[TERRAIN] LOD reference mode: heightfield XY center (camera/player LOD deferred)\n");

    if (d)
        Com_Printf("[TERRAIN] lod_count (jungle): %d (effective cap %d)\n", d->terrain_lod_count,
                   terrain_effective_lod_cap(d));

    Com_Printf("[TERRAIN] seam CPU meshes (tessellated patches): %d\n", TerrainSeam_GetCpuMeshCount());

    Com_Printf("[TERRAIN] material summary: CPU dominant channel when splat CPU present; otherwise flat/ch0; shader "
               "splat blending deferred\n");

    Com_Printf("[TERRAIN] limitation: shader splat blending deferred; terrain point-contents API stub; seam GPU ray "
               "patch deferred\n");

    Com_Printf("[TERRAIN] debug overlays (chunk/lod/seam): chunks=%d lod=%d seams=%d (visual hook deferred)\n",
               s_dbg_show_chunks, s_dbg_show_lod, s_dbg_show_seams);

    TerrainRender_DebugAppendInfo();
}

void TerrainDebug_DumpChunks(void)
{
    TerrainDebug_OnFrameBegin();

    const terrain_chunk_t *arr = TerrainChunks_GetArray();
    const int n = TerrainChunks_GetNumChunks();
    if (!arr || n <= 0) {
        Com_Printf("[TERRAIN] terrain_dump_chunks: no chunk grid\n");
        return;
    }

    const bool full = terrain_debug && terrain_debug->integer;
    const int DETAIL_CAP = 48;
    const int detail_n = full ? n : (n > DETAIL_CAP ? DETAIL_CAP : n);

    int vis = 0;
    int blas_ok = 0;
    for (int i = 0; i < n; i++) {
        if (arr[i].visible)
            vis++;
        if (TerrainRender_ChunkBlasValid(i))
            blas_ok++;
    }

    Com_Printf("[TERRAIN] --- terrain_dump_chunks ---\n");
    Com_Printf("[TERRAIN] summary: total=%d visible=%d chunks_w_valid_rtx_BLAS=%d (after last GPU upload)\n", n, vis,
               blas_ok);
    if (!full && n > DETAIL_CAP)
        Com_Printf("[TERRAIN] listing first %d chunks (terrain_debug 1 for full %d-chunk dump)\n", detail_n, n);
    else
        Com_Printf("[TERRAIN] listing %d chunk(s)\n", detail_n);

    for (int i = 0; i < detail_n; i++) {
        const terrain_chunk_t *c = &arr[i];
        const int nvx = c->sample_x1_ex - c->sample_x0;
        const int nvy = c->sample_y1_ex - c->sample_y0;
        const int nqx = nvx > 1 ? nvx - 1 : 0;
        const int nqy = nvy > 1 ? nvy - 1 : 0;
        const int tri = terrain_dbg_chunk_tri_count(c);
        const int blas = TerrainRender_ChunkBlasValid(i) ? 1 : 0;

        Com_Printf(
            "[TERRAIN] chunk[%d] grid=(%d,%d) flat=%d  cells/samples x[%d,%d) y[%d,%d) verts=%dx%d quads=%dx%d tri=%d "
            "vis=%d lod=%d/%d dirty=%d seam_hint=%d rtx_BLAS=%d\n",
            i, c->grid_x, c->grid_y, c->flat_index, c->sample_x0, c->sample_x1_ex, c->sample_y0, c->sample_y1_ex, nvx,
            nvy, nqx, nqy, tri, c->visible ? 1 : 0, c->lod_current, c->lod_target, c->dirty ? 1 : 0,
            c->dbg_seam_hint ? 1 : 0, blas);
        Com_Printf("[TERRAIN]   AABB mins %.3f %.3f %.3f  maxs %.3f %.3f %.3f  z_span %.3f..%.3f\n", c->bounds_mins[0],
                   c->bounds_mins[1], c->bounds_mins[2], c->bounds_maxs[0], c->bounds_maxs[1], c->bounds_maxs[2], c->z_min,
                   c->z_max);
    }

    if (!full && n > DETAIL_CAP)
        Com_Printf("[TERRAIN] (%d chunks omitted; set terrain_debug 1 for full dump)\n", n - DETAIL_CAP);
}

void TerrainDebug_ProbeExtra(float world_x, float world_y)
{
    int ci = -1;
    if (TerrainChunks_FindAtWorldXY(world_x, world_y, &ci)) {
        const terrain_chunk_t *arr = TerrainChunks_GetArray();
        if (arr && ci >= 0 && ci < TerrainChunks_GetNumChunks()) {
            const terrain_chunk_t *c = &arr[ci];
            Com_Printf("[TERRAIN] probe chunk flat_index=%d grid=(%d,%d) lod_cur=%d lod_tgt=%d visible=%d\n", ci,
                       c->grid_x, c->grid_y, c->lod_current, c->lod_target, c->visible ? 1 : 0);
            TerrainRender_DebugProbeSplatWorld(world_x, world_y);
            return;
        }
    }
    Com_Printf("[TERRAIN] probe chunk: (no chunk covers %.3f %.3f or grid absent)\n", world_x, world_y);
}

#endif /* QUASIMODO_TERRAIN */
