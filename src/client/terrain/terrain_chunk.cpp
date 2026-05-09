/*
 * Quasimodo RTX — CPU terrain chunk grid (Phase 4).
 */

#include "terrain.h"
#include "terrain_internal.h"

#include <math.h>

#if QUASIMODO_TERRAIN

extern "C" {
#include "common/common.h"
#include "common/cvar.h"
#include "common/zone.h"
}

extern const jungle_document_t *TerrainJungle_GetLoaded(void);

static terrain_chunk_t *s_chunks = nullptr;
static int s_chunk_count = 0;
static int s_grid_w = 0;
static int s_grid_h = 0;

void TerrainChunks_Free(void)
{
    if (s_chunks) {
        Z_Free(s_chunks);
        s_chunks = nullptr;
    }
    s_chunk_count = 0;
    s_grid_w = 0;
    s_grid_h = 0;
}

int TerrainChunks_GetNumChunks(void)
{
    return s_chunk_count;
}

void TerrainChunks_GetGridDims(int *out_gw, int *out_gh)
{
    if (out_gw)
        *out_gw = s_grid_w;
    if (out_gh)
        *out_gh = s_grid_h;
}

const terrain_chunk_t *TerrainChunks_GetArray(void)
{
    return s_chunks;
}

terrain_chunk_t *TerrainChunks_GetArrayMutable(void)
{
    return s_chunks;
}

bool TerrainChunks_FindAtWorldXY(float world_x, float world_y, int *out_chunk_index)
{
    if (!out_chunk_index || !s_chunks || s_chunk_count <= 0 || s_grid_w <= 0)
        return false;

    const jungle_document_t *d = TerrainJungle_GetLoaded();
    if (!d || d->terrain_scale_xy <= 0.f)
        return false;

    const float sx = d->terrain_scale_xy;
    const float ox = d->terrain_origin[0];
    const float oy = d->terrain_origin[1];
    const int cw = d->terrain_width;
    const int ch = d->terrain_height;
    const int cs = d->terrain_chunk_size;

    const float gx = (world_x - ox) / sx;
    const float gy = (world_y - oy) / sx;
    if (gx < 0.f || gy < 0.f || gx > (float)(cw - 1) || gy > (float)(ch - 1))
        return false;

    const int ix = (int)floorf(gx);
    const int iy = (int)floorf(gy);
    const int cx = ix / cs;
    const int cy = iy / cs;
    if (cx < 0 || cy < 0 || cx >= s_grid_w || cy >= s_grid_h)
        return false;

    *out_chunk_index = cy * s_grid_w + cx;
    return true;
}

bool TerrainChunks_Build(void)
{
    TerrainChunks_Free();

    if (!terrain_enable || !terrain_enable->integer)
        return false;

    terrain_heightfield_cpu_t hf;
    if (!Terrain_Internal_GetActiveHeightfield(&hf))
        return false;

    const jungle_document_t *d = TerrainJungle_GetLoaded();
    if (!d)
        return false;

    const int cs = d->terrain_chunk_size;
    if (cs <= 0) {
        Com_EPrintf("[TERRAIN] TerrainChunks_Build: invalid terrain.chunk_size %d (must be > 0)\n", cs);
        return false;
    }

    const int w = hf.width;
    const int h = hf.height;
    /*
     * chunk_size = number of terrain cells (quads) along X/Y per chunk.
     * Vertices must overlap one row/column between neighbors: chunk covering cs cells needs cs+1 sample
     * indices on each axis → exclusive end is min(start + cs + 1, dim).
     */
    s_grid_w = 0;
    for (int vx0 = 0; vx0 < w; vx0 += cs)
        s_grid_w++;
    s_grid_h = 0;
    for (int vy0 = 0; vy0 < h; vy0 += cs)
        s_grid_h++;
    s_chunk_count = s_grid_w * s_grid_h;

    if (s_chunk_count <= 0)
        return false;

    s_chunks = (terrain_chunk_t *)Z_TagMallocz((size_t)s_chunk_count * sizeof(terrain_chunk_t), TAG_RENDERER);
    if (!s_chunks) {
        Com_EPrintf("[TERRAIN] TerrainChunks_Build: allocation failed\n");
        s_chunk_count = 0;
        s_grid_w = s_grid_h = 0;
        return false;
    }

    const float sxy = hf.scale_xy;
    const float ox = hf.origin[0];
    const float oy = hf.origin[1];

    for (int cy = 0; cy < s_grid_h; cy++) {
        for (int cx = 0; cx < s_grid_w; cx++) {
            const int idx = cy * s_grid_w + cx;
            terrain_chunk_t *c = &s_chunks[idx];

            const int sx0 = cx * cs;
            const int sy0 = cy * cs;
            const int sx1_ex = sx0 + cs + 1 < w ? sx0 + cs + 1 : w;
            const int sy1_ex = sy0 + cs + 1 < h ? sy0 + cs + 1 : h;

            c->grid_x = cx;
            c->grid_y = cy;
            c->flat_index = idx;
            c->sample_x0 = sx0;
            c->sample_y0 = sy0;
            c->sample_x1_ex = sx1_ex;
            c->sample_y1_ex = sy1_ex;
            c->dbg_seam_hint = false;

            const float min_x = ox + (float)sx0 * sxy;
            const float max_x = ox + (float)(sx1_ex - 1) * sxy;
            const float min_y = oy + (float)sy0 * sxy;
            const float max_y = oy + (float)(sy1_ex - 1) * sxy;

            float zmin = 0.f, zmax = 0.f;
            if (!TerrainHeightmap_ComputeChunkZBounds(&hf, sx0, sy0, sx1_ex, sy1_ex, &zmin, &zmax)) {
                Com_EPrintf("[TERRAIN] TerrainChunks_Build: height bounds failed at chunk (%d,%d)\n", cx, cy);
                TerrainChunks_Free();
                return false;
            }

            c->z_min = zmin;
            c->z_max = zmax;
            c->bounds_mins[0] = min_x < max_x ? min_x : max_x;
            c->bounds_mins[1] = min_y < max_y ? min_y : max_y;
            c->bounds_mins[2] = zmin < zmax ? zmin : zmax;
            c->bounds_maxs[0] = min_x > max_x ? min_x : max_x;
            c->bounds_maxs[1] = min_y > max_y ? min_y : max_y;
            c->bounds_maxs[2] = zmin > zmax ? zmin : zmax;

            c->visible = true;
            c->lod_current = 0;
            c->lod_target = 0;
            c->dirty = true;
        }
    }

    return true;
}

void TerrainChunks_UpdateVisibility(void)
{
    if (!s_chunks || s_chunk_count <= 0)
        return;

    /*
     * Phase 4: no camera/frustum hook without renderer/view changes — all chunks visible.
     * TODO_PHASE5: frustum cull using engine camera when a stable accessor exists.
     */
    for (int i = 0; i < s_chunk_count; i++)
        s_chunks[i].visible = true;
}

#endif /* QUASIMODO_TERRAIN */
