/*
 * NRD (NVIDIA Real-Time Denoisers) integration wrapper for Quasimodo-RTX.
 *
 * Phase 0: scaffolding — create/destroy NRD instance.
 * Phase 1: Vulkan resource creation — pipelines, pool textures, samplers,
 *          descriptor sets, constant buffer.  No dispatching yet.
 */

#include "NRD.h"

extern "C" {
#include "nrd_integration.h"
#include "vkpt.h"
}

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/*  Forward declarations                                               */
/* ------------------------------------------------------------------ */

static VkFormat  nrd_format_to_vk(nrd::Format fmt);
static bool      create_pool_textures(uint16_t render_width, uint16_t render_height);
static void      destroy_pool_textures(void);
static bool      create_samplers(void);
static void      destroy_samplers(void);
static bool      create_pipelines(void);
static void      destroy_pipelines(void);
static bool      create_descriptor_resources(void);
static void      destroy_descriptor_resources(void);
static bool      create_constant_buffer(void);
static void      destroy_constant_buffer(void);

/* ------------------------------------------------------------------ */
/*  Per-texture bookkeeping                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    VkImage        image;
    VkImageView    view;
    VkDeviceMemory memory;
    VkFormat       format;
    uint32_t       width;
    uint32_t       height;
} NrdTexture;

/* ------------------------------------------------------------------ */
/*  Module state                                                       */
/* ------------------------------------------------------------------ */

static nrd::Instance *s_nrd_instance  = nullptr;
static bool           s_initialized   = false;

/* Pool textures */
#define NRD_MAX_PERMANENT_TEXTURES 16
#define NRD_MAX_TRANSIENT_TEXTURES 16

static NrdTexture s_permanent_pool[NRD_MAX_PERMANENT_TEXTURES];
static NrdTexture s_transient_pool[NRD_MAX_TRANSIENT_TEXTURES];
static uint32_t   s_permanent_count = 0;
static uint32_t   s_transient_count = 0;

/* Samplers */
static VkSampler s_samplers[2]; /* NEAREST_CLAMP, LINEAR_CLAMP */

/* Pipelines */
#define NRD_MAX_PIPELINES 64

static VkShaderModule    s_shader_modules[NRD_MAX_PIPELINES];
static VkPipeline        s_pipelines[NRD_MAX_PIPELINES];
static VkPipelineLayout  s_pipeline_layout = VK_NULL_HANDLE;
static uint32_t          s_pipeline_count  = 0;

/* Descriptor resources */
static VkDescriptorSetLayout s_desc_set_layout_cb_samplers = VK_NULL_HANDLE;
static VkDescriptorSetLayout s_desc_set_layout_resources   = VK_NULL_HANDLE;
static VkDescriptorPool      s_desc_pool                   = VK_NULL_HANDLE;

/* Constant buffer (host-visible, double-buffered) */
static BufferResource_t s_constant_buffers[MAX_FRAMES_IN_FLIGHT];

/* Render dimensions for pool texture sizing */
static uint16_t s_render_width  = 0;
static uint16_t s_render_height = 0;

/* ------------------------------------------------------------------ */
/*  NRD Format → VkFormat                                              */
/* ------------------------------------------------------------------ */

