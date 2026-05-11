/*
 * Quasimodo RTX — internal terrain heightfield trace helper (Phase 3 / Phase 7 stable floor).
 *
 * PMove sweeps an OBB; heightfield is triangles. We combine:
 *   (1) ray/triangle crossing along hull-bottom segments with correct *origin* endpos (lerp),
 *   (2) footprint ground-support when the sweep is nearly parallel to the surface so rays miss
 *       but the hull bottom at end_xy still sits within epsilon of the surface under the feet
 *       (fixes horizontal steps losing ground + CL_Trace needing fraction<1 for world ent).
 *
 * Not a full swept OBB vs heightfield. No vertical wall hull clipping.
 */

#include "terrain.h"

#include <float.h>
#include <math.h>
#include <string.h>

#include "physics_jolt.h"

#if QUASIMODO_TERRAIN

extern "C" {
#include "common/common.h"
#include "common/cvar.h"
#include "system/system.h"
#if USE_CLIENT
#include "common/bsp.h"
#endif
}

extern cvar_t *terrain_enable;
extern cvar_t *terrain_collision;
extern cvar_t *terrain_collision_backend;
extern cvar_t *terrain_debug;

#if USE_CLIENT
/*
 * Footstep code (tent.c) casts trace.surface (csurface_t *) back to mtexinfo_t * and reads step_id.
 * Terrain hits must point at the leading csurface_t inside a static mtexinfo_t — same layout as BSP traces.
 */
static mtexinfo_t terrain_collision_footstep_texinfo;
static bool terrain_collision_footstep_texinfo_ready;

static void Terrain_Internal_InitFootstepSurface(void)
{
    if (terrain_collision_footstep_texinfo_ready)
        return;
    memset(&terrain_collision_footstep_texinfo, 0, sizeof(terrain_collision_footstep_texinfo));
    Q_strlcpy(terrain_collision_footstep_texinfo.c.name, "terrain", sizeof(terrain_collision_footstep_texinfo.c.name));
    terrain_collision_footstep_texinfo.step_id = FOOTSTEP_ID_DEFAULT;
    terrain_collision_footstep_texinfo_ready = true;
}
#endif /* USE_CLIENT */

/* Feet may float this far above the highest terrain sample under the hull (slopes / numeric drift). */
static const float TERRAIN_SUPPORT_EPS_Z = 2.0f;
/* Feet may interpenetrate this far below the lowest terrain sample before we refuse support. */
static const float TERRAIN_PENETRATE_EPS_Z = 4.0f;
/* Synthetic hits must use fraction strictly < 1 so CL_Trace assigns world ent for PM_CategorizePosition. */
static const float TERRAIN_SUPPORT_SYNTH_FRAC = 1.0f - 1.0f / 8192.0f;
/*
 * Do not emit synthetic ground when the hull origin sweep is moving upward (jump / slide ascent).
 * PM_StepSlideMove uses the same trace path as walking; BSP allows leaving ground on upward moves.
 */
static const float TERRAIN_SUPPORT_MAX_UP_DZ = 0.125f;

