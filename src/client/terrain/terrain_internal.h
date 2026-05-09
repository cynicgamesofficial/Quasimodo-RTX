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
bool TerrainHeightmap_ValidateCpuBuffer(int width, int height, const char *height_format, size_t pixel_bytes);
#endif

#ifdef __cplusplus
}
#endif
