/*
 * Quasimodo RTX — terrain subsystem public API (Phase 1 skeleton).
 */

#pragma once

#if !defined(QUASIMODO_TERRAIN)
#define QUASIMODO_TERRAIN 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include "shared/shared.h"
#include <vulkan/vulkan.h>

/* Fallback CVars (registered when Terrain_Init runs). */
extern cvar_t *terrain_enable;
extern cvar_t *terrain_collision;
/* 0 = legacy only, 1 = jolt_compare (diagnostics; legacy remains authoritative). */
extern cvar_t *terrain_collision_backend;
extern cvar_t *terrain_water;
extern cvar_t *terrain_debug;
extern cvar_t *terrain_wireframe;
extern cvar_t *terrain_show_chunks;
extern cvar_t *terrain_show_lod;
extern cvar_t *terrain_show_seams;
extern cvar_t *terrain_water_debug;
extern cvar_t *terrain_water_level;
extern cvar_t *terrain_lod_bias;
extern cvar_t *terrain_rtx_instance;
extern cvar_t *terrain_uv_scale;
extern cvar_t *terrain_build_blas_on_load;

VkResult Terrain_InitVk(void);
VkResult Terrain_DestroyVk(void);
void Terrain_OnMapLoaded_Vk(const char *mapname);
void Terrain_OnMapUnload_Vk(void);
void Terrain_BuildBLAS(void);
void Terrain_BuildSeamBLAS(void);

void Terrain_Init(void);
void Terrain_Shutdown(void);

/* Loads maps/<map>.jungle via FS when terrain_enable is non-zero; optional file. */
bool Terrain_LoadJungle(const char *mapname);
void Terrain_Unload(void);
bool Terrain_IsLoaded(void);

void Terrain_PerFrameBegin(void);
void Terrain_PerFrameEnd(void);
void Terrain_InstanceBLAS(void);

/* Deferred terrain GPU upload runs before first SCR_UpdateScreen after map load when terrain_build_blas_on_load is 0. */
void Terrain_PreFrameRenderHook(void);

void Terrain_TraceLine(trace_t *trace,
                       const vec3_t start, const vec3_t end,
                       const vec3_t mins, const vec3_t maxs,
                       int brushmask);

/*
 * Phase 7B — after CM_BoxTrace against world BSP, optionally merge a terrain heightfield hit.
 * Uses hull/bottom-footprint trace when mins/maxs imply a box (see terrain_collision.cpp).
 * Requires contentmask & CONTENTS_SOLID for terrain solid hits.
 */
void Terrain_MergeWorldTrace(trace_t *dst,
                             const vec3_t start, const vec3_t end,
                             const vec3_t mins, const vec3_t maxs,
                             int contentmask,
                             struct edict_s *world_ent);

int Terrain_PointContents(const vec3_t p);

void Terrain_OnSwapchainRecreate(void);
void Terrain_OnPipelineReload(void);

#if QUASIMODO_TERRAIN
/*
 * Phase 3 — CPU heightfield sampling (no GPU upload).
 * When QUASIMODO_TERRAIN is 0 at compile time, these declarations are omitted.
 */
typedef struct terrain_heightfield_cpu_s {
    int width;
    int height;
    char height_format[32];
    float scale_xy;
    float scale_z;
    vec3_t origin;
    const uint8_t *pixels;
    size_t pixel_bytes;
} terrain_heightfield_cpu_t;

bool Terrain_Internal_GetActiveHeightfield(terrain_heightfield_cpu_t *out);

bool TerrainHeightmap_SampleHeight(const terrain_heightfield_cpu_t *hf, float world_x, float world_y, float *out_z);
bool TerrainHeightmap_SampleNormal(const terrain_heightfield_cpu_t *hf, float world_x, float world_y, vec3_t out_normal);
bool TerrainHeightmap_SampleTexel(const terrain_heightfield_cpu_t *hf, int ix, int iy, float *out_z);

bool Terrain_SampleHeight(float world_x, float world_y, float *out_z);
bool Terrain_SampleNormal(float world_x, float world_y, vec3_t out_normal);

/*
 * Internal-only heightfield ray vs bilinear surface (Phase 3).
 * Ray cast uses ray_start->ray_end; trace endpos uses origin_start->origin_end (player origin sweep).
 * On miss or when terrain is disabled / unavailable, returns false after initializing *out_tr to a clean miss
 * (fraction 1, cleared flags).
 */
bool Terrain_Internal_TraceHeightfieldSegment(const vec3_t ray_start, const vec3_t ray_end,
                                              const vec3_t origin_start, const vec3_t origin_end, trace_t *out_tr);

/*
 * Hull-bottom crossing traces plus footprint ground-support when rays miss (near-horizontal moves).
 */
bool Terrain_Internal_TraceHeightfieldHull(const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs,
                                           trace_t *out_tr);

void TerrainSeam_FreeAll(void);
bool TerrainSeam_LoadFromPatchPaths(const char *const *paths, int count);

/* Phase 6A terrain water (optional jungle-driven plane). */
void TerrainWater_Init(void);
void TerrainWater_Shutdown(void);
void TerrainWater_OnMapLoadedVk(const char *mapname);
void TerrainWater_OnMapUnloadVk(void);
void TerrainWater_BuildVk(void);
bool TerrainWater_IsUnderwater(const vec3_t pos);
#endif

#ifdef __cplusplus
}
#endif
