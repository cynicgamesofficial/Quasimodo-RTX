/*
 * Quasimodo RTX — terrain stubs when QUASIMODO_TERRAIN is OFF at compile time.
 */

#include "terrain.h"

#include <vulkan/vulkan.h>

VkResult Terrain_InitVk(void)
{
    return VK_SUCCESS;
}

VkResult Terrain_DestroyVk(void)
{
    return VK_SUCCESS;
}

void Terrain_OnMapLoaded_Vk(const char *mapname)
{
    (void)mapname;
}

void Terrain_OnMapUnload_Vk(void) {}

void Terrain_BuildBLAS(void) {}

void Terrain_BuildSeamBLAS(void) {}

void Terrain_Init(void) {}
void Terrain_Shutdown(void) {}

bool Terrain_LoadJungle(const char *mapname)
{
    (void)mapname;
    return false;
}

void Terrain_Unload(void) {}
bool Terrain_IsLoaded(void) { return false; }

void Terrain_PerFrameBegin(void) {}
void Terrain_PerFrameEnd(void) {}
void Terrain_InstanceBLAS(void) {}

void Terrain_TraceLine(trace_t *trace,
                       const vec3_t start, const vec3_t end,
                       const vec3_t mins, const vec3_t maxs,
                       int brushmask)
{
    (void)trace;
    (void)start;
    (void)end;
    (void)mins;
    (void)maxs;
    (void)brushmask;
}

int Terrain_PointContents(const vec3_t p)
{
    (void)p;
    return 0;
}

void Terrain_OnSwapchainRecreate(void) {}
void Terrain_OnPipelineReload(void) {}

cvar_t *terrain_enable;
cvar_t *terrain_collision;
cvar_t *terrain_water;
cvar_t *terrain_debug;
cvar_t *terrain_wireframe;
cvar_t *terrain_show_chunks;
cvar_t *terrain_show_lod;
cvar_t *terrain_show_seams;
cvar_t *terrain_water_debug;
cvar_t *terrain_water_level;
cvar_t *terrain_lod_bias;
/* terrain_rtx_instance: not registered when QUASIMODO_TERRAIN is off; Terrain_InstanceBLAS no-ops */
cvar_t *terrain_rtx_instance;
cvar_t *terrain_uv_scale;