static void terrain_jolt_compare_after_hull(const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs,
                                            int legacy_had_hit, int legacy_synthetic, const trace_t *legacy_tr)
{
	if (!terrain_collision_backend || terrain_collision_backend->integer != 1)
		return;
	if (!legacy_tr)
		return;

	const int mode = terrain_collision_backend->integer;

	physics_jolt_compare_stats_t before;
	PhysicsJolt_GetTerrainCompareStats(&before);

	PhysicsJolt_CompareTerrainHeightfieldHull(
		start, end, mins, maxs, mode, legacy_had_hit, legacy_synthetic, legacy_tr->fraction, legacy_tr->plane.normal,
		legacy_tr->plane.dist, legacy_tr->startsolid ? 1 : 0, legacy_tr->allsolid ? 1 : 0);

	if (!terrain_debug || !terrain_debug->integer)
		return;

	physics_jolt_compare_stats_t after;
	PhysicsJolt_GetTerrainCompareStats(&after);

	const uint64_t d_ljm = after.legacy_hit_jolt_miss - before.legacy_hit_jolt_miss;
	const uint64_t d_jlm = after.jolt_hit_legacy_miss - before.jolt_hit_legacy_miss;
	const uint64_t d_df = after.frac_mismatch - before.frac_mismatch;
	const uint64_t d_dn = after.normal_mismatch - before.normal_mismatch;
	const uint64_t d_syn = after.legacy_synth_jolt_ray_miss - before.legacy_synth_jolt_ray_miss;
	const uint64_t d_ray = after.ray_legacy_hit_jolt_ray_miss - before.ray_legacy_hit_jolt_ray_miss;
	const uint64_t d_sh = after.legacy_synth_jolt_support_hit - before.legacy_synth_jolt_support_hit;
	const uint64_t d_sm = after.legacy_synth_jolt_support_miss - before.legacy_synth_jolt_support_miss;
	const uint64_t d_dhz = after.support_height_mismatch - before.support_height_mismatch;
	const uint64_t d_dnz = after.support_normal_mismatch - before.support_normal_mismatch;
	if (d_ljm + d_jlm + d_df + d_dn + d_syn + d_ray + d_sh + d_sm + d_dhz + d_dnz == 0)
		return;

	static unsigned s_dbg_ms = 0;
	static int s_dbg_budget = 0;
	const unsigned t = Sys_Milliseconds();
	if (t - s_dbg_ms > 500) {
		s_dbg_ms = t;
		s_dbg_budget = 10;
	}
	if (s_dbg_budget <= 0)
		return;
	s_dbg_budget--;

	Com_Printf("[TERRAIN] J2A/J2B compare mismatch +%llu legacy>jolt +%llu jolt>legacy +%llu dfrac +%llu dnorm +%llu "
	           "synth_ray_miss +%llu ray_miss +%llu sup_hit +%llu sup_miss +%llu sup_dh +%llu sup_dn\n",
	           (unsigned long long)d_ljm, (unsigned long long)d_jlm, (unsigned long long)d_df,
	           (unsigned long long)d_dn, (unsigned long long)d_syn, (unsigned long long)d_ray,
	           (unsigned long long)d_sh, (unsigned long long)d_sm, (unsigned long long)d_dhz,
	           (unsigned long long)d_dnz);
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

/*
 * Ray vs triangles along (ray_start -> ray_end); result trace uses player *origin* sweep (origin_start -> origin_end).
 */
bool Terrain_Internal_TraceHeightfieldSegment(const vec3_t ray_start, const vec3_t ray_end,
                                              const vec3_t origin_start, const vec3_t origin_end, trace_t *out_tr)
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
    VectorSubtract(ray_end, ray_start, dir);
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

            if (ray_triangle_mt(ray_start, dir_u, seg_len, p00, p10, p11, &t, n)) {
                if (t < best_t) {
                    best_t = t;
                    VectorCopy(n, best_n);
                    hit = true;
                }
            }
            if (ray_triangle_mt(ray_start, dir_u, seg_len, p00, p11, p01, &t, n)) {
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

    vec3_t o_delta;
    VectorSubtract(origin_end, origin_start, o_delta);
    out_tr->fraction = frac;
    /* trace.endpos = swept player origin at impact (matches CM_BoxTrace), not triangle hit point */
    VectorMA(origin_start, frac, o_delta, out_tr->endpos);

    vec3_t hitpos;
    VectorMA(ray_start, best_t, dir_u, hitpos);
    VectorCopy(best_n, out_tr->plane.normal);
    out_tr->plane.dist = DotProduct(hitpos, best_n);
    out_tr->plane.type = PLANE_NON_AXIAL;
    out_tr->plane.signbits = 0;
#if USE_CLIENT
    Terrain_Internal_InitFootstepSurface();
    out_tr->surface = &terrain_collision_footstep_texinfo.c;
#else
    out_tr->surface = nullptr;
#endif
    out_tr->contents = CONTENTS_SOLID;
    out_tr->ent = nullptr;

    return true;
}

static void terrain_hull_footprint_xy_offs(const vec3_t mins, const vec3_t maxs, vec2_t out5[5])
{
    out5[0][0] = 0.5f * (mins[0] + maxs[0]);
    out5[0][1] = 0.5f * (mins[1] + maxs[1]);
    out5[1][0] = mins[0];
    out5[1][1] = mins[1];
    out5[2][0] = maxs[0];
    out5[2][1] = mins[1];
    out5[3][0] = mins[0];
    out5[3][1] = maxs[1];
    out5[4][0] = maxs[0];
    out5[4][1] = maxs[1];
}

/* True if hull bottom Z vs min/max terrain Z under the footprint says we're standing/supported at origin_end. */
static bool terrain_footprint_supported_at_end(const terrain_heightfield_cpu_t *hf, const vec3_t origin_end,
                                               const vec3_t mins, const vec3_t maxs)
{
    vec2_t offs[5];
    terrain_hull_footprint_xy_offs(mins, maxs, offs);

    float tmin = FLT_MAX;
    float tmax = -FLT_MAX;

    for (int i = 0; i < 5; i++) {
        const float wx = origin_end[0] + offs[i][0];
        const float wy = origin_end[1] + offs[i][1];
        float tz;
        if (!TerrainHeightmap_SampleHeight(hf, wx, wy, &tz))
            return false;
        if (tz < tmin)
            tmin = tz;
        if (tz > tmax)
            tmax = tz;
    }

    const float bz = origin_end[2] + mins[2];
    /* Floating above all samples under the hull */
    if (bz > tmax + TERRAIN_SUPPORT_EPS_Z)
        return false;
    /* Deep under the lowest sample (void / wrong volume) */
    if (bz < tmin - TERRAIN_PENETRATE_EPS_Z)
        return false;
    return true;
}

/*
 * Stricter than terrain_footprint_supported_at_end: hull bottom clearly above the highest terrain sample
 * under the footprint. Ground glue uses TERRAIN_SUPPORT_EPS_Z (looser); merge uses this so running jumps
 * (large horizontal step, modest dz) still skip hull snags while uphill slides remain on-surface (bz ~ tmax).
 */
static const float TERRAIN_MERGE_AIRBORNE_EPS_Z = 1.0f;

static bool terrain_merge_endpos_airborne_above_mesh(const terrain_heightfield_cpu_t *hf,
                                                     const vec3_t origin_end,
                                                     const vec3_t mins, const vec3_t maxs)
{
    if (!hf)
        return false;

    vec2_t offs[5];
    terrain_hull_footprint_xy_offs(mins, maxs, offs);

    float tmax = -FLT_MAX;
    int samples = 0;
    for (int i = 0; i < 5; i++) {
        const float wx = origin_end[0] + offs[i][0];
        const float wy = origin_end[1] + offs[i][1];
        float tz;
        if (!TerrainHeightmap_SampleHeight(hf, wx, wy, &tz))
            continue;
        samples++;
        if (tz > tmax)
            tmax = tz;
    }
    if (!samples)
        return false;

    const float bz = origin_end[2] + mins[2];
    return bz > tmax + TERRAIN_MERGE_AIRBORNE_EPS_Z;
}

static bool terrain_try_synthetic_floor_trace(const terrain_heightfield_cpu_t *hf,
                                              const vec3_t origin_start, const vec3_t origin_end,
                                              const vec3_t mins, const vec3_t maxs, trace_t *out_tr)
{
    if (!hf || !out_tr)
        return false;

    const float dz = origin_end[2] - origin_start[2];
    if (dz > TERRAIN_SUPPORT_MAX_UP_DZ)
        return false;

    if (!terrain_footprint_supported_at_end(hf, origin_end, mins, maxs))
        return false;

    vec2_t offs[5];
    terrain_hull_footprint_xy_offs(mins, maxs, offs);
    const float wx = origin_end[0] + offs[0][0];
    const float wy = origin_end[1] + offs[0][1];

    vec3_t n;
    if (!TerrainHeightmap_SampleNormal(hf, wx, wy, n))
        return false;

    float tz;
    if (!TerrainHeightmap_SampleHeight(hf, wx, wy, &tz))
        return false;

    trace_clear_miss(out_tr);
    out_tr->fraction = TERRAIN_SUPPORT_SYNTH_FRAC;

    vec3_t o_delta;
    VectorSubtract(origin_end, origin_start, o_delta);
    VectorMA(origin_start, out_tr->fraction, o_delta, out_tr->endpos);

    VectorCopy(n, out_tr->plane.normal);
    vec3_t plane_pt = { wx, wy, tz };
    out_tr->plane.dist = DotProduct(plane_pt, n);
    out_tr->plane.type = PLANE_NON_AXIAL;
    out_tr->plane.signbits = 0;
#if USE_CLIENT
    Terrain_Internal_InitFootstepSurface();
    out_tr->surface = &terrain_collision_footstep_texinfo.c;
#else
    out_tr->surface = nullptr;
#endif
    out_tr->contents = CONTENTS_SOLID;
    out_tr->ent = nullptr;
    out_tr->startsolid = qfalse;
    out_tr->allsolid = qfalse;

    return true;
}

/*
 * Bottom footprint: parallel segments along hull bottom + synthetic floor when rays miss (horizontal/near-tangent).
 */
bool Terrain_Internal_TraceHeightfieldHull(const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs,
                                           trace_t *out_tr)
{
    if (!out_tr)
        return false;
    trace_clear_miss(out_tr);

    if (!terrain_enable || !terrain_enable->integer)
        return false;
    if (!terrain_collision || !terrain_collision->integer)
        return false;

    terrain_heightfield_cpu_t hf;
    if (!Terrain_Internal_GetActiveHeightfield(&hf))
        return false;

    const float dx = maxs[0] - mins[0];
    const float dy = maxs[1] - mins[1];

    /* Crossing passes */
    trace_t best;
    trace_clear_miss(&best);
    best.fraction = 2.f;

    if (fabsf(dx) < 1e-4f && fabsf(dy) < 1e-4f) {
        if (!Terrain_Internal_TraceHeightfieldSegment(start, end, start, end, &best)) {
            if (terrain_try_synthetic_floor_trace(&hf, start, end, mins, maxs, out_tr)) {
                terrain_jolt_compare_after_hull(start, end, mins, maxs, 1, 1, out_tr);
                return true;
            }
            terrain_jolt_compare_after_hull(start, end, mins, maxs, 0, 0, out_tr);
            return false;
        }
        *out_tr = best;
        terrain_jolt_compare_after_hull(start, end, mins, maxs, 1, 0, out_tr);
        return true;
    }

    vec2_t offs[5];
    terrain_hull_footprint_xy_offs(mins, maxs, offs);

    for (int i = 0; i < 5; i++) {
        vec3_t rs, re;

        rs[0] = start[0] + offs[i][0];
        rs[1] = start[1] + offs[i][1];
        rs[2] = start[2] + mins[2];
        re[0] = end[0] + offs[i][0];
        re[1] = end[1] + offs[i][1];
        re[2] = end[2] + mins[2];

        trace_t tr;
        if (!Terrain_Internal_TraceHeightfieldSegment(rs, re, start, end, &tr))
            continue;
        if (tr.fraction < best.fraction)
            best = tr;
    }

    if (best.fraction <= 1.f) {
        *out_tr = best;
        terrain_jolt_compare_after_hull(start, end, mins, maxs, 1, 0, out_tr);
        return true;
    }

    if (terrain_try_synthetic_floor_trace(&hf, start, end, mins, maxs, out_tr)) {
        terrain_jolt_compare_after_hull(start, end, mins, maxs, 1, 1, out_tr);
        return true;
    }

    terrain_jolt_compare_after_hull(start, end, mins, maxs, 0, 0, out_tr);
    return false;
}

static void terrain_merge_debug_log(const char *tag, const trace_t *dst_before, float ter_frac, bool terrain_applied)
{
    if (!terrain_debug || !terrain_debug->integer)
        return;

    static unsigned s_reset_ms = 0;
    static int s_budget = 0;

    const unsigned t = Sys_Milliseconds();
    if (t - s_reset_ms > 500) {
        s_reset_ms = t;
        s_budget = 10;
    }
    if (s_budget <= 0)
        return;
    s_budget--;

    const bool probably_synth = ter_frac >= (1.0f - 0.002f);
    Com_Printf("[TERRAIN] merge %s: bsp_frac=%.5f ter_frac=%.5f mode=%s applied=%d\n", tag,
               dst_before ? dst_before->fraction : -1.f, ter_frac, probably_synth ? "support" : "cross",
               terrain_applied ? 1 : 0);
}

extern "C" void Terrain_MergeWorldTrace(trace_t *dst, const vec3_t start, const vec3_t end, const vec3_t mins,
                                          const vec3_t maxs, int contentmask, struct edict_s *world_ent)
{
    if (!dst)
        return;

    /* Terrain ground is solid; do not apply to masks that never test world solid. */
    if (!(contentmask & CONTENTS_SOLID))
        return;

    /* Keep BSP stuck-in-solid behavior authoritative; do not replace with terrain hits. */
    if (dst->allsolid || dst->startsolid)
        return;

    /*
     * Jump / vertical lift: hull triangle hits still merge here and clip velocity against terrain under the feet.
     * BSP sees clear air (fraction 1) but terrain rays can still register immediate hits — sticky floor.
     * (1) Standing hop: dz >> dh — ratio gate.
     * (2) Run + jump: dz may be smaller than dh per trace; ratio fails — use endpos clearly above local mesh
     *     (feet not hugging tmax) so uphill slides still merge while airborne arcs skip.
     */
    {
        const float dz = end[2] - start[2];
        if (dz > TERRAIN_SUPPORT_MAX_UP_DZ) {
            const float dx = end[0] - start[0];
            const float dy = end[1] - start[1];
            const float dh_sq = dx * dx + dy * dy;
            const float dh = dh_sq > 0.f ? sqrtf(dh_sq) : 0.f;
            static const float k_vert_dominates_horiz = 1.25f;
            if (dz > dh * k_vert_dominates_horiz) {
                terrain_merge_debug_log("skip_jump_ascent", dst, 1.f, false);
                return;
            }
            terrain_heightfield_cpu_t hf;
            if (Terrain_Internal_GetActiveHeightfield(&hf) &&
                terrain_merge_endpos_airborne_above_mesh(&hf, end, mins, maxs)) {
                terrain_merge_debug_log("skip_jump_airborne_end", dst, 1.f, false);
                return;
            }
        }
    }

    trace_t ter;

    memset(&ter, 0, sizeof(ter));
    ter.fraction = 1.f;

    if (!Terrain_Internal_TraceHeightfieldHull(start, end, mins, maxs, &ter)) {
        terrain_merge_debug_log("miss", dst, 1.f, false);
        return;
    }

    if (!(ter.fraction < dst->fraction))
    {
        terrain_merge_debug_log("no_replace", dst, ter.fraction, false);
        return;
    }

    terrain_merge_debug_log("win", dst, ter.fraction, true);

    *dst = ter;
    dst->ent = world_ent;
}

#endif /* QUASIMODO_TERRAIN */
