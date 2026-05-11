/*
 * Quasimodo RTX — Jolt stub when QUASIMODO_JOLT_PHYSICS is OFF at configure time.
 */

#include <stdio.h>
#include <string.h>

#include "physics_jolt.h"

int PhysicsJolt_Init(void)
{
	return 0;
}

void PhysicsJolt_Shutdown(void)
{
}

int PhysicsJolt_IsAvailable(void)
{
	return 0;
}

void PhysicsJolt_PrintInfo(void)
{
}

int PhysicsJolt_IsTerrainHeightfieldReady(void)
{
	return 0;
}

int PhysicsJolt_BuildTerrainHeightfieldSquare(int dim, const float *heights_world_z_row_major,
                                              const float origin3[3], float scale_xy)
{
	(void)dim;
	(void)heights_world_z_row_major;
	(void)origin3;
	(void)scale_xy;
	return 0;
}

void PhysicsJolt_DestroyTerrainHeightfield(void)
{
}

static physics_jolt_compare_stats_t g_stub_stats;

void PhysicsJolt_GetTerrainCompareStats(physics_jolt_compare_stats_t *out)
{
	if (!out)
		return;
	memset(out, 0, sizeof(*out));
	*out = g_stub_stats;
}

void PhysicsJolt_ResetTerrainCompareStats(void)
{
	memset(&g_stub_stats, 0, sizeof(g_stub_stats));
}

void PhysicsJolt_CompareTerrainHeightfieldHull(const float start[3], const float end[3],
                                               const float mins[3], const float maxs[3],
                                               int collision_backend_mode,
                                               int legacy_had_hit, int legacy_synthetic,
                                               float legacy_frac, const float legacy_normal[3],
                                               int legacy_startsolid, int legacy_allsolid)
{
	(void)start;
	(void)end;
	(void)mins;
	(void)maxs;
	(void)legacy_had_hit;
	(void)legacy_synthetic;
	(void)legacy_frac;
	(void)legacy_normal;
	(void)legacy_startsolid;
	(void)legacy_allsolid;

	if (collision_backend_mode != 1)
		return;

	static int s_warned = 0;
	if (!s_warned) {
		fprintf(stderr, "[JoltPhysics] terrain_collision_backend 1 (jolt_compare) but QUASIMODO_JOLT_PHYSICS is OFF at "
		                "build — compare disabled\n");
		s_warned = 1;
	}
	g_stub_stats.compare_skipped_unavailable++;
}

void PhysicsJolt_PrintTerrainDiagnostics(void)
{
	fprintf(stderr, "[JoltPhysics] stub: no Jolt integration at build time\n");
}
