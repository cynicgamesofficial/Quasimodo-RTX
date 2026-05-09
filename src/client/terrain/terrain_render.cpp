/*
 * Quasimodo RTX — terrain Vulkan RTX Phase 5A (GPU positions + static chunk BLAS, no TLAS).
 */

#include "terrain.h"
#include "terrain_internal.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if QUASIMODO_TERRAIN

extern "C" {
#include "../../refresh/vkpt/vkpt.h"
#include "../../refresh/vkpt/material.h"
#include "../../refresh/vkpt/streamline_reflex.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/zone.h"

extern void TerrainWater_DestroyVk(void);
extern bool TerrainWater_ShouldBuildPlaneGpu(void);
extern bool TerrainWater_SetupWaterBlasGeometry(int chunk_prim_end, size_t *vbo_size);
extern void TerrainWater_FillPlaneStaging(prim_positions_t *dst_pos, VboPrimitive *dst_prim, int prim_base,
                                          const terrain_heightfield_cpu_t *hf, float uv_scale);
extern void TerrainWater_CreateWaterBlas(VkBuffer buffer);
extern void TerrainWater_BuildWaterBlas(VkCommandBuffer cmd_buf, const BufferResource_t *pos_buf);
extern void TerrainWater_BuildVk(void);
extern void TerrainWater_OnGpuUploadFinished(void);
extern void TerrainWater_InstanceBLAS_Vk(void);
extern void TerrainWater_DebugAppendInfo(void);
extern void TerrainWater_Init(void);
extern void TerrainWater_Shutdown(void);
extern void TerrainWater_OnMapLoadedVk(const char *mapname);
extern void TerrainWater_OnMapUnloadVk(void);

extern cvar_t *terrain_enable;
extern cvar_t *terrain_rtx_instance; /* defined in terrain.cpp */
extern cvar_t *terrain_uv_scale;

/* Mirrors vertex_buffer.c ACCEL_STRUCT_ALIGNMENT */
#define TERRAIN_ACCEL_STRUCT_ALIGNMENT 256

/* Conservative TLAS instance budget for terrain chunks (path tracer cannot expose g_num_instances). */
#define TERRAIN_MAX_RTX_INSTANCE_CHUNKS 3500

static BufferResource_t s_terrain_pos_buf;
static BufferResource_t s_terrain_prim_buf;
static model_geometry_t *s_chunk_geoms = nullptr;
static int s_chunk_geom_slots = 0;
static int s_chunks_with_blas = 0;
static int s_chunks_with_gpu_positions = 0;
static VkDeviceSize s_terrain_gpu_bytes = 0;
static VkDeviceSize s_terrain_prim_bytes = 0;
static int s_terrain_prim_count = 0;
static int s_last_terrain_tlas_instances = 0;
static int s_terrain_tlas_budget_warn = 0;
static bool s_terrain_vk_ready = false;

static const mat4 terrain_identity_transform = {
    {1.f, 0.f, 0.f, 0.f},
    {0.f, 1.f, 0.f, 0.f},
    {0.f, 0.f, 1.f, 0.f},
    {0.f, 0.f, 0.f, 1.f},
};

static void terrain_gpu_free_all(void)
{
    if (!qvk.device)
        return;
    if (s_chunk_geom_slots == 0 && s_terrain_pos_buf.buffer == VK_NULL_HANDLE && s_terrain_prim_buf.buffer == VK_NULL_HANDLE)
        return;

    if (SL_vkDeviceWaitIdle)
        SL_vkDeviceWaitIdle(qvk.device);
    else
        vkDeviceWaitIdle(qvk.device);

    TerrainWater_DestroyVk();

    if (s_chunk_geoms && s_chunk_geom_slots > 0) {
        for (int i = 0; i < s_chunk_geom_slots; i++)
            vkpt_destroy_model_geometry(&s_chunk_geoms[i]);
        Z_Free(s_chunk_geoms);
        s_chunk_geoms = nullptr;
        s_chunk_geom_slots = 0;
    }
    buffer_destroy(&s_terrain_pos_buf);
    buffer_destroy(&s_terrain_prim_buf);
    s_chunks_with_blas = 0;
    s_chunks_with_gpu_positions = 0;
    s_terrain_gpu_bytes = 0;
    s_terrain_prim_bytes = 0;
    s_terrain_prim_count = 0;
    s_last_terrain_tlas_instances = 0;
}

void TerrainRender_SuballocateBlasMemory(model_geometry_t *info, size_t *vbo_size, const char *model_name)
{
    VkAccelerationStructureBuildSizesInfoKHR build_sizes = {};
    build_sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

    info->build_sizes = build_sizes;

    if (info->num_geometries == 0)
        return;

    VkAccelerationStructureBuildGeometryInfoKHR blasBuildinfo = {};
    blasBuildinfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    blasBuildinfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    blasBuildinfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    blasBuildinfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    blasBuildinfo.geometryCount = info->num_geometries;
    blasBuildinfo.pGeometries = info->geometries;

    qvkGetAccelerationStructureBuildSizesKHR(qvk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                               &blasBuildinfo, info->prim_counts, &info->build_sizes);

    if (info->build_sizes.buildScratchSize > buf_accel_scratch.size) {
        Com_WPrintf("[TERRAIN] '%s' BLAS scratch request %llu exceeds buf_accel_scratch (%zu).\n", model_name,
                    (unsigned long long)info->build_sizes.buildScratchSize, (size_t)buf_accel_scratch.size);
        info->num_geometries = 0;
    } else {
        *vbo_size = ((*vbo_size) + TERRAIN_ACCEL_STRUCT_ALIGNMENT - 1) & ~(size_t)(TERRAIN_ACCEL_STRUCT_ALIGNMENT - 1);
        info->blas_data_offset = *vbo_size;
        *vbo_size += info->build_sizes.accelerationStructureSize;
    }
}

void TerrainRender_CreateModelBlas(model_geometry_t *info, VkBuffer buffer, const char *name)
{
    if (info->num_geometries == 0)
        return;

    VkAccelerationStructureCreateInfoKHR blasCreateInfo = {};
    blasCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    blasCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    blasCreateInfo.buffer = buffer;
    blasCreateInfo.offset = info->blas_data_offset;
    blasCreateInfo.size = info->build_sizes.accelerationStructureSize;

    _VK(qvkCreateAccelerationStructureKHR(qvk.device, &blasCreateInfo, NULL, &info->accel));

    VkAccelerationStructureDeviceAddressInfoKHR as_device_address_info = {};
    as_device_address_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    as_device_address_info.accelerationStructure = info->accel;

    info->blas_device_address = qvkGetAccelerationStructureDeviceAddressKHR(qvk.device, &as_device_address_info);

    if (name && qvkDebugMarkerSetObjectNameEXT) {
        VkDebugMarkerObjectNameInfoEXT name_info = {};
        name_info.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT;
        name_info.object = (uint64_t)info->accel;
        name_info.objectType = VK_DEBUG_REPORT_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR_EXT;
        name_info.pObjectName = name;
        qvkDebugMarkerSetObjectNameEXT(qvk.device, &name_info);
    }
}

void TerrainRender_BuildModelBlas(VkCommandBuffer cmd_buf, model_geometry_t *info, size_t first_vertex_offset,
                                  const BufferResource_t *buffer)
{
    if (!info->accel)
        return;

    assert(buffer->address);

    for (uint32_t index = 0; index < info->num_geometries; index++) {
        VkAccelerationStructureGeometryKHR *geometry = info->geometries + index;
        geometry->geometry.triangles.vertexData.deviceAddress =
            buffer->address + info->prim_offsets[index] * sizeof(prim_positions_t) + first_vertex_offset;
    }

    VkAccelerationStructureBuildGeometryInfoKHR blasBuildinfo = {};
    blasBuildinfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    blasBuildinfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    blasBuildinfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    blasBuildinfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    blasBuildinfo.geometryCount = info->num_geometries;
    blasBuildinfo.pGeometries = info->geometries;
    blasBuildinfo.dstAccelerationStructure = info->accel;
    blasBuildinfo.scratchData.deviceAddress = buf_accel_scratch.address;

    const VkAccelerationStructureBuildRangeInfoKHR *pBlasBuildRange = info->build_ranges;

    qvkCmdBuildAccelerationStructuresKHR(cmd_buf, 1, &blasBuildinfo, &pBlasBuildRange);

    VkMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask =
        VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;

    VkPipelineStageFlags blas_dst_stage =
        qvk.use_ray_query ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
    vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, blas_dst_stage, 0, 1,
                         &barrier, 0, 0, 0, 0);
}

