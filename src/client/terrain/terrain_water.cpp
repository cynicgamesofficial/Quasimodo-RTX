/*
 * Quasimodo RTX — terrain water plane (Phase 6A), CPU mirror + GPU BLAS via terrain primitive buffer.
 */

#include "terrain.h"
#include "terrain_internal.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if QUASIMODO_TERRAIN

extern "C" {
#include "../../refresh/vkpt/vkpt.h"
#include "../../refresh/vkpt/material.h"
#include "../../refresh/vkpt/shader/constants.h"
#include "../../refresh/vkpt/streamline_reflex.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/zone.h"

void TerrainRender_SuballocateBlasMemory(model_geometry_t *info, size_t *vbo_size, const char *model_name);
void TerrainRender_CreateModelBlas(model_geometry_t *info, VkBuffer buffer, const char *name);
void TerrainRender_BuildModelBlas(VkCommandBuffer cmd_buf, model_geometry_t *info, size_t first_vertex_offset,
                                  const BufferResource_t *buffer);
}

extern cvar_t *terrain_enable;
extern cvar_t *terrain_water;
extern cvar_t *terrain_water_debug;
extern cvar_t *terrain_uv_scale;
extern cvar_t *terrain_rtx_instance;

extern const jungle_document_t *TerrainJungle_GetLoaded(void);

typedef struct {
    bool mirror_valid;
    bool jungle_water_enabled;
    float water_level;
    float water_color[4];
    float water_roughness;
    float water_refraction_strength;
    char water_reflection_mode[32];
    bool water_rtx_reflection_hook;
    char water_mask[MAX_QPATH];
    char water_normal_map[MAX_QPATH];
    float water_normal_scroll[2];
} terrain_water_mirror_t;

static terrain_water_mirror_t s_wm;
static model_geometry_t s_water_geom;
static uint32_t s_water_material_id = 0;
static bool s_water_geom_valid = false;
static int s_last_water_tlas_instances = 0;

static uint32_t terrain_water_encode_normal_vec(const vec3_t normal)
{
    float invL1Norm = 1.0f / (fabsf(normal[0]) + fabsf(normal[1]) + fabsf(normal[2]));

    vec2_t p = {normal[0] * invL1Norm, normal[1] * invL1Norm};
    vec2_t pp = {p[0], p[1]};

    if (normal[2] < 0.f) {
        pp[0] = (1.f - fabsf(p[1])) * ((p[0] >= 0.f) ? 1.f : -1.f);
        pp[1] = (1.f - fabsf(p[0])) * ((p[1] >= 0.f) ? 1.f : -1.f);
    }

    pp[0] = pp[0] * 0.5f + 0.5f;
    pp[1] = pp[1] * 0.5f + 0.5f;

    pp[0] = Q_clipf(pp[0], 0.f, 1.f);
    pp[1] = Q_clipf(pp[1], 0.f, 1.f);

    uint32_t ux = (uint32_t)(pp[0] * 0xffffu);
    uint32_t uy = (uint32_t)(pp[1] * 0xffffu);

    return ux | (uy << 16);
}

static void terrain_water_resolve_material_id(void)
{
    /*
     * Same extension rule as terrain_flat_material_flags: IMG_Find requires a path with extension.
     * BSP uses IF_TURBULENT for SURF_WARP surfaces; stock water materials are in baseq2/materials/baseq2.mat.
     */
    static const char *const candidates[] = {
        "textures/e1u1/water4.wal",
        "textures/e1u1/water8.wal",
        "textures/e1u1/water1_8.wal",
        "textures/e1u1/brwater.wal",
    };

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        pbr_material_t *m = MAT_Find(candidates[i], IT_WALL, IF_TURBULENT);
        if (m) {
            uint32_t id = MAT_SetKind(m->flags, MATERIAL_KIND_WATER);
            id |= MATERIAL_FLAG_WARP;
            s_water_material_id = id;
            return;
        }
    }

    /* Retry without turbulent flag — still avoids invalid-path MAT_Find spam. */
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        pbr_material_t *m = MAT_Find(candidates[i], IT_WALL, IF_NONE);
        if (m) {
            uint32_t id = MAT_SetKind(m->flags, MATERIAL_KIND_WATER);
            id |= MATERIAL_FLAG_WARP;
            s_water_material_id = id;
            return;
        }
    }

    static bool s_water_material_fallback_warned;
    if (terrain_water_debug && terrain_water_debug->integer && !s_water_material_fallback_warned) {
        Com_DPrintf("[TERRAIN] water: no stock water *.wal resolved; using synthesized water material id.\n");
        s_water_material_fallback_warned = true;
    }

    s_water_material_id = MATERIAL_KIND_WATER | MATERIAL_FLAG_WARP | (1u & MATERIAL_INDEX_MASK);
}

