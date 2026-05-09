/*
 * Quasimodo RTX — terrain debug reporting (Phase 4).
 */

#include "terrain.h"
#include "terrain_internal.h"

#include <math.h>
#include <string.h>

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

void TerrainDebug_PrintInfo(void)
{
    TerrainDebug_OnFrameBegin();

    Com_Printf("[TERRAIN] --- terrain_info ---\n");
    Com_Printf("[TERRAIN] loaded: %s\n", Terrain_IsLoaded() ? "yes" : "no");
    Com_Printf("[TERRAIN] terrain_enable: %d\n", terrain_enable && terrain_enable->integer ? 1 : 0);
    Com_Printf("[TERRAIN] terrain_collision: %d\n", terrain_collision && terrain_collision->integer ? 1 : 0);
    {
        const bool col_active = terrain_enable && terrain_enable->integer && terrain_collision && terrain_collision->integer
            && Terrain_IsLoaded();
        Com_Printf("[TERRAIN] collision active: %s\n", col_active ? "yes" : "no");
    }
    Com_Printf("[TERRAIN] collision mode: heightfield segment (ray), no swept AABB; see Phase 7B\n");
    Com_Printf("[TERRAIN] q2rtxded terrain collision: deferred (dedicated server has no terrain .jungle/CPU data in this build)\n");
    Com_Printf("[TERRAIN] point contents: deferred (no terrain solid/underfoot query in Phase 7B)\n");
    Com_Printf("[TERRAIN] seam collision: deferred (no .patch on jungletest; no seam raycast API)\n");

    const jungle_document_t *d = TerrainJungle_GetLoaded();
    if (d) {
        static const char *modes[] = {"terrain_only", "bsp_terrain", "bsp_only"};
        const int mi = (int)d->mode;
        if (mi >= 0 && mi < 3)
            Com_Printf("[TERRAIN] mode: %s\n", modes[mi]);
        else
            Com_Printf("[TERRAIN] mode: unknown (%d)\n", mi);

        Com_Printf("[TERRAIN] heightmap: %dx%d format \"%s\"\n", d->terrain_width, d->terrain_height,
                   d->terrain_height_format);
        Com_Printf("[TERRAIN] origin: %.3f %.3f %.3f\n", d->terrain_origin[0], d->terrain_origin[1],
                   d->terrain_origin[2]);
        Com_Printf("[TERRAIN] scale_xy: %.6f scale_z: %.6f\n", d->terrain_scale_xy, d->terrain_scale_z);
        Com_Printf("[TERRAIN] chunk_size (jungle): %d\n", d->terrain_chunk_size);
        Com_Printf("[TERRAIN] lod_count (jungle): %d (effective cap %d)\n", d->terrain_lod_count,
                   terrain_effective_lod_cap(d));
        Com_Printf("[TERRAIN] LOD reference: heightfield XY center (Phase 4); camera/player-driven LOD deferred.\n");
    } else {
        Com_Printf("[TERRAIN] jungle document: (not resident)\n");
    }

    int gw = 0, gh = 0;
    TerrainChunks_GetGridDims(&gw, &gh);
    const int total = TerrainChunks_GetNumChunks();
    Com_Printf("[TERRAIN] chunk grid: %d x %d cells, total chunks: %d\n", gw, gh, total);

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
    Com_Printf("[TERRAIN] LOD distribution (current): ");
    if (!arr || total <= 0 || cap_lod <= 0) {
        Com_Printf("(no chunk grid)\n");
    } else {
        for (int L = 0; L < cap_lod; L++) {
            Com_Printf("L%d=%d ", L, lod_hist[L]);
        }
        Com_Printf("\n");
    }

    if (d)
        Com_Printf("[TERRAIN] seam patch refs (jungle): %d\n", d->seam_patch_paths_count);
    Com_Printf("[TERRAIN] seam CPU meshes (tessellated): %d\n", TerrainSeam_GetCpuMeshCount());

    Com_Printf("[TERRAIN] debug show state (synced): chunks=%d lod=%d seams=%d\n", s_dbg_show_chunks,
               s_dbg_show_lod, s_dbg_show_seams);

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

    Com_Printf("[TERRAIN] --- terrain_dump_chunks (%d) ---\n", n);
    for (int i = 0; i < n; i++) {
        const terrain_chunk_t *c = &arr[i];
        Com_Printf(
            "[TERRAIN] chunk[%d] grid=(%d,%d) samples=[%d,%d)-[%d,%d) visible=%d lod=%d tgt=%d dirty=%d "
            "seam_hint=%d\n",
            i, c->grid_x, c->grid_y, c->sample_x0, c->sample_x1_ex, c->sample_y0, c->sample_y1_ex,
            c->visible ? 1 : 0, c->lod_current, c->lod_target, c->dirty ? 1 : 0, c->dbg_seam_hint ? 1 : 0);
        Com_Printf("[TERRAIN]   AABB mins %.3f %.3f %.3f maxs %.3f %.3f %.3f z_min/max %.3f %.3f\n",
                   c->bounds_mins[0], c->bounds_mins[1], c->bounds_mins[2], c->bounds_maxs[0], c->bounds_maxs[1],
                   c->bounds_maxs[2], c->z_min, c->z_max);
    }
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
            return;
        }
    }
    Com_Printf("[TERRAIN] probe chunk: (no chunk covers %.3f %.3f or grid absent)\n", world_x, world_y);
}

#endif /* QUASIMODO_TERRAIN */