static VkFormat nrd_format_to_vk(nrd::Format fmt)
{
    switch (fmt)
    {
    case nrd::Format::R8_UNORM:              return VK_FORMAT_R8_UNORM;
    case nrd::Format::R8_SNORM:              return VK_FORMAT_R8_SNORM;
    case nrd::Format::R8_UINT:               return VK_FORMAT_R8_UINT;
    case nrd::Format::R8_SINT:               return VK_FORMAT_R8_SINT;
    case nrd::Format::RG8_UNORM:             return VK_FORMAT_R8G8_UNORM;
    case nrd::Format::RG8_SNORM:             return VK_FORMAT_R8G8_SNORM;
    case nrd::Format::RG8_UINT:              return VK_FORMAT_R8G8_UINT;
    case nrd::Format::RG8_SINT:              return VK_FORMAT_R8G8_SINT;
    case nrd::Format::RGBA8_UNORM:           return VK_FORMAT_R8G8B8A8_UNORM;
    case nrd::Format::RGBA8_SNORM:           return VK_FORMAT_R8G8B8A8_SNORM;
    case nrd::Format::RGBA8_UINT:            return VK_FORMAT_R8G8B8A8_UINT;
    case nrd::Format::RGBA8_SINT:            return VK_FORMAT_R8G8B8A8_SINT;
    case nrd::Format::RGBA8_SRGB:            return VK_FORMAT_R8G8B8A8_SRGB;
    case nrd::Format::R16_UNORM:             return VK_FORMAT_R16_UNORM;
    case nrd::Format::R16_SNORM:             return VK_FORMAT_R16_SNORM;
    case nrd::Format::R16_UINT:              return VK_FORMAT_R16_UINT;
    case nrd::Format::R16_SINT:              return VK_FORMAT_R16_SINT;
    case nrd::Format::R16_SFLOAT:            return VK_FORMAT_R16_SFLOAT;
    case nrd::Format::RG16_UNORM:            return VK_FORMAT_R16G16_UNORM;
    case nrd::Format::RG16_SNORM:            return VK_FORMAT_R16G16_SNORM;
    case nrd::Format::RG16_UINT:             return VK_FORMAT_R16G16_UINT;
    case nrd::Format::RG16_SINT:             return VK_FORMAT_R16G16_SINT;
    case nrd::Format::RG16_SFLOAT:           return VK_FORMAT_R16G16_SFLOAT;
    case nrd::Format::RGBA16_UNORM:          return VK_FORMAT_R16G16B16A16_UNORM;
    case nrd::Format::RGBA16_SNORM:          return VK_FORMAT_R16G16B16A16_SNORM;
    case nrd::Format::RGBA16_UINT:           return VK_FORMAT_R16G16B16A16_UINT;
    case nrd::Format::RGBA16_SINT:           return VK_FORMAT_R16G16B16A16_SINT;
    case nrd::Format::RGBA16_SFLOAT:         return VK_FORMAT_R16G16B16A16_SFLOAT;
    case nrd::Format::R32_UINT:              return VK_FORMAT_R32_UINT;
    case nrd::Format::R32_SINT:              return VK_FORMAT_R32_SINT;
    case nrd::Format::R32_SFLOAT:            return VK_FORMAT_R32_SFLOAT;
    case nrd::Format::RG32_UINT:             return VK_FORMAT_R32G32_UINT;
    case nrd::Format::RG32_SINT:             return VK_FORMAT_R32G32_SINT;
    case nrd::Format::RG32_SFLOAT:           return VK_FORMAT_R32G32_SFLOAT;
    case nrd::Format::RGB32_UINT:            return VK_FORMAT_R32G32B32_UINT;
    case nrd::Format::RGB32_SINT:            return VK_FORMAT_R32G32B32_SINT;
    case nrd::Format::RGB32_SFLOAT:          return VK_FORMAT_R32G32B32_SFLOAT;
    case nrd::Format::RGBA32_UINT:           return VK_FORMAT_R32G32B32A32_UINT;
    case nrd::Format::RGBA32_SINT:           return VK_FORMAT_R32G32B32A32_SINT;
    case nrd::Format::RGBA32_SFLOAT:         return VK_FORMAT_R32G32B32A32_SFLOAT;
    case nrd::Format::R10_G10_B10_A2_UNORM:  return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    case nrd::Format::R10_G10_B10_A2_UINT:   return VK_FORMAT_A2B10G10R10_UINT_PACK32;
    case nrd::Format::R11_G11_B10_UFLOAT:    return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    case nrd::Format::R9_G9_B9_E5_UFLOAT:   return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;
    default:                                 return VK_FORMAT_UNDEFINED;
    }
}

/* ------------------------------------------------------------------ */
/*  Pool texture helpers                                               */
/* ------------------------------------------------------------------ */