extern "C" {

void TerrainWater_Init(void)
{
    memset(&s_wm, 0, sizeof(s_wm));
    memset(&s_water_geom, 0, sizeof(s_water_geom));
    s_water_material_id = MATERIAL_KIND_WATER | MATERIAL_FLAG_WARP;
    s_water_geom_valid = false;
    s_last_water_tlas_instances = 0;
}

void TerrainWater_Shutdown(void)
{
    TerrainWater_OnMapUnloadVk();
}

void TerrainWater_OnMapLoadedVk(const char *mapname)
{
    (void)mapname;

    memset(&s_wm, 0, sizeof(s_wm));
    s_water_geom_valid = false;
    s_last_water_tlas_instances = 0;

    const jungle_document_t *d = TerrainJungle_GetLoaded();
    if (!d) {
        return;
    }

    s_wm.mirror_valid = true;
    s_wm.jungle_water_enabled = d->water_enabled;
    s_wm.water_level = d->water_level;
    memcpy(s_wm.water_color, d->water_color, sizeof(s_wm.water_color));
    s_wm.water_roughness = d->water_roughness;
    s_wm.water_refraction_strength = d->water_refraction_strength;
    Q_strlcpy(s_wm.water_reflection_mode, d->water_reflection_mode, sizeof(s_wm.water_reflection_mode));
    s_wm.water_rtx_reflection_hook = d->water_rtx_reflection_hook;
    Q_strlcpy(s_wm.water_mask, d->water_mask, sizeof(s_wm.water_mask));
    Q_strlcpy(s_wm.water_normal_map, d->water_normal_map, sizeof(s_wm.water_normal_map));
    s_wm.water_normal_scroll[0] = d->water_normal_scroll[0];
    s_wm.water_normal_scroll[1] = d->water_normal_scroll[1];

    terrain_water_resolve_material_id();
}

void TerrainWater_OnMapUnloadVk(void)
{
    memset(&s_wm, 0, sizeof(s_wm));
    s_water_geom_valid = false;
    s_last_water_tlas_instances = 0;
}

void TerrainWater_DestroyVk(void)
{
    if (s_water_geom.accel || s_water_geom.geometries) {
        vkpt_destroy_model_geometry(&s_water_geom);
    }
    memset(&s_water_geom, 0, sizeof(s_water_geom));
    s_water_geom_valid = false;
}

bool TerrainWater_ShouldBuildPlaneGpu(void)
{
    if (!terrain_enable || !terrain_enable->integer)
        return false;
    if (!terrain_water || !terrain_water->integer)
        return false;
    if (!s_wm.mirror_valid || !s_wm.jungle_water_enabled)
        return false;
    if (!isfinite(s_wm.water_level))
        return false;
    return true;
}

bool TerrainWater_SetupWaterBlasGeometry(int chunk_prim_end, size_t *vbo_size)
{
    vkpt_init_model_geometry(&s_water_geom, 1u);
    vkpt_append_model_geometry(&s_water_geom, 2u, (uint32_t)chunk_prim_end, "terrain_water");
    TerrainRender_SuballocateBlasMemory(&s_water_geom, vbo_size, "terrain:water");
    return s_water_geom.num_geometries > 0;
}

void TerrainWater_SetWaterBlasInstanceFlags(void)
{
    if (s_water_geom.num_geometries == 0)
        return;
    s_water_geom.instance_mask = AS_FLAG_TRANSPARENT;
    s_water_geom.instance_flags =
        VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR | VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    s_water_geom.sbt_offset = SBTO_OPAQUE;
}

void TerrainWater_FillPlaneStaging(prim_positions_t *dst_pos, VboPrimitive *dst_prim, int prim_base,
                                   const terrain_heightfield_cpu_t *hf, float uv_scale)
{
    if (!dst_pos || !dst_prim || !hf || hf->width < 2 || hf->height < 2 || hf->scale_xy <= 0.f)
        return;

    const float min_x = hf->origin[0];
    const float min_y = hf->origin[1];
    const float max_x = hf->origin[0] + (float)(hf->width - 1) * hf->scale_xy;
    const float max_y = hf->origin[1] + (float)(hf->height - 1) * hf->scale_xy;
    const float z = s_wm.water_level;

    vec3_t v00, v10, v11, v01;
    VectorSet(v00, min_x, min_y, z);
    VectorSet(v10, max_x, min_y, z);
    VectorSet(v11, max_x, max_y, z);
    VectorSet(v01, min_x, max_y, z);

    vec3_t up = {0.f, 0.f, 1.f};
    vec3_t tan_x = {1.f, 0.f, 0.f};
    const uint32_t n_enc = terrain_water_encode_normal_vec(up);
    const uint32_t t_enc = terrain_water_encode_normal_vec(tan_x);

    const uint32_t emissive_alpha_pack = 0x3c003c00;

    /* Triangle v00-v10-v11 */
    {
        VectorCopy(v00, dst_pos[prim_base + 0][0]);
        VectorCopy(v10, dst_pos[prim_base + 0][1]);
        VectorCopy(v11, dst_pos[prim_base + 0][2]);

        VboPrimitive *dst = dst_prim + prim_base + 0;
        memset(dst, 0, sizeof(VboPrimitive));
        VectorCopy(v00, dst->pos0);
        VectorCopy(v10, dst->pos1);
        VectorCopy(v11, dst->pos2);
        dst->material_id = s_water_material_id;
        dst->cluster = -1;
        dst->shell = 0;
        dst->instance = 0;
        dst->emissive_and_alpha = emissive_alpha_pack;
        dst->normals[0] = n_enc;
        dst->normals[1] = n_enc;
        dst->normals[2] = n_enc;
        dst->tangents[0] = t_enc;
        dst->tangents[1] = t_enc;
        dst->tangents[2] = t_enc;
        dst->uv0[0] = min_x * uv_scale;
        dst->uv0[1] = min_y * uv_scale;
        dst->uv1[0] = max_x * uv_scale;
        dst->uv1[1] = min_y * uv_scale;
        dst->uv2[0] = max_x * uv_scale;
        dst->uv2[1] = max_y * uv_scale;
    }

    /* Triangle v00-v11-v01 */
    {
        VectorCopy(v00, dst_pos[prim_base + 1][0]);
        VectorCopy(v11, dst_pos[prim_base + 1][1]);
        VectorCopy(v01, dst_pos[prim_base + 1][2]);

        VboPrimitive *dst = dst_prim + prim_base + 1;
        memset(dst, 0, sizeof(VboPrimitive));
        VectorCopy(v00, dst->pos0);
        VectorCopy(v11, dst->pos1);
        VectorCopy(v01, dst->pos2);
        dst->material_id = s_water_material_id;
        dst->cluster = -1;
        dst->shell = 0;
        dst->instance = 0;
        dst->emissive_and_alpha = emissive_alpha_pack;
        dst->normals[0] = n_enc;
        dst->normals[1] = n_enc;
        dst->normals[2] = n_enc;
        dst->tangents[0] = t_enc;
        dst->tangents[1] = t_enc;
        dst->tangents[2] = t_enc;
        dst->uv0[0] = min_x * uv_scale;
        dst->uv0[1] = min_y * uv_scale;
        dst->uv1[0] = max_x * uv_scale;
        dst->uv1[1] = max_y * uv_scale;
        dst->uv2[0] = min_x * uv_scale;
        dst->uv2[1] = max_y * uv_scale;
    }
}

void TerrainWater_CreateWaterBlas(VkBuffer buffer)
{
    TerrainWater_SetWaterBlasInstanceFlags();
    TerrainRender_CreateModelBlas(&s_water_geom, buffer, "terrain:water");
    if (s_water_geom.accel)
        s_water_geom_valid = true;
}

void TerrainWater_BuildWaterBlas(VkCommandBuffer cmd_buf, const BufferResource_t *pos_buf)
{
    TerrainRender_BuildModelBlas(cmd_buf, &s_water_geom, 0, pos_buf);
}

void TerrainWater_BuildVk(void)
{
    terrain_heightfield_cpu_t hf;
    if (!Terrain_Internal_GetActiveHeightfield(&hf))
        return;

    float zmin = 0.f, zmax = 0.f;
    if (TerrainHeightmap_ComputeChunkZBounds(&hf, 0, 0, hf.width, hf.height, &zmin, &zmax)) {
        if (s_wm.water_level < zmin || s_wm.water_level > zmax)
            Com_DPrintf("[TERRAIN] water level %.3f outside heightfield z bounds [%.3f .. %.3f]\n", s_wm.water_level,
                        zmin, zmax);
    }
}

void TerrainWater_OnGpuUploadFinished(void)
{
    /* Reserved for future GPU-ready bookkeeping; geometry validity tracked via s_water_geom_valid */
}

void TerrainWater_InstanceBLAS_Vk(void)
{
    s_last_water_tlas_instances = 0;

    if (!terrain_enable || !terrain_enable->integer)
        return;
    if (!Terrain_IsLoaded())
        return;
    if (!terrain_water || !terrain_water->integer)
        return;
    if (!terrain_rtx_instance || !terrain_rtx_instance->integer)
        return;
    if (!TerrainWater_ShouldBuildPlaneGpu())
        return;
    if (!s_water_geom_valid || !s_water_geom.accel || !s_water_geom.blas_device_address)
        return;

    static const mat4 terrain_identity_transform = {
        {1.f, 0.f, 0.f, 0.f},
        {0.f, 1.f, 0.f, 0.f},
        {0.f, 0.f, 1.f, 0.f},
        {0.f, 0.f, 0.f, 1.f},
    };

    vkpt_pt_instance_model_blas(&s_water_geom, terrain_identity_transform, VERTEX_BUFFER_TERRAIN, -1, 0);
    s_last_water_tlas_instances = 1;
}

void TerrainWater_DebugAppendInfo(void)
{
    Com_Printf("[TERRAIN] --- water (Phase 6A) ---\n");
    Com_Printf("[TERRAIN] water mirror valid: %s\n", s_wm.mirror_valid ? "yes" : "no");
    if (s_wm.mirror_valid) {
        Com_Printf("[TERRAIN] jungle water.enabled: %d\n", s_wm.jungle_water_enabled ? 1 : 0);
        Com_Printf("[TERRAIN] jungle water.level: %.3f\n", s_wm.water_level);
        Com_Printf("[TERRAIN] jungle water.color rgba: %.3f %.3f %.3f %.3f\n", s_wm.water_color[0], s_wm.water_color[1],
                   s_wm.water_color[2], s_wm.water_color[3]);
        Com_Printf("[TERRAIN] jungle water roughness: %.3f refraction_strength: %.3f\n", s_wm.water_roughness,
                   s_wm.water_refraction_strength);
        Com_Printf("[TERRAIN] jungle water reflection_mode: \"%s\" (metadata)\n",
                   s_wm.water_reflection_mode[0] ? s_wm.water_reflection_mode : "");
        Com_Printf("[TERRAIN] jungle water rtx_reflection_hook: %d (TODO_RTX metadata)\n",
                   s_wm.water_rtx_reflection_hook ? 1 : 0);
        if (s_wm.water_mask[0])
            Com_Printf("[TERRAIN] jungle water.mask path: %s (parsed, not used)\n", s_wm.water_mask);
        if (s_wm.water_normal_map[0])
            Com_Printf("[TERRAIN] jungle water.normal_map path: %s (parsed, not used)\n", s_wm.water_normal_map);
        Com_Printf("[TERRAIN] jungle water.normal_scroll: %.4f %.4f (parsed, not used)\n", s_wm.water_normal_scroll[0],
                   s_wm.water_normal_scroll[1]);
    }

    Com_Printf("[TERRAIN] terrain_water cvar: %d\n", terrain_water && terrain_water->integer ? 1 : 0);
    Com_Printf("[TERRAIN] water GPU plane BLAS valid: %s\n", s_water_geom_valid ? "yes" : "no");
    Com_Printf("[TERRAIN] water TLAS instances (last frame): %d\n", s_last_water_tlas_instances);
    Com_Printf("[TERRAIN] TerrainWater_IsUnderwater test: %s\n",
               (!terrain_water || !terrain_water->integer || !s_wm.mirror_valid || !s_wm.jungle_water_enabled)
                   ? "inactive (enable terrain_water + jungle water)"
                   : "active (height vs water.level)");
}

bool TerrainWater_IsUnderwater(const vec3_t pos)
{
    if (!terrain_enable || !terrain_enable->integer)
        return false;
    if (!terrain_water || !terrain_water->integer)
        return false;
    if (!s_wm.mirror_valid || !s_wm.jungle_water_enabled)
        return false;
    if (!isfinite(s_wm.water_level))
        return false;
    return pos[2] < s_wm.water_level;
}

} /* extern "C" */

#endif /* QUASIMODO_TERRAIN */
