/*
 * Quasimodo RTX — CPU terrain LOD selection (Phase 4).
 */

#include "terrain.h"
#include "terrain_internal.h"

#include <math.h>

#if QUASIMODO_TERRAIN

extern "C" {
#include "common/common.h"
#include "common/cvar.h"
}

extern const jungle_document_t *TerrainJungle_GetLoaded(void);
extern cvar_t *terrain_enable;
extern cvar_t *terrain_lod_bias;

static vec3_t s_lod_ref_org;

void TerrainLOD_SetReferenceWorld(const vec3_t ref_org)
{
    VectorCopy(ref_org, s_lod_ref_org);
}

static int terrain_effective_lod_count(const jungle_document_t *d)
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

static void terrain_lod_neighbor_clamp(terrain_chunk_t *chunks, int gw, int gh)
{
    /*
     * Enforce |ΔLOD| <= 1 between cardinal neighbors (relax toward finer LOD index).
     */
    const int max_pass = (gw + gh) * 2 + 4;
    static const int k_dirs[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};

    for (int pass = 0; pass < max_pass; pass++) {
        bool changed = false;
        for (int cy = 0; cy < gh; cy++) {
            for (int cx = 0; cx < gw; cx++) {
                const int i = cy * gw + cx;
                int L = chunks[i].lod_target;

                for (int k = 0; k < 4; k++) {
                    const int nx = cx + k_dirs[k][0];
                    const int ny = cy + k_dirs[k][1];
                    if (nx < 0 || ny < 0 || nx >= gw || ny >= gh)
                        continue;
                    const int j = ny * gw + nx;
                    const int Lj = chunks[j].lod_target;
                    if (L > Lj + 1) {
                        L = Lj + 1;
                        changed = true;
                    }
                }

                if (L != chunks[i].lod_target) {
                    chunks[i].lod_target = L;
                    changed = true;
                }
            }
        }
        if (!changed)
            break;
    }
}

void TerrainLOD_Update(void)
{
    if (!terrain_enable || !terrain_enable->integer)
        return;

    terrain_chunk_t *chunks = TerrainChunks_GetArrayMutable();
    const int nc = TerrainChunks_GetNumChunks();
    if (!chunks || nc <= 0)
        return;

    const jungle_document_t *d = TerrainJungle_GetLoaded();
    if (!d || d->terrain_scale_xy <= 0.f)
        return;

    const int N = terrain_effective_lod_count(d);
    const float sxy = d->terrain_scale_xy;
    const int cs = d->terrain_chunk_size;
    float unit = (float)cs * sxy;
    if (!(unit > 0.f))
        unit = 1.f;

    const int bias = terrain_lod_bias ? terrain_lod_bias->integer : 0;

    for (int i = 0; i < nc; i++) {
        terrain_chunk_t *c = &chunks[i];

        const float cx = (c->bounds_mins[0] + c->bounds_maxs[0]) * 0.5f;
        const float cy = (c->bounds_mins[1] + c->bounds_maxs[1]) * 0.5f;
        const float dx = cx - s_lod_ref_org[0];
        const float dy = cy - s_lod_ref_org[1];
        const float dist = sqrtf(dx * dx + dy * dy);

        float ring_w = unit * 2.f;
        if (!(ring_w > 0.f))
            ring_w = 1.f;

        int raw = (int)floorf(dist / ring_w);
        if (raw < 0)
            raw = 0;

        int tgt = raw + bias;
        if (tgt < 0)
            tgt = 0;
        if (tgt >= N)
            tgt = N - 1;

        if (!c->visible)
            tgt = N - 1;

        c->lod_target = tgt;
    }

    int gw = 0, gh = 0;
    TerrainChunks_GetGridDims(&gw, &gh);
    if (gw > 0 && gh > 0)
        terrain_lod_neighbor_clamp(chunks, gw, gh);

    for (int i = 0; i < nc; i++) {
        if (chunks[i].lod_current != chunks[i].lod_target) {
            chunks[i].lod_current = chunks[i].lod_target;
            chunks[i].dirty = true;
        }
    }
}

#endif /* QUASIMODO_TERRAIN */
