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

void Terrain_TraceLine(trace_t *trace,
                       const vec3_t start, const vec3_t end,
                       const vec3_t mins, const vec3_t maxs,
                       int brushmask);

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
 * Swept AABB vs terrain is not implemented; trace acts as a zero-thickness segment.
 * On miss or when terrain is disabled / unavailable, returns false after initializing *out_tr to a clean miss
 * (fraction 1, cleared flags). On hit, fills trace_t with the nearest intersection along the segment.
 */
bool Terrain_Internal_TraceHeightfieldSegment(const vec3_t start, const vec3_t end, trace_t *out_tr);

void TerrainSeam_FreeAll(void);
bool TerrainSeam_LoadFromPatchPaths(const char *const *paths, int count);
#endif

#ifdef __cplusplus
}
#endif