static int terrain_chunk_triangle_count(const terrain_chunk_t *c)
{
    const int nx = c->sample_x1_ex - c->sample_x0;
    const int ny = c->sample_y1_ex - c->sample_y0;
    if (nx < 2 || ny < 2)
        return 0;
    return 2 * (nx - 1) * (ny - 1);
}

static void terrain_fill_chunk_positions(const terrain_heightfield_cpu_t *hf, const terrain_chunk_t *c,
                                         prim_positions_t *dst_base, int prim_base)
{
    const int nx = c->sample_x1_ex - c->sample_x0;
    const int ny = c->sample_y1_ex - c->sample_y0;
    int wptr = prim_base;

    for (int ly = 0; ly < ny - 1; ly++) {
        for (int lx = 0; lx < nx - 1; lx++) {
            const int ix0 = c->sample_x0 + lx;
            const int iy0 = c->sample_y0 + ly;
            const int ix1 = ix0 + 1;
            const int iy1 = iy0 + 1;

            float z00 = 0.f, z10 = 0.f, z01 = 0.f, z11 = 0.f;
            if (!TerrainHeightmap_SampleTexel(hf, ix0, iy0, &z00))
                z00 = hf->origin[2];
            if (!TerrainHeightmap_SampleTexel(hf, ix1, iy0, &z10))
                z10 = hf->origin[2];
            if (!TerrainHeightmap_SampleTexel(hf, ix0, iy1, &z01))
                z01 = hf->origin[2];
            if (!TerrainHeightmap_SampleTexel(hf, ix1, iy1, &z11))
                z11 = hf->origin[2];

            const float x00 = hf->origin[0] + (float)ix0 * hf->scale_xy;
            const float y00 = hf->origin[1] + (float)iy0 * hf->scale_xy;
            const float x10 = hf->origin[0] + (float)ix1 * hf->scale_xy;
            const float y10 = hf->origin[1] + (float)iy0 * hf->scale_xy;
            const float x01 = hf->origin[0] + (float)ix0 * hf->scale_xy;
            const float y01 = hf->origin[1] + (float)iy1 * hf->scale_xy;
            const float x11 = hf->origin[0] + (float)ix1 * hf->scale_xy;
            const float y11 = hf->origin[1] + (float)iy1 * hf->scale_xy;

            vec3_t v00, v10, v01, v11;
            VectorSet(v00, x00, y00, z00);
            VectorSet(v10, x10, y10, z10);
            VectorSet(v01, x01, y01, z01);
            VectorSet(v11, x11, y11, z11);

            /* Two triangles per quad; winding matches BSP-style placement */
            VectorCopy(v00, dst_base[wptr][0]);
            VectorCopy(v10, dst_base[wptr][1]);
            VectorCopy(v11, dst_base[wptr][2]);
            wptr++;

            VectorCopy(v00, dst_base[wptr][0]);
            VectorCopy(v11, dst_base[wptr][1]);
            VectorCopy(v01, dst_base[wptr][2]);
            wptr++;
        }
    }
}