static bool create_single_texture(NrdTexture *tex, VkFormat format,
                                  uint32_t width, uint32_t height, const char *label)
{
    tex->format = format;
    tex->width  = width;
    tex->height = height;

    VkImageCreateInfo img_info = {};
    img_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.imageType     = VK_IMAGE_TYPE_2D;
    img_info.format        = format;
    img_info.extent.width  = width;
    img_info.extent.height = height;
    img_info.extent.depth  = 1;
    img_info.mipLevels     = 1;
    img_info.arrayLayers   = 1;
    img_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    img_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    img_info.usage         = VK_IMAGE_USAGE_STORAGE_BIT
                           | VK_IMAGE_USAGE_SAMPLED_BIT
                           | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    img_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkResult res = vkCreateImage(qvk.device, &img_info, nullptr, &tex->image);
    if (res != VK_SUCCESS) {
        Com_EPrintf("[NRD] vkCreateImage failed for %s: %d\n", label, res);
        return false;
    }

    VkMemoryRequirements mem_req;
    vkGetImageMemoryRequirements(qvk.device, tex->image, &mem_req);

    res = allocate_gpu_memory(mem_req, &tex->memory);
    if (res != VK_SUCCESS) {
        Com_EPrintf("[NRD] allocate_gpu_memory failed for %s: %d\n", label, res);
        vkDestroyImage(qvk.device, tex->image, nullptr);
        tex->image = VK_NULL_HANDLE;
        return false;
    }

    vkBindImageMemory(qvk.device, tex->image, tex->memory, 0);

    VkImageViewCreateInfo view_info = {};
    view_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image                           = tex->image;
    view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format                          = format;
    view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel   = 0;
    view_info.subresourceRange.levelCount     = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount     = 1;

    res = vkCreateImageView(qvk.device, &view_info, nullptr, &tex->view);
    if (res != VK_SUCCESS) {
        Com_EPrintf("[NRD] vkCreateImageView failed for %s: %d\n", label, res);
        vkDestroyImage(qvk.device, tex->image, nullptr);
        vkFreeMemory(qvk.device, tex->memory, nullptr);
        tex->image  = VK_NULL_HANDLE;
        tex->memory = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

static void destroy_single_texture(NrdTexture *tex)
{
    if (tex->view)   { vkDestroyImageView(qvk.device, tex->view, nullptr);   tex->view   = VK_NULL_HANDLE; }
    if (tex->image)  { vkDestroyImage(qvk.device, tex->image, nullptr);      tex->image  = VK_NULL_HANDLE; }
    if (tex->memory) { vkFreeMemory(qvk.device, tex->memory, nullptr);       tex->memory = VK_NULL_HANDLE; }
}

static bool create_pool_textures(uint16_t render_width, uint16_t render_height)
{
    const nrd::InstanceDesc *inst = nrd::GetInstanceDesc(*s_nrd_instance);
    if (!inst) return false;

    s_permanent_count = inst->permanentPoolSize;
    s_transient_count = inst->transientPoolSize;

    if (s_permanent_count > NRD_MAX_PERMANENT_TEXTURES ||
        s_transient_count > NRD_MAX_TRANSIENT_TEXTURES) {
        Com_EPrintf("[NRD] Pool sizes exceed static limits (%u perm, %u trans)\n",
                    s_permanent_count, s_transient_count);
        return false;
    }

    memset(s_permanent_pool, 0, sizeof(s_permanent_pool));
    memset(s_transient_pool, 0, sizeof(s_transient_pool));

    for (uint32_t i = 0; i < s_permanent_count; i++) {
        const nrd::TextureDesc &td = inst->permanentPool[i];
        VkFormat fmt = nrd_format_to_vk(td.format);
        uint32_t w = render_width  / td.downsampleFactor;
        uint32_t h = render_height / td.downsampleFactor;
        if (w == 0) w = 1;
        if (h == 0) h = 1;

        char label[64];
        snprintf(label, sizeof(label), "NRD_perm_%u", i);

        if (!create_single_texture(&s_permanent_pool[i], fmt, w, h, label)) {
            destroy_pool_textures();
            return false;
        }
    }

    for (uint32_t i = 0; i < s_transient_count; i++) {
        const nrd::TextureDesc &td = inst->transientPool[i];
        VkFormat fmt = nrd_format_to_vk(td.format);
        uint32_t w = render_width  / td.downsampleFactor;
        uint32_t h = render_height / td.downsampleFactor;
        if (w == 0) w = 1;
        if (h == 0) h = 1;

        char label[64];
        snprintf(label, sizeof(label), "NRD_trans_%u", i);

        if (!create_single_texture(&s_transient_pool[i], fmt, w, h, label)) {
            destroy_pool_textures();
            return false;
        }
    }

    Com_Printf("[NRD] Created %u permanent + %u transient textures (%ux%u)\n",
               s_permanent_count, s_transient_count, render_width, render_height);
    return true;
}

static void destroy_pool_textures(void)
{
    for (uint32_t i = 0; i < s_permanent_count; i++)
        destroy_single_texture(&s_permanent_pool[i]);
    for (uint32_t i = 0; i < s_transient_count; i++)
        destroy_single_texture(&s_transient_pool[i]);
    s_permanent_count = 0;
    s_transient_count = 0;
}

/* ------------------------------------------------------------------ */
/*  Samplers                                                           */
/* ------------------------------------------------------------------ */

static bool create_samplers(void)
{
    VkSamplerCreateInfo nearest_info = {};
    nearest_info.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    nearest_info.magFilter    = VK_FILTER_NEAREST;
    nearest_info.minFilter    = VK_FILTER_NEAREST;
    nearest_info.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    nearest_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    nearest_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    nearest_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    nearest_info.maxLod       = VK_LOD_CLAMP_NONE;

    VkResult res = vkCreateSampler(qvk.device, &nearest_info, nullptr,
                                   &s_samplers[(uint32_t)nrd::Sampler::NEAREST_CLAMP]);
    if (res != VK_SUCCESS) {
        Com_EPrintf("[NRD] Failed to create NEAREST_CLAMP sampler: %d\n", res);
        return false;
    }

    VkSamplerCreateInfo linear_info = nearest_info;
    linear_info.magFilter  = VK_FILTER_LINEAR;
    linear_info.minFilter  = VK_FILTER_LINEAR;
    linear_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    res = vkCreateSampler(qvk.device, &linear_info, nullptr,
                          &s_samplers[(uint32_t)nrd::Sampler::LINEAR_CLAMP]);
    if (res != VK_SUCCESS) {
        Com_EPrintf("[NRD] Failed to create LINEAR_CLAMP sampler: %d\n", res);
        vkDestroySampler(qvk.device, s_samplers[0], nullptr);
        s_samplers[0] = VK_NULL_HANDLE;
        return false;
    }

    Com_Printf("[NRD] Created 2 samplers (NEAREST_CLAMP, LINEAR_CLAMP)\n");
    return true;
}

static void destroy_samplers(void)
{
    for (int i = 0; i < 2; i++) {
        if (s_samplers[i]) {
            vkDestroySampler(qvk.device, s_samplers[i], nullptr);
            s_samplers[i] = VK_NULL_HANDLE;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Descriptor set layouts, pool                                       */
/* ------------------------------------------------------------------ */

static bool create_descriptor_resources(void)
{
    const nrd::InstanceDesc *inst = nrd::GetInstanceDesc(*s_nrd_instance);
    if (!inst) return false;

    const nrd::LibraryDesc *lib = nrd::GetLibraryDesc();

    /* --- Set 0: constant buffer (as UBO) + immutable samplers --- */
    VkDescriptorSetLayoutBinding bindings_set0[3];
    memset(bindings_set0, 0, sizeof(bindings_set0));
    uint32_t binding_count_set0 = 0;

    /* Constant buffer */
    bindings_set0[binding_count_set0].binding         = lib->spirvBindingOffsets.constantBufferOffset;
    bindings_set0[binding_count_set0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    bindings_set0[binding_count_set0].descriptorCount = 1;
    bindings_set0[binding_count_set0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    binding_count_set0++;

    /* Samplers (immutable) */
    bindings_set0[binding_count_set0].binding            = lib->spirvBindingOffsets.samplerOffset + 0;
    bindings_set0[binding_count_set0].descriptorType     = VK_DESCRIPTOR_TYPE_SAMPLER;
    bindings_set0[binding_count_set0].descriptorCount    = 1;
    bindings_set0[binding_count_set0].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings_set0[binding_count_set0].pImmutableSamplers = &s_samplers[(uint32_t)nrd::Sampler::NEAREST_CLAMP];
    binding_count_set0++;

    bindings_set0[binding_count_set0].binding            = lib->spirvBindingOffsets.samplerOffset + 1;
    bindings_set0[binding_count_set0].descriptorType     = VK_DESCRIPTOR_TYPE_SAMPLER;
    bindings_set0[binding_count_set0].descriptorCount    = 1;
    bindings_set0[binding_count_set0].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings_set0[binding_count_set0].pImmutableSamplers = &s_samplers[(uint32_t)nrd::Sampler::LINEAR_CLAMP];
    binding_count_set0++;

    VkDescriptorSetLayoutCreateInfo layout_info_set0 = {};
    layout_info_set0.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info_set0.bindingCount = binding_count_set0;
    layout_info_set0.pBindings    = bindings_set0;

    VkResult res = vkCreateDescriptorSetLayout(qvk.device, &layout_info_set0,
                                               nullptr, &s_desc_set_layout_cb_samplers);
    if (res != VK_SUCCESS) {
        Com_EPrintf("[NRD] Failed to create CB+sampler desc set layout: %d\n", res);
        return false;
    }

    /* --- Set 1: texture inputs (sampled) + storage texture outputs --- */
    uint32_t max_textures = inst->descriptorPoolDesc.perSetTexturesMaxNum;
    uint32_t max_storage  = inst->descriptorPoolDesc.perSetStorageTexturesMaxNum;

    VkDescriptorSetLayoutBinding *bindings_set1 =
        (VkDescriptorSetLayoutBinding *)calloc(max_textures + max_storage,
                                               sizeof(VkDescriptorSetLayoutBinding));
    uint32_t binding_count_set1 = 0;

    for (uint32_t i = 0; i < max_textures; i++) {
        bindings_set1[binding_count_set1].binding         = lib->spirvBindingOffsets.textureOffset + i;
        bindings_set1[binding_count_set1].descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        bindings_set1[binding_count_set1].descriptorCount = 1;
        bindings_set1[binding_count_set1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        binding_count_set1++;
    }

    for (uint32_t i = 0; i < max_storage; i++) {
        bindings_set1[binding_count_set1].binding         = lib->spirvBindingOffsets.storageTextureAndBufferOffset + i;
        bindings_set1[binding_count_set1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings_set1[binding_count_set1].descriptorCount = 1;
        bindings_set1[binding_count_set1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        binding_count_set1++;
    }

    VkDescriptorSetLayoutCreateInfo layout_info_set1 = {};
    layout_info_set1.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info_set1.bindingCount = binding_count_set1;
    layout_info_set1.pBindings    = bindings_set1;

    res = vkCreateDescriptorSetLayout(qvk.device, &layout_info_set1,
                                      nullptr, &s_desc_set_layout_resources);
    free(bindings_set1);

    if (res != VK_SUCCESS) {
        Com_EPrintf("[NRD] Failed to create resource desc set layout: %d\n", res);
        vkDestroyDescriptorSetLayout(qvk.device, s_desc_set_layout_cb_samplers, nullptr);
        s_desc_set_layout_cb_samplers = VK_NULL_HANDLE;
        return false;
    }

    /* --- Descriptor pool --- */
    uint32_t max_sets = inst->descriptorPoolDesc.setsMaxNum * MAX_FRAMES_IN_FLIGHT;

    VkDescriptorPoolSize pool_sizes[4];
    pool_sizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    pool_sizes[0].descriptorCount = max_sets;
    pool_sizes[1].type            = VK_DESCRIPTOR_TYPE_SAMPLER;
    pool_sizes[1].descriptorCount = max_sets * 2;
    pool_sizes[2].type            = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    pool_sizes[2].descriptorCount = inst->descriptorPoolDesc.totalTexturesNum * MAX_FRAMES_IN_FLIGHT;
    pool_sizes[3].type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    pool_sizes[3].descriptorCount = inst->descriptorPoolDesc.totalStorageTexturesNum * MAX_FRAMES_IN_FLIGHT;

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets       = max_sets * 2;
    pool_info.poolSizeCount = 4;
    pool_info.pPoolSizes    = pool_sizes;

    res = vkCreateDescriptorPool(qvk.device, &pool_info, nullptr, &s_desc_pool);
    if (res != VK_SUCCESS) {
        Com_EPrintf("[NRD] Failed to create descriptor pool: %d\n", res);
        destroy_descriptor_resources();
        return false;
    }

    Com_Printf("[NRD] Descriptor resources: set0 (CB+%u samplers), set1 (%u tex + %u storage), pool (%u sets)\n",
               inst->samplersNum, max_textures, max_storage, max_sets * 2);
    return true;
}

static void destroy_descriptor_resources(void)
{
    if (s_desc_pool) {
        vkDestroyDescriptorPool(qvk.device, s_desc_pool, nullptr);
        s_desc_pool = VK_NULL_HANDLE;
    }
    if (s_desc_set_layout_resources) {
        vkDestroyDescriptorSetLayout(qvk.device, s_desc_set_layout_resources, nullptr);
        s_desc_set_layout_resources = VK_NULL_HANDLE;
    }
    if (s_desc_set_layout_cb_samplers) {
        vkDestroyDescriptorSetLayout(qvk.device, s_desc_set_layout_cb_samplers, nullptr);
        s_desc_set_layout_cb_samplers = VK_NULL_HANDLE;
    }
}

/* ------------------------------------------------------------------ */
/*  Pipelines                                                          */
/* ------------------------------------------------------------------ */

static bool create_pipelines(void)
{
    const nrd::InstanceDesc *inst = nrd::GetInstanceDesc(*s_nrd_instance);
    if (!inst) return false;

    s_pipeline_count = inst->pipelinesNum;
    if (s_pipeline_count > NRD_MAX_PIPELINES) {
        Com_EPrintf("[NRD] Pipeline count %u exceeds limit %u\n",
                    s_pipeline_count, NRD_MAX_PIPELINES);
        return false;
    }

    /* Create pipeline layout: set0 = CB+samplers, set1 = resources */
    VkDescriptorSetLayout set_layouts[] = {
        s_desc_set_layout_cb_samplers,
        s_desc_set_layout_resources,
    };

    VkPipelineLayoutCreateInfo layout_info = {};
    layout_info.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 2;
    layout_info.pSetLayouts    = set_layouts;

    VkResult res = vkCreatePipelineLayout(qvk.device, &layout_info, nullptr,
                                          &s_pipeline_layout);
    if (res != VK_SUCCESS) {
        Com_EPrintf("[NRD] Failed to create pipeline layout: %d\n", res);
        return false;
    }

    /* Create shader modules and compute pipelines */
    VkComputePipelineCreateInfo *pipe_infos =
        (VkComputePipelineCreateInfo *)calloc(s_pipeline_count,
                                              sizeof(VkComputePipelineCreateInfo));

    for (uint32_t i = 0; i < s_pipeline_count; i++) {
        const nrd::PipelineDesc &pd = inst->pipelines[i];
        const nrd::ComputeShaderDesc &spirv = pd.computeShaderSPIRV;

        if (!spirv.bytecode || spirv.size == 0) {
            Com_EPrintf("[NRD] Pipeline %u (%s) has no SPIR-V bytecode\n",
                        i, pd.shaderIdentifier);
            free(pipe_infos);
            destroy_pipelines();
            return false;
        }

        VkShaderModuleCreateInfo sm_info = {};
        sm_info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        sm_info.codeSize = (size_t)spirv.size;
        sm_info.pCode    = (const uint32_t *)spirv.bytecode;

        res = vkCreateShaderModule(qvk.device, &sm_info, nullptr,
                                   &s_shader_modules[i]);
        if (res != VK_SUCCESS) {
            Com_EPrintf("[NRD] vkCreateShaderModule failed for pipeline %u: %d\n", i, res);
            free(pipe_infos);
            destroy_pipelines();
            return false;
        }

        pipe_infos[i].sType              = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipe_infos[i].stage.sType        = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipe_infos[i].stage.stage        = VK_SHADER_STAGE_COMPUTE_BIT;
        pipe_infos[i].stage.module       = s_shader_modules[i];
        pipe_infos[i].stage.pName        = inst->shaderEntryPoint;
        pipe_infos[i].layout             = s_pipeline_layout;
    }

    res = vkCreateComputePipelines(qvk.device, VK_NULL_HANDLE,
                                   s_pipeline_count, pipe_infos,
                                   nullptr, s_pipelines);
    free(pipe_infos);

    if (res != VK_SUCCESS) {
        Com_EPrintf("[NRD] vkCreateComputePipelines failed: %d\n", res);
        destroy_pipelines();
        return false;
    }

    Com_Printf("[NRD] Created %u compute pipelines (entry: %s)\n",
               s_pipeline_count, inst->shaderEntryPoint);
    return true;
}

static void destroy_pipelines(void)
{
    for (uint32_t i = 0; i < s_pipeline_count; i++) {
        if (s_pipelines[i]) {
            vkDestroyPipeline(qvk.device, s_pipelines[i], nullptr);
            s_pipelines[i] = VK_NULL_HANDLE;
        }
        if (s_shader_modules[i]) {
            vkDestroyShaderModule(qvk.device, s_shader_modules[i], nullptr);
            s_shader_modules[i] = VK_NULL_HANDLE;
        }
    }
    s_pipeline_count = 0;

    if (s_pipeline_layout) {
        vkDestroyPipelineLayout(qvk.device, s_pipeline_layout, nullptr);
        s_pipeline_layout = VK_NULL_HANDLE;
    }
}

/* ------------------------------------------------------------------ */
/*  Constant buffer                                                    */
/* ------------------------------------------------------------------ */

static bool create_constant_buffer(void)
{
    const nrd::InstanceDesc *inst = nrd::GetInstanceDesc(*s_nrd_instance);
    if (!inst) return false;

    VkDeviceSize cb_size = inst->constantBufferMaxDataSize;
    if (cb_size == 0) cb_size = 128 * 1024;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkResult res = buffer_create(&s_constant_buffers[i], cb_size,
                                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                                     | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (res != VK_SUCCESS) {
            Com_EPrintf("[NRD] Failed to create constant buffer %d: %d\n", i, res);
            destroy_constant_buffer();
            return false;
        }
    }

    Com_Printf("[NRD] Created %d constant buffers (%u bytes each)\n",
               MAX_FRAMES_IN_FLIGHT, (uint32_t)cb_size);
    return true;
}

static void destroy_constant_buffer(void)
{
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        buffer_destroy(&s_constant_buffers[i]);
    }
}

/* ------------------------------------------------------------------ */
/*  Public API (C linkage)                                             */
/* ------------------------------------------------------------------ */

extern "C" qboolean vkpt_nrd_init(uint16_t render_width, uint16_t render_height)
{
    if (s_initialized)
        return qtrue;

    /* --- Phase 0: NRD instance creation --- */
    const nrd::LibraryDesc *lib = nrd::GetLibraryDesc();
    if (!lib) {
        Com_EPrintf("[NRD] GetLibraryDesc() returned NULL\n");
        return qfalse;
    }

    Com_Printf("[NRD] Library v%u.%u.%u  normal_enc=%u  roughness_enc=%u  denoisers=%u\n",
               lib->versionMajor, lib->versionMinor, lib->versionBuild,
               (unsigned)lib->normalEncoding, (unsigned)lib->roughnessEncoding,
               lib->supportedDenoisersNum);

    Com_Printf("[NRD] SPIRV binding offsets: sampler=%u texture=%u cb=%u storage=%u\n",
               lib->spirvBindingOffsets.samplerOffset,
               lib->spirvBindingOffsets.textureOffset,
               lib->spirvBindingOffsets.constantBufferOffset,
               lib->spirvBindingOffsets.storageTextureAndBufferOffset);

    nrd::DenoiserDesc denoisers[1] = {};
    denoisers[0].denoiser   = nrd::Denoiser::RELAX_DIFFUSE_SPECULAR;
    denoisers[0].identifier = 0;

    nrd::InstanceCreationDesc desc = {};
    desc.denoisers    = denoisers;
    desc.denoisersNum = 1;

    nrd::Result result = nrd::CreateInstance(desc, s_nrd_instance);
    if (result != nrd::Result::SUCCESS) {
        Com_EPrintf("[NRD] CreateInstance failed (result=%u)\n", (unsigned)result);
        return qfalse;
    }

    const nrd::InstanceDesc *inst = nrd::GetInstanceDesc(*s_nrd_instance);
    if (inst) {
        Com_Printf("[NRD] Instance: %u pipelines, %u permanent tex, %u transient tex, CB max %u bytes\n",
                   inst->pipelinesNum, inst->permanentPoolSize,
                   inst->transientPoolSize, inst->constantBufferMaxDataSize);
    }

    /* --- Phase 1: Vulkan resources --- */
    s_render_width  = render_width;
    s_render_height = render_height;

    if (!create_samplers())            { vkpt_nrd_destroy(); return qfalse; }
    if (!create_descriptor_resources()){ vkpt_nrd_destroy(); return qfalse; }
    if (!create_pipelines())           { vkpt_nrd_destroy(); return qfalse; }
    if (!create_pool_textures(render_width, render_height))
                                       { vkpt_nrd_destroy(); return qfalse; }
    if (!create_constant_buffer())     { vkpt_nrd_destroy(); return qfalse; }

    s_initialized = true;
    Com_Printf("[NRD] Fully initialized (render %ux%u)\n", render_width, render_height);

    return qtrue;
}

extern "C" void vkpt_nrd_destroy(void)
{
    vkDeviceWaitIdle(qvk.device);

    destroy_constant_buffer();
    destroy_pool_textures();
    destroy_pipelines();
    destroy_descriptor_resources();
    destroy_samplers();

    if (s_nrd_instance) {
        nrd::DestroyInstance(*s_nrd_instance);
        s_nrd_instance = nullptr;
        Com_Printf("[NRD] Instance destroyed\n");
    }

    s_initialized   = false;
    s_render_width  = 0;
    s_render_height = 0;
}

extern "C" qboolean vkpt_nrd_is_initialized(void)
{
    return s_initialized ? qtrue : qfalse;
}

extern "C" qboolean vkpt_nrd_resize(uint16_t render_width, uint16_t render_height)
{
    if (!s_initialized)
        return qfalse;

    if (render_width == s_render_width && render_height == s_render_height)
        return qtrue;

    vkDeviceWaitIdle(qvk.device);

    destroy_pool_textures();

    s_render_width  = render_width;
    s_render_height = render_height;

    if (!create_pool_textures(render_width, render_height)) {
        Com_EPrintf("[NRD] resize: failed to recreate pool textures\n");
        return qfalse;
    }

    Com_Printf("[NRD] Resized pool textures to %ux%u\n", render_width, render_height);
    return qtrue;
}
