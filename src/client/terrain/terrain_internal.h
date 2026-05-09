/*
 * Quasimodo RTX — private terrain subsystem definitions (not part of public terrain.h API).
 * Shared by terrain_jungle.cpp, terrain.cpp, and related terrain/*.cpp units.
 */

#pragma once

#include "shared/shared.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JUNGLE_MODE_TERRAIN_ONLY,
    JUNGLE_MODE_BSP_TERRAIN,
    JUNGLE_MODE_BSP_ONLY
} jungle_mode_enum_t;

typedef struct jungle_document {
    int version;
    jungle_mode_enum_t mode;
    char format_str[16];

    char world_bsp[MAX_QPATH];
    bool world_bsp_interior_only;

    char terrain_heightmap[MAX_QPATH];
    int terrain_width;
    int terrain_height;
    char terrain_height_format[32];
    float terrain_scale_xy;
    float terrain_scale_z;
    vec3_t terrain_origin;
    int terrain_chunk_size;
    int terrain_lod_count;
    bool terrain_lod_morph;
    char terrain_splatmap[MAX_QPATH];

    char **terrain_materials;
    int terrain_materials_count;

    char **seam_patch_paths;
    int seam_patch_paths_count;

    bool water_enabled;
    float water_level;
    char water_mask[MAX_QPATH];
    char water_normal_map[MAX_QPATH];
    float water_normal_scroll[2];
    float water_color[4];
    float water_roughness;
    float water_refraction_strength;
    char water_reflection_mode[32];
    bool water_rtx_reflection_hook;

    bool sky_override;
    char sky_shader[MAX_QPATH];

    bool collision_terrain;
    bool collision_water;
    bool collision_seam;

    bool debug_draw_chunks;
    bool debug_draw_lod;
    bool debug_draw_seams;
    bool debug_draw_water_mask;

    uint8_t *cpu_heightmap;
    size_t cpu_heightmap_bytes;

    uint8_t *cpu_splat;
    size_t cpu_splat_bytes;
    int cpu_splat_w;
    int cpu_splat_h;
    int cpu_splat_comp;

    uint8_t *cpu_water_mask;
    size_t cpu_water_mask_bytes;
    int cpu_water_mask_w;
    int cpu_water_mask_h;
    int cpu_water_mask_comp;
} jungle_document_t;

#if !defined(QUASIMODO_TERRAIN)
#define QUASIMODO_TERRAIN 0
#endif

#if QUASIMODO_TERRAIN

#include "terrain.h"

bool TerrainHeightmap_ValidateCpuBuffer(int width, int height, const char *height_format, size_t pixel_bytes);

/*
 * Min/max world Z over heightfield texels [sx0,sx1_ex) x [sy0,sy1_ex) in sample space.
 */
bool TerrainHeightmap_ComputeChunkZBounds(const terrain_heightfield_cpu_t *hf, int sx0, int sy0, int sx1_ex,
                                          int sy1_ex, float *out_zmin, float *out_zmax);

#define TERRAIN_LOD_LEVEL_CAP 16

typedef struct terrain_chunk_s {
    int grid_x;
    int grid_y;
    int flat_index;
    /* Heightfield sample indices: x in [sample_x0, sample_x1_ex), y in [sample_y0, sample_y1_ex) — includes
     * shared boundary row/col with neighbors so quads are not missing between chunks. */
    int sample_x0;
    int sample_y0;
    int sample_x1_ex;
    int sample_y1_ex;
    vec3_t bounds_mins;
    vec3_t bounds_maxs;
    float z_min;
    float z_max;
    bool visible;
    int lod_current;
    int lod_target;
    bool dirty;
    bool dbg_seam_hint;
} terrain_chunk_t;

bool TerrainChunks_Build(void);
void TerrainChunks_Free(void);
void TerrainChunks_UpdateVisibility(void);

int TerrainChunks_GetNumChunks(void);
void TerrainChunks_GetGridDims(int *out_gw, int *out_gh);
const terrain_chunk_t *TerrainChunks_GetArray(void);
terrain_chunk_t *TerrainChunks_GetArrayMutable(void);
bool TerrainChunks_FindAtWorldXY(float world_x, float world_y, int *out_chunk_index);

void TerrainLOD_Update(void);
void TerrainLOD_SetReferenceWorld(const vec3_t ref_org);

void TerrainDebug_OnFrameBegin(void);
void TerrainDebug_PrintInfo(void);
void TerrainDebug_DumpChunks(void);
void TerrainDebug_ProbeExtra(float world_x, float world_y);

void TerrainRender_DebugAppendInfo(void);
/* Phase 8B: optional splat probe line when CPU splat exists */
void TerrainRender_DebugProbeSplatWorld(float world_x, float world_y);
void Terrain_InstanceBLAS_Vk(void);

void Terrain_RunDeferredGpuUploadIfAny(void);

int TerrainSeam_GetCpuMeshCount(void);

#endif /* QUASIMODO_TERRAIN */

#ifdef __cplusplus
}
#endif