/* Same octahedral packing as encode_normal() in bsp_mesh.c / decode_normal() in shaders — keep in sync. */
static uint32_t terrain_encode_normal_vec(const vec3_t normal)
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

static uint32_t terrain_flat_material_flags(void)
{
    /*
     * MAT_Find passes names to IMG_Find; image paths must include an extension
     * (see images.c find_or_load_image). BSP builds paths like textures/.../*.wal.
     * Names below match entries in baseq2/materials/baseq2.mat (stock maps textures).
     */
    static const char *const k_fallbacks[] = {
        "textures/e1u1/floor3_2.wal",
        "textures/e1u1/floor3_1.wal",
        "textures/e1u1/floor1_3.wal",
    };
    for (size_t i = 0; i < sizeof(k_fallbacks) / sizeof(k_fallbacks[0]); i++) {
        pbr_material_t *mat = MAT_Find(k_fallbacks[i], IT_WALL, IF_NONE);
        if (mat)
            return mat->flags;
    }
    return MATERIAL_KIND_REGULAR | (1u & MATERIAL_INDEX_MASK);
}

static void terrain_vertex_normal(const terrain_heightfield_cpu_t *hf, float wx, float wy, vec3_t out_n)
{
    if (TerrainHeightmap_SampleNormal(hf, wx, wy, out_n))
        return;

    VectorSet(out_n, 0.f, 0.f, 1.f);
}

