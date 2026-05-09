/*
 * Quasimodo RTX — terrain Vulkan RTX Phase 5A (GPU positions + static chunk BLAS, no TLAS).
 */

#include "terrain.h"
#include "terrain_internal.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if QUASIMODO_TERRAIN

extern "C" {
#include "../../refresh/vkpt/vkpt.h"
#include "../../refresh/vkpt/streamline_reflex.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/zone.h"

extern cvar_t *terrain_enable;
extern cvar_t *terrain_rtx_instance; /* defined in terrain.cpp */

/* Mirrors vertex_buffer.c ACCEL_STRUCT_ALIGNMENT */
#define TERRAIN_ACCEL_STRUCT_ALIGNMENT 256

static BufferResource_t s_terrain_pos_buf;
static model_geometry_t *s_chunk_geoms = nullptr;
static int s_chunk_geom_slots = 0;
static int s_chunks_with_blas = 0;
static int s_chunks_with_gpu_positions = 0;
static VkDeviceSize s_terrain_gpu_bytes = 0;
static bool s_terrain_vk_ready = false;

static void terrain_gpu_free_all(void)
{
    if (!qvk.device)
        return;
    if (s_chunk_geom_slots == 0 && s_terrain_pos_buf.buffer == VK_NULL_HANDLE)
        return;

    if (SL_vkDeviceWaitIdle)
        SL_vkDeviceWaitIdle(qvk.device);
    else
        vkDeviceWaitIdle(qvk.device);

    if (s_chunk_geoms && s_chunk_geom_slots > 0) {
        for (int i = 0; i < s_chunk_geom_slots; i++)
            vkpt_destroy_model_geometry(&s_chunk_geoms[i]);
        Z_Free(s_chunk_geoms);
        s_chunk_geoms = nullptr;
        s_chunk_geom_slots = 0;
    }
    buffer_destroy(&s_terrain_pos_buf);
    s_chunks_with_blas = 0;
    s_chunks_with_gpu_positions = 0;
    s_terrain_gpu_bytes = 0;
}

static void terrain_suballocate_blas_memory(model_geometry_t *info, size_t *vbo_size, const char *model_name)
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

static void terrain_create_model_blas(model_geometry_t *info, VkBuffer buffer, const char *name)
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

static void terrain_build_model_blas(VkCommandBuffer cmd_buf, model_geometry_t *info, size_t first_vertex_offset,
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

    size_t vbo_size = (size_t)total_prims * sizeof(prim_positions_t);
    for (int i = 0; i < nchunks; i++) {
        char label[64];
        Q_snprintf(label, sizeof label, "terrain:blas[%d]", i);
        terrain_suballocate_blas_memory(&s_chunk_geoms[i], &vbo_size, label);
    }

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

    for (int i = 0; i < nchunks; i++) {
        char label[64];
        Q_snprintf(label, sizeof label, "terrain:blas[%d]", i);
        terrain_create_model_blas(&s_chunk_geoms[i], s_terrain_pos_buf.buffer, label);
        if (s_chunk_geoms[i].accel)
            s_chunks_with_blas++;
    }

    BufferResource_t staging_buffer;
    res = buffer_create(&staging_buffer, (size_t)total_prims * sizeof(prim_positions_t), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (res != VK_SUCCESS) {
        terrain_gpu_free_all();
        return res;
    }

    prim_positions_t *staging_positions = (prim_positions_t *)buffer_map(&staging_buffer);
    int prim_write = 0;
    for (int i = 0; i < nchunks; i++) {
        const int nt = terrain_chunk_triangle_count(&chunks[i]);
        if (nt <= 0)
            continue;
        terrain_fill_chunk_positions(&hf, &chunks[i], staging_positions, prim_write);
        prim_write += nt;
    }
    buffer_unmap(&staging_buffer);

    VkCommandBuffer cmd_buf = vkpt_begin_command_buffer(&qvk.cmd_buffers_graphics);

    VkBufferCopy copyRegion = {};
    copyRegion.size = staging_buffer.size;
    vkCmdCopyBuffer(cmd_buf, staging_buffer.buffer, s_terrain_pos_buf.buffer, 1, &copyRegion);

    VkBufferMemoryBarrier buf_barrier = {};
    buf_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    buf_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buf_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buf_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    buf_barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    buf_barrier.buffer = s_terrain_pos_buf.buffer;
    buf_barrier.offset = 0;
    buf_barrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 1,
                         &buf_barrier, 0, NULL);

    for (int i = 0; i < nchunks; i++)
        terrain_build_model_blas(cmd_buf, &s_chunk_geoms[i], 0, &s_terrain_pos_buf);

    vkpt_submit_command_buffer(cmd_buf, qvk.queue_graphics, (1 << qvk.device_count) - 1, 0, NULL, NULL, NULL, 0, NULL,
                               NULL, NULL);

    if (SL_vkDeviceWaitIdle)
        SL_vkDeviceWaitIdle(qvk.device);
    else
        vkDeviceWaitIdle(qvk.device);

    buffer_destroy(&staging_buffer);

    s_terrain_gpu_bytes = (VkDeviceSize)s_terrain_pos_buf.size;
    s_chunks_with_gpu_positions = chunks_with_tris;
    return VK_SUCCESS;
}

VkResult Terrain_InitVk(void)
{
    s_terrain_vk_ready = true;
    return VK_SUCCESS;
}

VkResult Terrain_DestroyVk(void)
{
    terrain_gpu_free_all();
    s_terrain_vk_ready = false;
    return VK_SUCCESS;
}

void Terrain_OnMapUnload_Vk(void)
{
    if (!s_terrain_vk_ready)
        return;
    terrain_gpu_free_all();
}

void Terrain_OnMapLoaded_Vk(const char *mapname)
{
    (void)mapname;

    if (!s_terrain_vk_ready)
        return;
    if (!terrain_enable || !terrain_enable->integer)
        return;
    if (!Terrain_IsLoaded()) {
        terrain_gpu_free_all();
        return;
    }

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

void TerrainRender_DebugAppendInfo(void)
{
    Com_Printf("[TERRAIN] vk terrain ready: %s\n", s_terrain_vk_ready ? "yes" : "no");
    Com_Printf("[TERRAIN] chunks w/ GPU position data: %d (geom slots: %d)\n", s_chunks_with_gpu_positions,
               s_chunk_geom_slots);
    Com_Printf("[TERRAIN] chunks w/ BLAS built: %d\n", s_chunks_with_blas);
    Com_Printf("[TERRAIN] terrain GPU bytes (position buffer): %llu\n", (unsigned long long)s_terrain_gpu_bytes);
    Com_Printf("[TERRAIN] Terrain_InstanceBLAS: Phase 5A no-op (terrain_rtx_instance=%d; TLAS deferred)\n",
               terrain_rtx_instance && terrain_rtx_instance->integer ? 1 : 0);
}

} /* extern "C" */

#endif /* QUASIMODO_TERRAIN */
