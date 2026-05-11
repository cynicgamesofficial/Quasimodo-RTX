/*
 * Quasimodo RTX — optional JoltPhysics wrapper (J1 skeleton + J2A terrain ray compare + J2B support probe diagnostics).
 * No Jolt types in this header. Implementations live in physics_jolt.cpp (ON) or physics_jolt_stub.cpp (OFF).
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int  PhysicsJolt_Init(void);
void PhysicsJolt_Shutdown(void);
int  PhysicsJolt_IsAvailable(void);
void PhysicsJolt_PrintInfo(void);

/*
 * J2A — optional square heightfield for terrain compare diagnostics only.
 * Heights are world Z in Quake space (units), row-major: index = iy * dim + ix.
 * Jolt uses Y-up internally; mapping is handled inside the ON implementation.
 */
int  PhysicsJolt_IsTerrainHeightfieldReady(void);
int  PhysicsJolt_BuildTerrainHeightfieldSquare(int dim, const float *heights_world_z_row_major,
                                               const float origin3[3], float scale_xy);
void PhysicsJolt_DestroyTerrainHeightfield(void);

typedef struct physics_jolt_compare_stats_s {
	uint64_t compare_total;
	uint64_t compare_skipped_unavailable;
	uint64_t legacy_hit_jolt_miss;
	uint64_t jolt_hit_legacy_miss;
	uint64_t frac_mismatch;
	uint64_t normal_mismatch;
	uint64_t startsolid_mismatch;
	uint64_t legacy_synth_jolt_ray_miss;
	/* J2B — HeightFieldShape::ProjectOntoSurface footprint probe (diagnostics only; synthetic + Jolt ray miss). */
	uint64_t ray_legacy_hit_jolt_ray_miss;
	uint64_t legacy_synth_jolt_support_hit;
	uint64_t legacy_synth_jolt_support_miss;
	uint64_t support_height_mismatch;
	uint64_t support_normal_mismatch;
} physics_jolt_compare_stats_t;

void PhysicsJolt_GetTerrainCompareStats(physics_jolt_compare_stats_t *out);
void PhysicsJolt_ResetTerrainCompareStats(void);

/*
 * collision_backend_mode: only 1 (jolt_compare) does work; 0 = immediate return.
 * Legacy trace is authoritative; this only updates counters / optional logs.
 * legacy_plane_dist: synthetic diagnostic height compare (J2B); pass legacy_tr->plane.dist from Quake trace.
 */
void PhysicsJolt_CompareTerrainHeightfieldHull(const float start[3], const float end[3],
                                               const float mins[3], const float maxs[3],
                                               int collision_backend_mode,
                                               int legacy_had_hit, int legacy_synthetic,
                                               float legacy_frac, const float legacy_normal[3],
                                               float legacy_plane_dist,
                                               int legacy_startsolid, int legacy_allsolid);

void PhysicsJolt_PrintTerrainDiagnostics(void);

#ifdef __cplusplus
}
#endif