static void terrain_fill_chunk_primitives(const terrain_heightfield_cpu_t *hf, const terrain_chunk_t *c,
                                          VboPrimitive *dst_base, int prim_base, uint32_t material_id, float uv_scale)
{
    const int nx = c->sample_x1_ex - c->sample_x0;
    const int ny = c->sample_y1_ex - c->sample_y0;
    int wptr = prim_base;

    vec3_t t_x = {1.f, 0.f, 0.f};
    const uint32_t tang_enc = terrain_encode_normal_vec(t_x);

    for (int ly = 0; ly < ny - 1; ly++) {
        for (int lx = 0; lx < nx - 1; lx++) {
            const int ix0 = c->sample_x0 + lx;
            const int iy0 = c->sample_y0 + ly;
            const int ix1 = ix0 + 1;
            const int iy1 = iy0 + 1;

            float z00 = 0.f, z10 = 0.f, z01 = 0.f, z11 = 0.f;
            if (!TerrainHeightmap_SampleTexel(hf, ix0, iy0, &z00))
                z00 = hf->origin[2];
            if (!TerrainHeightmap_SampleTexel(hf, ix1, iy0, &z10))
                z10 = hf->origin[2];
            if (!TerrainHeightmap_SampleTexel(hf, ix0, iy1, &z01))
                z01 = hf->origin[2];
            if (!TerrainHeightmap_SampleTexel(hf, ix1, iy1, &z11))
                z11 = hf->origin[2];

            const float x00 = hf->origin[0] + (float)ix0 * hf->scale_xy;
            const float y00 = hf->origin[1] + (float)iy0 * hf->scale_xy;
            const float x10 = hf->origin[0] + (float)ix1 * hf->scale_xy;
            const float y10 = hf->origin[1] + (float)iy0 * hf->scale_xy;
            const float x01 = hf->origin[0] + (float)ix0 * hf->scale_xy;
            const float y01 = hf->origin[1] + (float)iy1 * hf->scale_xy;
            const float x11 = hf->origin[0] + (float)ix1 * hf->scale_xy;
            const float y11 = hf->origin[1] + (float)iy1 * hf->scale_xy;

            vec3_t v00, v10, v01, v11;
            VectorSet(v00, x00, y00, z00);
            VectorSet(v10, x10, y10, z10);
            VectorSet(v01, x01, y01, z01);
            VectorSet(v11, x11, y11, z11);

            vec3_t n00, n10, n11, n01;
            terrain_vertex_normal(hf, x00, y00, n00);
            terrain_vertex_normal(hf, x10, y10, n10);
            terrain_vertex_normal(hf, x11, y11, n11);
            terrain_vertex_normal(hf, x01, y01, n01);

            /* Triangle v00-v10-v11 */
            {
                VboPrimitive *dst = dst_base + wptr;
                memset(dst, 0, sizeof(VboPrimitive));
                VectorCopy(v00, dst->pos0);
                VectorCopy(v10, dst->pos1);
                VectorCopy(v11, dst->pos2);
                dst->material_id = material_id;
                dst->cluster = -1;
                dst->shell = 0;
                dst->instance = 0;
                dst->emissive_and_alpha = 0x3c003c00;
                dst->normals[0] = terrain_encode_normal_vec(n00);
                dst->normals[1] = terrain_encode_normal_vec(n10);
                dst->normals[2] = terrain_encode_normal_vec(n11);
                dst->tangents[0] = tang_enc;
                dst->tangents[1] = tang_enc;
                dst->tangents[2] = tang_enc;
                dst->uv0[0] = x00 * uv_scale;
                dst->uv0[1] = y00 * uv_scale;
                dst->uv1[0] = x10 * uv_scale;
                dst->uv1[1] = y10 * uv_scale;
                dst->uv2[0] = x11 * uv_scale;
                dst->uv2[1] = y11 * uv_scale;
                wptr++;
            }

            /* Triangle v00-v11-v01 */
            {
                VboPrimitive *dst = dst_base + wptr;
                memset(dst, 0, sizeof(VboPrimitive));
                VectorCopy(v00, dst->pos0);
                VectorCopy(v11, dst->pos1);
                VectorCopy(v01, dst->pos2);
                dst->material_id = material_id;
                dst->cluster = -1;
                dst->shell = 0;
                dst->instance = 0;
                dst->emissive_and_alpha = 0x3c003c00;
                dst->normals[0] = terrain_encode_normal_vec(n00);
                dst->normals[1] = terrain_encode_normal_vec(n11);
                dst->normals[2] = terrain_encode_normal_vec(n01);
                dst->tangents[0] = tang_enc;
                dst->tangents[1] = tang_enc;
                dst->tangents[2] = tang_enc;
                dst->uv0[0] = x00 * uv_scale;
                dst->uv0[1] = y00 * uv_scale;
                dst->uv1[0] = x11 * uv_scale;
                dst->uv1[1] = y11 * uv_scale;
                dst->uv2[0] = x01 * uv_scale;
                dst->uv2[1] = y01 * uv_scale;
                wptr++;
            }
        }
    }
}

