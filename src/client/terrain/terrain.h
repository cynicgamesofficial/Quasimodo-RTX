/*
 * Quasimodo RTX — terrain subsystem public API (Phase 1 skeleton).
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "shared/shared.h"

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

void Terrain_Init(void);
void Terrain_Shutdown(void);

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

#ifdef __cplusplus
}
#endif
