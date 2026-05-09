/*
 * Quasimodo RTX — internal terrain heightfield trace helper (Phase 3).
 *
 * Segment vs bilinear patch mesh (per-cell two triangles). Swept player bounds are not handled here.
 * trace_t is always cleared to a miss first; disabled/unloaded paths return false without a hit.
 */

#include "terrain.h"

#include <float.h>
#include <math.h>
#include <string.h>

#if QUASIMODO_TERRAIN

extern "C" {
#include "common/common.h"
#include "common/cvar.h"
}

extern cvar_t *terrain_enable;
extern cvar_t *terrain_collision;

static bool ray_triangle_mt(const vec3_t orig, const vec3_t dir_u,
                            float seg_len,
                            const vec3_t v0, const vec3_t v1, const vec3_t v2,
                            float *out_t, vec3_t out_n)
{
    if (!out_t || !out_n)
        return false;

    vec3_t edge1, edge2, pvec, tvec, qvec;

    VectorSubtract(v1, v0, edge1);
    VectorSubtract(v2, v0, edge2);
    CrossProduct(dir_u, edge2, pvec);

    const float det = DotProduct(edge1, pvec);
    const float eps = 1e-12f;
    if (fabsf(det) < eps)
        return false;

    const float inv_det = 1.0f / det;

    VectorSubtract(orig, v0, tvec);
    const float u = DotProduct(tvec, pvec) * inv_det;
    if (u < 0.f || u > 1.f)
        return false;

    CrossProduct(tvec, edge1, qvec);
    const float v = DotProduct(dir_u, qvec) * inv_det;
    if (v < 0.f || u + v > 1.f)
        return false;

    const float t = DotProduct(edge2, qvec) * inv_det;
    if (t <= 0.f || t > seg_len)
        return false;

    vec3_t normal;
    CrossProduct(edge1, edge2, normal);
    if (VectorNormalize(normal) < 1e-12f)
        return false;
    /* Prefer normals opposing ray direction (front hit). */
    if (DotProduct(normal, dir_u) > 0.f)
        VectorScale(normal, -1.f, normal);

    *out_t = t;
    VectorCopy(normal, out_n);
    return true;
}

static void trace_clear_miss(trace_t *tr)
{
    if (!tr)
        return;
    memset(tr, 0, sizeof(*tr));
    tr->fraction = 1.f;
    tr->allsolid = qfalse;
    tr->startsolid = qfalse;
}

bool Terrain_Internal_TraceHeightfieldSegment(const vec3_t start, const vec3_t end, trace_t *out_tr)
{
    if (!out_tr) {
        return false;
    }
    trace_clear_miss(out_tr);

    if (!terrain_enable || !terrain_enable->integer)
        return false;
    if (!terrain_collision || !terrain_collision->integer)
        return false;

    terrain_heightfield_cpu_t hf;
    if (!Terrain_Internal_GetActiveHeightfield(&hf))
        return false;

    vec3_t dir;
    VectorSubtract(end, start, dir);
    const float seg_len = VectorLength(dir);
    if (seg_len < 1e-8f)
        return false;

    vec3_t dir_u;
    VectorCopy(dir, dir_u);
    VectorNormalize(dir_u);

    const int w = hf.width;
    const int h = hf.height;
    const float ox = hf.origin[0];
    const float oy = hf.origin[1];
    const float sxy = hf.scale_xy;
    if (!(sxy > 0.f))
        return false;

    float best_t = FLT_MAX;
    vec3_t best_n = { 0.f, 0.f, 1.f };
    bool hit = false;

    for (int iy = 0; iy < h - 1; iy++) {
        for (int ix = 0; ix < w - 1; ix++) {
            vec3_t p00, p10, p01, p11;
            float z00, z10, z01, z11;

            p00[0] = ox + (float)ix * sxy;
            p00[1] = oy + (float)iy * sxy;
            p10[0] = ox + (float)(ix + 1) * sxy;
            p10[1] = oy + (float)iy * sxy;
            p01[0] = ox + (float)ix * sxy;
            p01[1] = oy + (float)(iy + 1) * sxy;
            p11[0] = ox + (float)(ix + 1) * sxy;
            p11[1] = oy + (float)(iy + 1) * sxy;

            if (!TerrainHeightmap_SampleTexel(&hf, ix, iy, &z00))
                return false;
            if (!TerrainHeightmap_SampleTexel(&hf, ix + 1, iy, &z10))
                return false;
            if (!TerrainHeightmap_SampleTexel(&hf, ix, iy + 1, &z01))
                return false;
            if (!TerrainHeightmap_SampleTexel(&hf, ix + 1, iy + 1, &z11))
                return false;

            p00[2] = z00;
            p10[2] = z10;
            p01[2] = z01;
            p11[2] = z11;

            float t = 0.f;
            vec3_t n;

            if (ray_triangle_mt(start, dir_u, seg_len, p00, p10, p11, &t, n)) {
                if (t < best_t) {
                    best_t = t;
                    VectorCopy(n, best_n);
                    hit = true;
                }
            }
            if (ray_triangle_mt(start, dir_u, seg_len, p00, p11, p01, &t, n)) {
                if (t < best_t) {
                    best_t = t;
                    VectorCopy(n, best_n);
                    hit = true;
                }
            }
        }
    }

    if (!hit || !(best_t < FLT_MAX))
        return false;

    const float frac = best_t / seg_len;
    if (frac < 0.f || frac > 1.f)
        return false;

    vec3_t hitpos;
    VectorMA(start, best_t, dir_u, hitpos);

    out_tr->fraction = frac;
    VectorCopy(hitpos, out_tr->endpos);
    VectorCopy(best_n, out_tr->plane.normal);
    out_tr->plane.dist = DotProduct(hitpos, best_n);
    out_tr->plane.type = PLANE_NON_AXIAL;
    out_tr->plane.signbits = 0;
    out_tr->surface = nullptr;
    out_tr->contents = CONTENTS_SOLID;
    out_tr->ent = nullptr;

    return true;
}

#endif /* QUASIMODO_TERRAIN */
