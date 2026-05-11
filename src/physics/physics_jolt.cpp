/*
 * Quasimodo RTX — Jolt minimal library init when QUASIMODO_JOLT_PHYSICS is ON.
 * J1: RegisterTypes / Factory.
 * J2A: optional square HeightFieldShape + hull-bottom ray compare counters (diagnostics only).
 */

#include <cstdarg>
#include <cstdio>
#include <cmath>
#include <cstring>

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/SubShapeID.h>

#include "physics_jolt.h"

using namespace JPH;

static bool g_jolt_core_ready = false;
static RefConst<Shape> g_terrain_hf_shape;
static uint32_t g_terrain_hf_dim = 0;
static physics_jolt_compare_stats_t g_cmp_stats;

static constexpr float kFracEps = 0.01f;
static constexpr float kNormalDotMin = 0.95f;

static void PhysicsJolt_Trace(const char *inFMT, ...)
{
	va_list list;
	va_start(list, inFMT);
	char buffer[1024];
	vsnprintf(buffer, sizeof(buffer), inFMT, list);
	va_end(list);
	std::fputs(buffer, stderr);
}

#ifdef JPH_ENABLE_ASSERTS
static bool PhysicsJolt_AssertFailed(const char *inExpression, const char *inMessage, const char *inFile, JPH::uint inLine)
{
	std::fprintf(stderr, "%s:%u: (%s) %s\n", inFile, (unsigned)inLine, inExpression, inMessage ? inMessage : "");
	return true;
}
#endif

static Vec3 QuakePosToJolt(float qx, float qy, float qz)
{
	/* Quake Z-up -> Jolt Y-up (horizontal XZ preserved). */
	return Vec3(qx, qz, qy);
}

int PhysicsJolt_Init(void)
{
	if (g_jolt_core_ready)
		return 1;

	JPH::RegisterDefaultAllocator();
	JPH::Trace = PhysicsJolt_Trace;
#ifdef JPH_ENABLE_ASSERTS
	JPH::AssertFailed = PhysicsJolt_AssertFailed;
#endif

	JPH::Factory::sInstance = new JPH::Factory();
	JPH::RegisterTypes();

	g_jolt_core_ready = true;
	return 1;
}

void PhysicsJolt_Shutdown(void)
{
	if (!g_jolt_core_ready)
		return;

	PhysicsJolt_DestroyTerrainHeightfield();

	JPH::UnregisterTypes();
	delete JPH::Factory::sInstance;
	JPH::Factory::sInstance = nullptr;

	g_jolt_core_ready = false;
}

int PhysicsJolt_IsAvailable(void)
{
	return g_jolt_core_ready ? 1 : 0;
}

void PhysicsJolt_PrintInfo(void)
{
	if (g_jolt_core_ready)
		std::fprintf(stderr, "[JoltPhysics] core registered (J1 + J2A terrain compare hooks).\n");
	else
		std::fprintf(stderr, "[JoltPhysics] not initialized.\n");
}

int PhysicsJolt_IsTerrainHeightfieldReady(void)
{
	return g_terrain_hf_shape != nullptr ? 1 : 0;
}

void PhysicsJolt_DestroyTerrainHeightfield(void)
{
	g_terrain_hf_shape = nullptr;
	g_terrain_hf_dim = 0;
}

int PhysicsJolt_BuildTerrainHeightfieldSquare(int dim, const float *heights_world_z_row_major,
                                              const float origin3[3], float scale_xy)
{
	PhysicsJolt_DestroyTerrainHeightfield();

	if (!g_jolt_core_ready || !heights_world_z_row_major || dim < 2)
		return 0;
	if (!(scale_xy > 0.f))
		return 0;

	const Vec3 offset(origin3[0], 0.0f, origin3[1]);
	const Vec3 scale(scale_xy, 1.0f, scale_xy);

	HeightFieldShapeSettings settings(heights_world_z_row_major, offset, scale, (uint32)dim);
	settings.mBitsPerSample = 16;

	ShapeSettings::ShapeResult created = settings.Create();
	if (!created.IsValid()) {
		std::fprintf(stderr, "[JoltPhysics] HeightFieldShape build failed\n");
		return 0;
	}

	g_terrain_hf_shape = created.Get();
	g_terrain_hf_dim = (uint32)dim;
	return 1;
}

static bool CastOneRayJolt(const Shape *shape, const Vec3 &qStart, const Vec3 &qEnd, float &outFrac, Vec3 &outN)
{
	const Vec3 j0 = QuakePosToJolt(qStart.GetX(), qStart.GetY(), qStart.GetZ());
	const Vec3 j1 = QuakePosToJolt(qEnd.GetX(), qEnd.GetY(), qEnd.GetZ());
	const Vec3 dir = j1 - j0;
	const float len = dir.Length();
	if (len < 1.0e-8f)
		return false;

	RayCast ray(j0, dir);
	RayCastResult hit;
	hit.Reset();
	SubShapeIDCreator id_creator;
	if (!shape->CastRay(ray, id_creator, hit))
		return false;
	if (hit.mFraction < 0.f || hit.mFraction > 1.f)
		return false;

	outFrac = hit.mFraction;
	const Vec3 jHit = ray.GetPointOnRay(hit.mFraction);
	outN = shape->GetSurfaceNormal(hit.mSubShapeID2, jHit);
	if (outN.LengthSq() > 1.0e-12f)
		outN = outN.Normalized();
	/* Map Jolt normal back to Quake Z-up for compare vs legacy. */
	const Vec3 nq(outN.GetX(), outN.GetZ(), outN.GetY());
	outN = nq;
	if (outN.LengthSq() > 1.0e-12f)
		outN = outN.Normalized();
	return true;
}