static VkResult terrain_upload_positions_and_blas(void)
{
    terrain_gpu_free_all();

    if (!terrain_enable || !terrain_enable->integer)
        return VK_SUCCESS;

    terrain_heightfield_cpu_t hf;
    if (!Terrain_Internal_GetActiveHeightfield(&hf))
        return VK_SUCCESS;

    const int nchunks = TerrainChunks_GetNumChunks();
    const terrain_chunk_t *chunks = TerrainChunks_GetArray();
    if (!chunks || nchunks <= 0)
        return VK_SUCCESS;

    s_chunk_geom_slots = nchunks;
    s_chunk_geoms = (model_geometry_t *)Z_Mallocz((size_t)nchunks * sizeof(model_geometry_t));
    if (!s_chunk_geoms)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    int total_prims = 0;
    int chunks_with_tris = 0;
    for (int i = 0; i < nchunks; i++) {
        const int nt = terrain_chunk_triangle_count(&chunks[i]);
        vkpt_init_model_geometry(&s_chunk_geoms[i], 1u);
        if (nt <= 0)
            continue;

        chunks_with_tris++;
        char label[64];
        Q_snprintf(label, sizeof label, "terrain_chunk[%d]", i);
        vkpt_append_model_geometry(&s_chunk_geoms[i], (uint32_t)nt, (uint32_t)total_prims, label);
        total_prims += nt;
    }

    if (total_prims <= 0) {
        terrain_gpu_free_all();
        return VK_SUCCESS;
    }

    const uint32_t mat_flags = terrain_flat_material_flags();
    const float uv_scale = (terrain_uv_scale && terrain_uv_scale->value > 0.f) ? terrain_uv_scale->value : (1.f / 128.f);

    const int water_prim_count = TerrainWater_ShouldBuildPlaneGpu() ? 2 : 0;
    const int total_all_prims = total_prims + water_prim_count;

    if ((size_t)total_all_prims > ((size_t)1 << 26) / sizeof(VboPrimitive)) {
        Com_WPrintf("[TERRAIN] terrain+water primitive count %d too large for GPU upload.\n", total_all_prims);
        terrain_gpu_free_all();
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }

    size_t vbo_size = (size_t)total_all_prims * sizeof(prim_positions_t);
    for (int i = 0; i < nchunks; i++) {
        char label[64];
        Q_snprintf(label, sizeof label, "terrain:blas[%d]", i);
        TerrainRender_SuballocateBlasMemory(&s_chunk_geoms[i], &vbo_size, label);
    }

    if (water_prim_count > 0) {
        if (!TerrainWater_SetupWaterBlasGeometry(total_prims, &vbo_size)) {
            Com_WPrintf("[TERRAIN] water BLAS sizing failed; aborting terrain GPU upload.\n");
            terrain_gpu_free_all();
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }

    const size_t prim_sz = (size_t)total_all_prims * sizeof(VboPrimitive);

    VkResult res =
        buffer_create(&s_terrain_pos_buf, vbo_size,
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                          | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                          | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (res != VK_SUCCESS) {
        terrain_gpu_free_all();
        return res;
    }

    buffer_attach_name(&s_terrain_pos_buf, "terrain_positions");

    res = buffer_create(&s_terrain_prim_buf, prim_sz,
                        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                            | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (res != VK_SUCCESS) {
        terrain_gpu_free_all();
        return res;
    }

    buffer_attach_name(&s_terrain_prim_buf, "terrain_primitives");

    for (int i = 0; i < nchunks; i++) {
        const int nt = terrain_chunk_triangle_count(&chunks[i]);
        if (nt <= 0)
            continue;
        s_chunk_geoms[i].instance_mask = AS_FLAG_OPAQUE;
        s_chunk_geoms[i].instance_flags = VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
        s_chunk_geoms[i].sbt_offset = SBTO_OPAQUE;
    }

    for (int i = 0; i < nchunks; i++) {
        char label[64];
        Q_snprintf(label, sizeof label, "terrain:blas[%d]", i);
        TerrainRender_CreateModelBlas(&s_chunk_geoms[i], s_terrain_pos_buf.buffer, label);
        if (s_chunk_geoms[i].accel)
            s_chunks_with_blas++;
    }

    if (water_prim_count > 0)
        TerrainWater_CreateWaterBlas(s_terrain_pos_buf.buffer);

    BufferResource_t staging_buffer;
    res = buffer_create(&staging_buffer, (size_t)total_all_prims * sizeof(prim_positions_t), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (res != VK_SUCCESS) {
        terrain_gpu_free_all();
        return res;
    }

    BufferResource_t staging_prim;
    res = buffer_create(&staging_prim, prim_sz, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (res != VK_SUCCESS) {
        buffer_destroy(&staging_buffer);
        terrain_gpu_free_all();
        return res;
    }

    prim_positions_t *staging_positions = (prim_positions_t *)buffer_map(&staging_buffer);
    VboPrimitive *staging_prims = (VboPrimitive *)buffer_map(&staging_prim);

    int prim_write = 0;
    for (int i = 0; i < nchunks; i++) {
        const int nt = terrain_chunk_triangle_count(&chunks[i]);
        if (nt <= 0)
            continue;
        terrain_fill_chunk_positions(&hf, &chunks[i], staging_positions, prim_write);
        terrain_fill_chunk_primitives(&hf, &chunks[i], staging_prims, prim_write, mat_flags, uv_scale);
        prim_write += nt;
    }
    if (water_prim_count > 0)
        TerrainWater_FillPlaneStaging(staging_positions, staging_prims, total_prims, &hf, uv_scale);

    buffer_unmap(&staging_buffer);
    buffer_unmap(&staging_prim);

    VkCommandBuffer cmd_buf = vkpt_begin_command_buffer(&qvk.cmd_buffers_graphics);

    VkBufferCopy copyRegion = {};
    copyRegion.size = staging_buffer.size;
    vkCmdCopyBuffer(cmd_buf, staging_buffer.buffer, s_terrain_pos_buf.buffer, 1, &copyRegion);

    VkBufferCopy copyPrim = {};
    copyPrim.size = staging_prim.size;
    vkCmdCopyBuffer(cmd_buf, staging_prim.buffer, s_terrain_prim_buf.buffer, 1, &copyPrim);

    VkBufferMemoryBarrier buf_barrier = {};
    buf_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    buf_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buf_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buf_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    buf_barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    buf_barrier.buffer = s_terrain_pos_buf.buffer;
    buf_barrier.offset = 0;
    buf_barrier.size = VK_WHOLE_SIZE;

    VkBufferMemoryBarrier prim_barrier = {};
    prim_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    prim_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    prim_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    prim_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    prim_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    prim_barrier.buffer = s_terrain_prim_buf.buffer;
    prim_barrier.offset = 0;
    prim_barrier.size = VK_WHOLE_SIZE;

    VkBufferMemoryBarrier barriers[2] = {buf_barrier, prim_barrier};
    vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR
                             | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, NULL, 2, barriers, 0, NULL);

    for (int i = 0; i < nchunks; i++)
        TerrainRender_BuildModelBlas(cmd_buf, &s_chunk_geoms[i], 0, &s_terrain_pos_buf);

    if (water_prim_count > 0)
        TerrainWater_BuildWaterBlas(cmd_buf, &s_terrain_pos_buf);

    vkpt_submit_command_buffer(cmd_buf, qvk.queue_graphics, (1 << qvk.device_count) - 1, 0, NULL, NULL, NULL, 0, NULL,
                               NULL, NULL);

    if (SL_vkDeviceWaitIdle)
        SL_vkDeviceWaitIdle(qvk.device);
    else
        vkDeviceWaitIdle(qvk.device);

    buffer_destroy(&staging_buffer);
    buffer_destroy(&staging_prim);

    vkpt_write_terrain_primitive_buffer_descriptor(s_terrain_prim_buf.buffer, (VkDeviceSize)prim_sz);

    TerrainWater_BuildVk();
    TerrainWater_OnGpuUploadFinished();

    s_terrain_gpu_bytes = (VkDeviceSize)s_terrain_pos_buf.size;
    s_terrain_prim_bytes = (VkDeviceSize)prim_sz;
    s_terrain_prim_count = total_all_prims;
    s_chunks_with_gpu_positions = chunks_with_tris;
    return VK_SUCCESS;
}

VkResult Terrain_InitVk(void)
{
    TerrainWater_Init();
    s_terrain_vk_ready = true;
    return VK_SUCCESS;
}

VkResult Terrain_DestroyVk(void)
{
    terrain_gpu_free_all();
    TerrainWater_Shutdown();
    s_terrain_vk_ready = false;
    return VK_SUCCESS;
}

void Terrain_OnMapUnload_Vk(void)
{
    if (!s_terrain_vk_ready)
        return;
    TerrainWater_OnMapUnloadVk();
    terrain_gpu_free_all();
}

void Terrain_OnMapLoaded_Vk(const char *mapname)
{
    if (!s_terrain_vk_ready)
        return;
    if (!terrain_enable || !terrain_enable->integer)
        return;
    if (!Terrain_IsLoaded()) {
        terrain_gpu_free_all();
        return;
    }

    TerrainWater_OnMapLoadedVk(mapname);

    VkResult r = terrain_upload_positions_and_blas();
    if (r != VK_SUCCESS)
        Com_EPrintf("[TERRAIN] Terrain_OnMapLoaded_Vk: GPU upload/BLAS failed (%d)\n", (int)r);
}

void Terrain_BuildBLAS(void)
{
    if (!s_terrain_vk_ready || !terrain_enable || !terrain_enable->integer || !Terrain_IsLoaded())
        return;
    VkResult r = terrain_upload_positions_and_blas();
    if (r != VK_SUCCESS)
        Com_EPrintf("[TERRAIN] Terrain_BuildBLAS: rebuild failed (%d)\n", (int)r);
}

void Terrain_BuildSeamBLAS(void)
{
    /* TODO_RTX: expose Phase 3 seam CPU meshes for GPU BLAS without pulling seam internals here. */
}

void Terrain_InstanceBLAS_Vk(void)
{
    s_last_terrain_tlas_instances = 0;

    if (!s_terrain_vk_ready || !terrain_enable || !terrain_enable->integer)
        return;
    if (!Terrain_IsLoaded())
        return;
    if (!terrain_rtx_instance || !terrain_rtx_instance->integer)
        return;
    if (!s_chunk_geoms || s_chunk_geom_slots <= 0)
        return;
    if (!s_terrain_prim_buf.buffer || s_terrain_prim_count <= 0)
        return;

    int eligible = 0;
    for (int i = 0; i < s_chunk_geom_slots; i++) {
        if (s_chunk_geoms[i].accel && s_chunk_geoms[i].blas_device_address)
            eligible++;
    }

    int budget = eligible;
    if (budget > TERRAIN_MAX_RTX_INSTANCE_CHUNKS) {
        budget = TERRAIN_MAX_RTX_INSTANCE_CHUNKS;
        if (!s_terrain_tlas_budget_warn) {
            Com_WPrintf("[TERRAIN] TLAS terrain instancing truncated to %d chunks (eligible %d).\n", budget, eligible);
            s_terrain_tlas_budget_warn = 1;
        }
    }

    int submitted = 0;
    for (int i = 0; i < s_chunk_geom_slots && submitted < budget; i++) {
        if (!s_chunk_geoms[i].accel || !s_chunk_geoms[i].blas_device_address)
            continue;

        vkpt_pt_instance_model_blas(&s_chunk_geoms[i], terrain_identity_transform, VERTEX_BUFFER_TERRAIN, -1, 0);
        submitted++;
    }

    TerrainWater_InstanceBLAS_Vk();

    s_last_terrain_tlas_instances = submitted;
}

void TerrainRender_DebugAppendInfo(void)
{
    Com_Printf("[TERRAIN] vk terrain ready: %s\n", s_terrain_vk_ready ? "yes" : "no");
    Com_Printf("[TERRAIN] terrain_rtx_instance: %d\n", terrain_rtx_instance && terrain_rtx_instance->integer ? 1 : 0);
    if (terrain_uv_scale)
        Com_Printf("[TERRAIN] terrain_uv_scale: %f\n", terrain_uv_scale->value);
    Com_Printf("[TERRAIN] chunks w/ GPU position data: %d (geom slots: %d)\n", s_chunks_with_gpu_positions,
               s_chunk_geom_slots);
    Com_Printf("[TERRAIN] chunks w/ BLAS built: %d\n", s_chunks_with_blas);
    Com_Printf("[TERRAIN] terrain GPU bytes (position buffer): %llu\n", (unsigned long long)s_terrain_gpu_bytes);
    Com_Printf("[TERRAIN] terrain GPU bytes (primitive buffer): %llu\n", (unsigned long long)s_terrain_prim_bytes);
    Com_Printf("[TERRAIN] terrain primitive count: %d\n", s_terrain_prim_count);
    Com_Printf("[TERRAIN] TLAS terrain chunk instances submitted (last frame): %d\n", s_last_terrain_tlas_instances);
    Com_Printf("[TERRAIN] material mode: flat default (stock floor *.wal from baseq2.mat)\n");
    TerrainWater_DebugAppendInfo();
}

} /* extern "C" */

#endif /* QUASIMODO_TERRAIN */