static bool JoltHullTraceAggregate(const Shape *shape, const float *start, const float *end, const float *mins, const float *maxs,
                                   float &outFrac, Vec3 &outN)
{
	const float dx = maxs[0] - mins[0];
	const float dy = maxs[1] - mins[1];

	const Vec3 qstart(start[0], start[1], start[2]);
	const Vec3 qend(end[0], end[1], end[2]);

	bool any = false;
	float bestF = 2.f;
	Vec3 bestN(0, 0, 1);

	if (fabsf(dx) < 1.0e-4f && fabsf(dy) < 1.0e-4f) {
		float f;
		Vec3 n;
		if (CastOneRayJolt(shape, qstart, qend, f, n)) {
			any = true;
			bestF = f;
			bestN = n;
		}
	} else {
		const float ox[5] = {
			0.5f * (mins[0] + maxs[0]),
			mins[0],
			maxs[0],
			mins[0],
			maxs[0],
		};
		const float oy[5] = {
			0.5f * (mins[1] + maxs[1]),
			mins[1],
			mins[1],
			maxs[1],
			maxs[1],
		};

		for (int i = 0; i < 5; i++) {
			const Vec3 rs(start[0] + ox[i], start[1] + oy[i], start[2] + mins[2]);
			const Vec3 re(end[0] + ox[i], end[1] + oy[i], end[2] + mins[2]);
			float f;
			Vec3 n;
			if (!CastOneRayJolt(shape, rs, re, f, n))
				continue;
			if (f < bestF) {
				bestF = f;
				bestN = n;
				any = true;
			}
		}
	}

	if (!any)
		return false;
	outFrac = bestF;
	outN = bestN;
	return true;
}

void PhysicsJolt_GetTerrainCompareStats(physics_jolt_compare_stats_t *out)
{
	if (!out)
		return;
	*out = g_cmp_stats;
}

void PhysicsJolt_ResetTerrainCompareStats(void)
{
	memset(&g_cmp_stats, 0, sizeof(g_cmp_stats));
}

void PhysicsJolt_CompareTerrainHeightfieldHull(const float start[3], const float end[3],
                                               const float mins[3], const float maxs[3],
                                               int collision_backend_mode,
                                               int legacy_had_hit, int legacy_synthetic,
                                               float legacy_frac, const float legacy_normal[3],
                                               int legacy_startsolid, int legacy_allsolid)
{
	(void)legacy_allsolid;
	if (collision_backend_mode != 1)
		return;

	if (!g_jolt_core_ready) {
		static bool s_warned = false;
		if (!s_warned) {
			std::fprintf(stderr, "[JoltPhysics] terrain_collision_backend 1 but Jolt core not initialized (compare skipped)\n");
			s_warned = true;
		}
		g_cmp_stats.compare_skipped_unavailable++;
		return;
	}

	if (!g_terrain_hf_shape) {
		g_cmp_stats.compare_skipped_unavailable++;
		return;
	}

	g_cmp_stats.compare_total++;

	float jfrac = 1.f;
	Vec3 jn(0, 0, 1);
	const bool jolt_hit = JoltHullTraceAggregate(g_terrain_hf_shape, start, end, mins, maxs, jfrac, jn);

	const int lh = legacy_had_hit ? 1 : 0;
	const int jh = jolt_hit ? 1 : 0;

	if (lh && !jh) {
		g_cmp_stats.legacy_hit_jolt_miss++;
		if (legacy_synthetic)
			g_cmp_stats.legacy_synth_jolt_ray_miss++;
	} else if (!lh && jh) {
		g_cmp_stats.jolt_hit_legacy_miss++;
	}

	if (lh && jh) {
		if (fabsf(legacy_frac - jfrac) > kFracEps)
			g_cmp_stats.frac_mismatch++;

		Vec3 ln(legacy_normal[0], legacy_normal[1], legacy_normal[2]);
		if (ln.LengthSq() > 1.0e-12f)
			ln = ln.Normalized();
		const float nd = fabsf(jn.Dot(ln));
		if (nd < kNormalDotMin)
			g_cmp_stats.normal_mismatch++;
	}

	if (legacy_startsolid != 0)
		g_cmp_stats.startsolid_mismatch++;
}

void PhysicsJolt_PrintTerrainDiagnostics(void)
{
	std::fprintf(stderr, "[JoltPhysics] terrain HF ready: %d  dim: %u\n",
	             PhysicsJolt_IsTerrainHeightfieldReady(), (unsigned)g_terrain_hf_dim);
	physics_jolt_compare_stats_t s;
	PhysicsJolt_GetTerrainCompareStats(&s);
	std::fprintf(stderr, "[JoltPhysics] compare total=%llu skipped_unavail=%llu legacy>jolt_miss=%llu jolt>legacy_miss=%llu "
	                     "dfrac=%llu dnormal=%llu startsolid=%llu legacy_synth_jolt_miss=%llu\n",
	             (unsigned long long)s.compare_total, (unsigned long long)s.compare_skipped_unavailable,
	             (unsigned long long)s.legacy_hit_jolt_miss, (unsigned long long)s.jolt_hit_legacy_miss,
	             (unsigned long long)s.frac_mismatch, (unsigned long long)s.normal_mismatch,
	             (unsigned long long)s.startsolid_mismatch, (unsigned long long)s.legacy_synth_jolt_ray_miss);
}
