/*
 * NRD (NVIDIA Real-Time Denoisers) integration wrapper for Quasimodo-RTX.
 *
 * Phase 0: scaffolding — create/destroy NRD instance.
 * Phase 1: Vulkan resource creation — pipelines, pool textures, samplers,
 *          descriptor sets, constant buffer.  No dispatching yet.
 * Phase 2: Input packing — compute shader converts engine GBuffers into
 *          NRD-formatted input images (normal+roughness, radiance+hitdist).
 * Phase 3: NRD dispatch — SetCommonSettings, SetDenoiserSettings,
 *          GetComputeDispatches → record per-dispatch command buffer.
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

/* Phase 2: input packing */
static bool      create_input_images(uint16_t render_width, uint16_t render_height);
static void      destroy_input_images(void);
static bool      create_pack_descriptor_resources(void);
static void      destroy_pack_descriptor_resources(void);
static void      update_pack_descriptor_set(void);

/* Phase 3: NRD dispatch */
static bool      create_output_images(uint16_t render_width, uint16_t render_height);
static void      destroy_output_images(void);
static VkImageView resolve_nrd_image_view(nrd::ResourceType type, uint16_t index_in_pool,
                                          nrd::DescriptorType desc_type);

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
/*  Phase 2: NRD input images                                          */
/* ------------------------------------------------------------------ */

/* NRD input images that need format conversion from engine GBuffers.
 * IN_MV and IN_VIEWZ are aliased directly from engine images. */
static NrdTexture s_input_normal_roughness    = {};  /* R32_UINT (written as packed R10G10B10A2) */
static VkImageView s_input_normal_roughness_unorm_view = VK_NULL_HANDLE; /* A2B10G10R10_UNORM view */
static NrdTexture s_input_diff_radiance       = {};  /* RGBA16F  */
static NrdTexture s_input_spec_radiance       = {};  /* RGBA16F  */

/* Packing pipeline (engine compute shader that converts GBuffers → NRD) */
static VkDescriptorSetLayout s_pack_desc_set_layout = VK_NULL_HANDLE;
static VkDescriptorPool      s_pack_desc_pool       = VK_NULL_HANDLE;
static VkDescriptorSet       s_pack_desc_set        = VK_NULL_HANDLE;
static VkPipelineLayout      s_pack_pipeline_layout  = VK_NULL_HANDLE;
static VkPipeline            s_pack_pipeline         = VK_NULL_HANDLE;
static bool                  s_input_images_transitioned = false;

/* ------------------------------------------------------------------ */
/*  Phase 3: NRD output images & dispatch state                        */
/* ------------------------------------------------------------------ */

static NrdTexture s_output_diff_radiance = {};  /* RGBA16F denoised diffuse  */
static NrdTexture s_output_spec_radiance = {};  /* RGBA16F denoised specular */

static bool       s_pool_textures_transitioned = false;
static float      s_prev_jitter[2] = {0.0f, 0.0f};

/* ------------------------------------------------------------------ */
/*  Phase 4: NRD composite pipeline state                              */
/* ------------------------------------------------------------------ */

static VkDescriptorSetLayout s_comp_desc_set_layout = VK_NULL_HANDLE;
static VkDescriptorPool      s_comp_desc_pool       = VK_NULL_HANDLE;
static VkDescriptorSet       s_comp_desc_set        = VK_NULL_HANDLE;
static VkPipelineLayout      s_comp_pipeline_layout  = VK_NULL_HANDLE;
static VkPipeline            s_comp_pipeline         = VK_NULL_HANDLE;

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

    /* Size enough for all dispatches, not just one.
     * constantBufferMaxDataSize is the per-dispatch maximum; multiply by
     * pipeline count to cover sequential sub-allocation during dispatch. */
    VkDeviceSize per_dispatch = inst->constantBufferMaxDataSize;
    if (per_dispatch == 0) per_dispatch = 4096;
    VkDeviceSize cb_size = per_dispatch * inst->pipelinesNum;
    if (cb_size < 128 * 1024) cb_size = 128 * 1024;

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
/*  Phase 2: NRD input images                                          */
/* ------------------------------------------------------------------ */

/* Variant of create_single_texture that sets MUTABLE_FORMAT_BIT and
   creates a second VkImageView with an alternate format for sampling. */
static bool create_mutable_texture(NrdTexture *tex, VkFormat storage_fmt,
                                   VkFormat sampled_fmt, VkImageView *out_sampled_view,
                                   uint32_t width, uint32_t height, const char *label)
{
    tex->format = storage_fmt;
    tex->width  = width;
    tex->height = height;

    VkFormat view_formats[2];
    view_formats[0] = storage_fmt;
    view_formats[1] = sampled_fmt;

    VkImageFormatListCreateInfo fmt_list = {};
    fmt_list.sType           = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO;
    fmt_list.viewFormatCount = 2;
    fmt_list.pViewFormats    = view_formats;

    VkImageCreateInfo img_info = {};
    img_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.pNext         = &fmt_list;
    img_info.flags         = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    img_info.imageType     = VK_IMAGE_TYPE_2D;
    img_info.format        = storage_fmt;
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

    /* Storage-format view (for imageStore in packing shader) */
    VkImageViewCreateInfo view_info = {};
    view_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image                           = tex->image;
    view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format                          = storage_fmt;
    view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel   = 0;
    view_info.subresourceRange.levelCount     = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount     = 1;

    res = vkCreateImageView(qvk.device, &view_info, nullptr, &tex->view);
    if (res != VK_SUCCESS) {
        Com_EPrintf("[NRD] vkCreateImageView (storage) failed for %s: %d\n", label, res);
        destroy_single_texture(tex);
        return false;
    }

    /* Sampled-format view (for NRD's texture2D reads) */
    view_info.format = sampled_fmt;
    res = vkCreateImageView(qvk.device, &view_info, nullptr, out_sampled_view);
    if (res != VK_SUCCESS) {
        Com_EPrintf("[NRD] vkCreateImageView (sampled) failed for %s: %d\n", label, res);
        destroy_single_texture(tex);
        return false;
    }

    return true;
}

static bool create_input_images(uint16_t render_width, uint16_t render_height)
{
    /* Normal+Roughness: R32_UINT for storage writes, A2B10G10R10_UNORM for NRD reads */
    if (!create_mutable_texture(&s_input_normal_roughness,
                                VK_FORMAT_R32_UINT,
                                VK_FORMAT_A2B10G10R10_UNORM_PACK32,
                                &s_input_normal_roughness_unorm_view,
                                render_width, render_height,
                                "NRD_IN_NORMAL_ROUGHNESS")) {
        return false;
    }

    /* Diffuse radiance + hit distance: RGBA16F */
    if (!create_single_texture(&s_input_diff_radiance,
                               VK_FORMAT_R16G16B16A16_SFLOAT,
                               render_width, render_height,
                               "NRD_IN_DIFF_RADIANCE_HITDIST")) {
        destroy_input_images();
        return false;
    }

    /* Specular radiance + hit distance: RGBA16F */
    if (!create_single_texture(&s_input_spec_radiance,
                               VK_FORMAT_R16G16B16A16_SFLOAT,
                               render_width, render_height,
                               "NRD_IN_SPEC_RADIANCE_HITDIST")) {
        destroy_input_images();
        return false;
    }

    Com_Printf("[NRD] Created 3 input images (%ux%u)\n", render_width, render_height);
    return true;
}

static void destroy_input_images(void)
{
    if (s_input_normal_roughness_unorm_view) {
        vkDestroyImageView(qvk.device, s_input_normal_roughness_unorm_view, nullptr);
        s_input_normal_roughness_unorm_view = VK_NULL_HANDLE;
    }
    destroy_single_texture(&s_input_normal_roughness);
    destroy_single_texture(&s_input_diff_radiance);
    destroy_single_texture(&s_input_spec_radiance);
    s_input_images_transitioned = false;
}

/* ------------------------------------------------------------------ */
/*  Phase 3: NRD output images                                         */
/* ------------------------------------------------------------------ */

static bool create_output_images(uint16_t render_width, uint16_t render_height)
{
    if (!create_single_texture(&s_output_diff_radiance,
                               VK_FORMAT_R16G16B16A16_SFLOAT,
                               render_width, render_height,
                               "NRD_OUT_DIFF_RADIANCE_HITDIST")) {
        return false;
    }

    if (!create_single_texture(&s_output_spec_radiance,
                               VK_FORMAT_R16G16B16A16_SFLOAT,
                               render_width, render_height,
                               "NRD_OUT_SPEC_RADIANCE_HITDIST")) {
        destroy_single_texture(&s_output_diff_radiance);
        return false;
    }

    Com_Printf("[NRD] Created 2 output images (%ux%u)\n", render_width, render_height);
    return true;
}

static void destroy_output_images(void)
{
    destroy_single_texture(&s_output_diff_radiance);
    destroy_single_texture(&s_output_spec_radiance);
    s_pool_textures_transitioned = false;
}

/* ------------------------------------------------------------------ */
/*  Phase 4: Composite descriptor set (reads NRD denoised outputs)     */
/* ------------------------------------------------------------------ */

static bool create_comp_descriptor_resources(void)
{
    /* Descriptor set layout: 2 storage images for NRD denoised diff+spec */
    VkDescriptorSetLayoutBinding bindings[2];
    memset(bindings, 0, sizeof(bindings));
    for (int i = 0; i < 2; i++) {
        bindings[i].binding         = (uint32_t)i;
        bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layout_info = {};
    layout_info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 2;
    layout_info.pBindings    = bindings;

    VkResult res = vkCreateDescriptorSetLayout(qvk.device, &layout_info,
                                               nullptr, &s_comp_desc_set_layout);
    if (res != VK_SUCCESS) {
        Com_EPrintf("[NRD] Failed to create comp desc set layout: %d\n", res);
        return false;
    }

    /* Descriptor pool */
    VkDescriptorPoolSize pool_size;
    pool_size.type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    pool_size.descriptorCount = 2;

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets       = 1;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes    = &pool_size;

    res = vkCreateDescriptorPool(qvk.device, &pool_info, nullptr, &s_comp_desc_pool);
    if (res != VK_SUCCESS) {
        Com_EPrintf("[NRD] Failed to create comp desc pool: %d\n", res);
        return false;
    }

    /* Allocate descriptor set */
    VkDescriptorSetAllocateInfo alloc_info = {};
    alloc_info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool     = s_comp_desc_pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts        = &s_comp_desc_set_layout;

    res = vkAllocateDescriptorSets(qvk.device, &alloc_info, &s_comp_desc_set);
    if (res != VK_SUCCESS) {
        Com_EPrintf("[NRD] Failed to allocate comp desc set: %d\n", res);
        return false;
    }

    Com_Printf("[NRD] Composite descriptor resources created\n");
    return true;
}

static void destroy_comp_descriptor_resources(void)
{
    s_comp_desc_set = VK_NULL_HANDLE;
    if (s_comp_desc_pool) {
        vkDestroyDescriptorPool(qvk.device, s_comp_desc_pool, nullptr);
        s_comp_desc_pool = VK_NULL_HANDLE;
    }
    if (s_comp_desc_set_layout) {
        vkDestroyDescriptorSetLayout(qvk.device, s_comp_desc_set_layout, nullptr);
        s_comp_desc_set_layout = VK_NULL_HANDLE;
    }
}

/* Write the 2 NRD output image views into the composite descriptor set. */
static void update_comp_descriptor_set(void)
{
    if (!s_comp_desc_set)
        return;

    VkDescriptorImageInfo img_infos[2];
    memset(img_infos, 0, sizeof(img_infos));

    img_infos[0].imageView   = s_output_diff_radiance.view;
    img_infos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    img_infos[1].imageView   = s_output_spec_radiance.view;
    img_infos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet writes[2];
    memset(writes, 0, sizeof(writes));
    for (int i = 0; i < 2; i++) {
        writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet          = s_comp_desc_set;
        writes[i].dstBinding      = (uint32_t)i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[i].pImageInfo      = &img_infos[i];
    }

    vkUpdateDescriptorSets(qvk.device, 2, writes, 0, nullptr);
}

/* ------------------------------------------------------------------ */
/*  Phase 2: Packing descriptor set & pipeline                         */
/* ------------------------------------------------------------------ */

static bool create_pack_descriptor_resources(void)
{
    /* Descriptor set layout: 3 storage images for NRD outputs */
    VkDescriptorSetLayoutBinding bindings[3];
    memset(bindings, 0, sizeof(bindings));
    for (int i = 0; i < 3; i++) {
        bindings[i].binding         = (uint32_t)i;
        bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layout_info = {};
    layout_info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 3;
    layout_info.pBindings    = bindings;

    VkResult res = vkCreateDescriptorSetLayout(qvk.device, &layout_info,
                                               nullptr, &s_pack_desc_set_layout);
    if (res != VK_SUCCESS) {
        Com_EPrintf("[NRD] Failed to create pack desc set layout: %d\n", res);
        return false;
    }

    /* Descriptor pool */
    VkDescriptorPoolSize pool_size;
    pool_size.type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    pool_size.descriptorCount = 3;

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets       = 1;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes    = &pool_size;

    res = vkCreateDescriptorPool(qvk.device, &pool_info, nullptr, &s_pack_desc_pool);
    if (res != VK_SUCCESS) {
        Com_EPrintf("[NRD] Failed to create pack desc pool: %d\n", res);
        destroy_pack_descriptor_resources();
        return false;
    }

    /* Allocate descriptor set */
    VkDescriptorSetAllocateInfo alloc_info = {};
    alloc_info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool     = s_pack_desc_pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts        = &s_pack_desc_set_layout;

    res = vkAllocateDescriptorSets(qvk.device, &alloc_info, &s_pack_desc_set);
    if (res != VK_SUCCESS) {
        Com_EPrintf("[NRD] Failed to allocate pack desc set: %d\n", res);
        destroy_pack_descriptor_resources();
        return false;
    }

    Com_Printf("[NRD] Pack descriptor resources created\n");
    return true;
}

static void destroy_pack_descriptor_resources(void)
{
    /* Descriptor set freed implicitly by pool destruction */
    s_pack_desc_set = VK_NULL_HANDLE;

    if (s_pack_desc_pool) {
        vkDestroyDescriptorPool(qvk.device, s_pack_desc_pool, nullptr);
        s_pack_desc_pool = VK_NULL_HANDLE;
    }
    if (s_pack_desc_set_layout) {
        vkDestroyDescriptorSetLayout(qvk.device, s_pack_desc_set_layout, nullptr);
        s_pack_desc_set_layout = VK_NULL_HANDLE;
    }
}

/* Write the 3 NRD storage image views into the packing descriptor set. */
static void update_pack_descriptor_set(void)
{
    if (!s_pack_desc_set)
        return;

    VkDescriptorImageInfo img_infos[3];
    memset(img_infos, 0, sizeof(img_infos));

    img_infos[0].imageView   = s_input_normal_roughness.view; /* R32_UINT storage view */
    img_infos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    img_infos[1].imageView   = s_input_diff_radiance.view;
    img_infos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    img_infos[2].imageView   = s_input_spec_radiance.view;
    img_infos[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet writes[3];
    memset(writes, 0, sizeof(writes));
    for (int i = 0; i < 3; i++) {
        writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet          = s_pack_desc_set;
        writes[i].dstBinding      = (uint32_t)i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[i].pImageInfo      = &img_infos[i];
    }

    vkUpdateDescriptorSets(qvk.device, 3, writes, 0, nullptr);
}

/* ------------------------------------------------------------------ */
/*  Phase 2: Image barriers (C++ compatible, no designated inits)      */
/* ------------------------------------------------------------------ */

static void barrier_compute_write(VkCommandBuffer cmd_buf, VkImage image)
{
    VkImageMemoryBarrier barrier = {};
    barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask                   = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout                       = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout                       = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                           = image;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;

    vkCmdPipelineBarrier(cmd_buf,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
}

static void transition_to_general(VkCommandBuffer cmd_buf, VkImage image)
{
    VkImageMemoryBarrier barrier = {};
    barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask                   = 0;
    barrier.dstAccessMask                   = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout                       = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                           = image;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;

    vkCmdPipelineBarrier(cmd_buf,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
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

    /* --- Phase 2: Input images + packing descriptors --- */
    if (!create_input_images(render_width, render_height))
                                       { vkpt_nrd_destroy(); return qfalse; }
    if (!create_pack_descriptor_resources())
                                       { vkpt_nrd_destroy(); return qfalse; }
    update_pack_descriptor_set();

    /* --- Phase 3: Output images --- */
    if (!create_output_images(render_width, render_height))
                                       { vkpt_nrd_destroy(); return qfalse; }

    /* --- Phase 4: Composite descriptors --- */
    if (!create_comp_descriptor_resources())
                                       { vkpt_nrd_destroy(); return qfalse; }
    update_comp_descriptor_set();

    s_initialized = true;
    Com_Printf("[NRD] Fully initialized (render %ux%u)\n", render_width, render_height);

    return qtrue;
}

extern "C" void vkpt_nrd_destroy(void)
{
    vkDeviceWaitIdle(qvk.device);

    /* Phase 4 teardown */
    destroy_comp_descriptor_resources();

    /* Phase 3 teardown */
    destroy_output_images();

    /* Phase 2 teardown */
    vkpt_nrd_destroy_pipelines();
    destroy_pack_descriptor_resources();
    destroy_input_images();

    /* Phase 1 teardown */
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
    destroy_input_images();
    destroy_output_images();

    s_render_width  = render_width;
    s_render_height = render_height;

    if (!create_pool_textures(render_width, render_height)) {
        Com_EPrintf("[NRD] resize: failed to recreate pool textures\n");
        return qfalse;
    }

    if (!create_input_images(render_width, render_height)) {
        Com_EPrintf("[NRD] resize: failed to recreate input images\n");
        return qfalse;
    }
    update_pack_descriptor_set();

    if (!create_output_images(render_width, render_height)) {
        Com_EPrintf("[NRD] resize: failed to recreate output images\n");
        return qfalse;
    }
    update_comp_descriptor_set();

    Com_Printf("[NRD] Resized to %ux%u\n", render_width, render_height);
    return qtrue;
}

/* ------------------------------------------------------------------ */
/*  Phase 2: Packing pipeline create / destroy (shader-reload safe)    */
/* ------------------------------------------------------------------ */

extern "C" VkResult vkpt_nrd_create_pipelines(void)
{
    if (!s_initialized || !s_pack_desc_set_layout)
        return VK_SUCCESS; /* silently skip if NRD not initialised */

    /* Pipeline layout: set0 = engine UBO, set1 = engine textures, set2 = NRD outputs */
    VkDescriptorSetLayout set_layouts[3];
    set_layouts[0] = qvk.desc_set_layout_ubo;
    set_layouts[1] = qvk.desc_set_layout_textures;
    set_layouts[2] = s_pack_desc_set_layout;

    VkPipelineLayoutCreateInfo layout_info = {};
    layout_info.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 3;
    layout_info.pSetLayouts    = set_layouts;

    VkResult res = vkCreatePipelineLayout(qvk.device, &layout_info, nullptr,
                                          &s_pack_pipeline_layout);
    if (res != VK_SUCCESS) {
        Com_EPrintf("[NRD] Failed to create pack pipeline layout: %d\n", res);
        return res;
    }

    /* Compute pipeline using engine shader QVK_MOD_NRD_PACK_INPUTS_COMP */
    VkPipelineShaderStageCreateInfo stage = {};
    stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = qvk.shader_modules[QVK_MOD_NRD_PACK_INPUTS_COMP];
    stage.pName  = "main";

    VkComputePipelineCreateInfo pipe_info = {};
    pipe_info.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipe_info.stage  = stage;
    pipe_info.layout = s_pack_pipeline_layout;

    res = vkCreateComputePipelines(qvk.device, VK_NULL_HANDLE, 1, &pipe_info,
                                   nullptr, &s_pack_pipeline);
    if (res != VK_SUCCESS) {
        Com_EPrintf("[NRD] Failed to create pack compute pipeline: %d\n", res);
        vkDestroyPipelineLayout(qvk.device, s_pack_pipeline_layout, nullptr);
        s_pack_pipeline_layout = VK_NULL_HANDLE;
        return res;
    }

    Com_Printf("[NRD] Pack pipeline created\n");

    /* ---- Composite pipeline (Phase 4) ---- */
    if (s_comp_desc_set_layout)
    {
        VkDescriptorSetLayout comp_layouts[3];
        comp_layouts[0] = qvk.desc_set_layout_ubo;
        comp_layouts[1] = qvk.desc_set_layout_textures;
        comp_layouts[2] = s_comp_desc_set_layout;

        VkPipelineLayoutCreateInfo comp_layout_info = {};
        comp_layout_info.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        comp_layout_info.setLayoutCount = 3;
        comp_layout_info.pSetLayouts    = comp_layouts;

        res = vkCreatePipelineLayout(qvk.device, &comp_layout_info, nullptr,
                                     &s_comp_pipeline_layout);
        if (res != VK_SUCCESS) {
            Com_EPrintf("[NRD] Failed to create comp pipeline layout: %d\n", res);
            return res;
        }

        VkPipelineShaderStageCreateInfo comp_stage = {};
        comp_stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        comp_stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        comp_stage.module = qvk.shader_modules[QVK_MOD_NRD_COMPOSITE_COMP];
        comp_stage.pName  = "main";

        VkComputePipelineCreateInfo comp_pipe_info = {};
        comp_pipe_info.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        comp_pipe_info.stage  = comp_stage;
        comp_pipe_info.layout = s_comp_pipeline_layout;

        res = vkCreateComputePipelines(qvk.device, VK_NULL_HANDLE, 1, &comp_pipe_info,
                                       nullptr, &s_comp_pipeline);
        if (res != VK_SUCCESS) {
            Com_EPrintf("[NRD] Failed to create comp pipeline: %d\n", res);
            vkDestroyPipelineLayout(qvk.device, s_comp_pipeline_layout, nullptr);
            s_comp_pipeline_layout = VK_NULL_HANDLE;
            return res;
        }

        Com_Printf("[NRD] Composite pipeline created\n");
    }

    return VK_SUCCESS;
}

extern "C" VkResult vkpt_nrd_destroy_pipelines(void)
{
    if (s_comp_pipeline) {
        vkDestroyPipeline(qvk.device, s_comp_pipeline, nullptr);
        s_comp_pipeline = VK_NULL_HANDLE;
    }
    if (s_comp_pipeline_layout) {
        vkDestroyPipelineLayout(qvk.device, s_comp_pipeline_layout, nullptr);
        s_comp_pipeline_layout = VK_NULL_HANDLE;
    }
    if (s_pack_pipeline) {
        vkDestroyPipeline(qvk.device, s_pack_pipeline, nullptr);
        s_pack_pipeline = VK_NULL_HANDLE;
    }
    if (s_pack_pipeline_layout) {
        vkDestroyPipelineLayout(qvk.device, s_pack_pipeline_layout, nullptr);
        s_pack_pipeline_layout = VK_NULL_HANDLE;
    }
    return VK_SUCCESS;
}

/* ------------------------------------------------------------------ */
/*  Phase 2: Input packing dispatch                                    */
/* ------------------------------------------------------------------ */

extern "C" VkResult vkpt_nrd_pack_inputs(VkCommandBuffer cmd_buf)
{
    if (!s_initialized || !s_pack_pipeline)
        return VK_SUCCESS;

    /* Transition NRD input images to GENERAL on first use */
    if (!s_input_images_transitioned) {
        transition_to_general(cmd_buf, s_input_normal_roughness.image);
        transition_to_general(cmd_buf, s_input_diff_radiance.image);
        transition_to_general(cmd_buf, s_input_spec_radiance.image);
        s_input_images_transitioned = true;
    }

    /* Bind pipeline */
    vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, s_pack_pipeline);

    /* Bind descriptor sets: set0=UBO, set1=textures, set2=NRD outputs */
    VkDescriptorSet desc_sets[3];
    desc_sets[0] = qvk.desc_set_ubo;
    desc_sets[1] = qvk_get_current_desc_set_textures();
    desc_sets[2] = s_pack_desc_set;

    vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
        s_pack_pipeline_layout, 0, 3, desc_sets, 0, nullptr);

    /* Dispatch: 16x16 workgroups over the render resolution */
    uint32_t gx = (qvk.gpu_slice_width + 15) / 16;
    uint32_t gy = (qvk.extent_render.height + 15) / 16;
    vkCmdDispatch(cmd_buf, gx, gy, 1);

    /* Barriers on all 3 NRD output images */
    barrier_compute_write(cmd_buf, s_input_normal_roughness.image);
    barrier_compute_write(cmd_buf, s_input_diff_radiance.image);
    barrier_compute_write(cmd_buf, s_input_spec_radiance.image);

    return VK_SUCCESS;
}

/* ------------------------------------------------------------------ */
/*  Phase 3: NRD resource resolver                                     */
/* ------------------------------------------------------------------ */

/* Map an NRD ResourceType + indexInPool to the corresponding VkImageView.
 * For IN_NORMAL_ROUGHNESS the view depends on descriptor type:
 *   TEXTURE → A2B10G10R10_UNORM (for NRD sampled reads)
 *   STORAGE_TEXTURE → R32_UINT (for storage writes)                      */
static VkImageView resolve_nrd_image_view(nrd::ResourceType type,
                                          uint16_t index_in_pool,
                                          nrd::DescriptorType desc_type)
{
    switch (type) {
    /* Engine aliased inputs */
    case nrd::ResourceType::IN_MV:
        return qvk.images_views[VKPT_IMG_PT_MOTION];
    case nrd::ResourceType::IN_VIEWZ:
        return qvk.images_views[VKPT_IMG_PT_VIEW_DEPTH_A];

    /* Converted inputs (written by pack shader) */
    case nrd::ResourceType::IN_NORMAL_ROUGHNESS:
        return (desc_type == nrd::DescriptorType::TEXTURE)
            ? s_input_normal_roughness_unorm_view
            : s_input_normal_roughness.view;
    case nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST:
        return s_input_diff_radiance.view;
    case nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST:
        return s_input_spec_radiance.view;

    /* Denoised outputs */
    case nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST:
        return s_output_diff_radiance.view;
    case nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST:
        return s_output_spec_radiance.view;

    /* Internal NRD pool textures */
    case nrd::ResourceType::TRANSIENT_POOL:
        if (index_in_pool < s_transient_count)
            return s_transient_pool[index_in_pool].view;
        Com_EPrintf("[NRD] TRANSIENT_POOL index %u out of range (%u)\n",
                    index_in_pool, s_transient_count);
        return VK_NULL_HANDLE;
    case nrd::ResourceType::PERMANENT_POOL:
        if (index_in_pool < s_permanent_count)
            return s_permanent_pool[index_in_pool].view;
        Com_EPrintf("[NRD] PERMANENT_POOL index %u out of range (%u)\n",
                    index_in_pool, s_permanent_count);
        return VK_NULL_HANDLE;

    default:
        Com_EPrintf("[NRD] Unhandled resource type %u\n", (unsigned)type);
        return VK_NULL_HANDLE;
    }
}

/* ------------------------------------------------------------------ */
/*  Phase 3: NRD dispatch                                              */
/* ------------------------------------------------------------------ */

extern "C" VkResult vkpt_nrd_dispatch(VkCommandBuffer cmd_buf)
{
    if (!s_initialized || !s_nrd_instance)
        return VK_SUCCESS;

    const nrd::InstanceDesc *inst = nrd::GetInstanceDesc(*s_nrd_instance);
    if (!inst) return VK_ERROR_INITIALIZATION_FAILED;

    const nrd::LibraryDesc *lib = nrd::GetLibraryDesc();
    int frame_idx = qvk.current_frame_index;

    /* --- Transition pool + output textures to GENERAL on first use --- */
    if (!s_pool_textures_transitioned) {
        for (uint32_t i = 0; i < s_permanent_count; i++)
            transition_to_general(cmd_buf, s_permanent_pool[i].image);
        for (uint32_t i = 0; i < s_transient_count; i++)
            transition_to_general(cmd_buf, s_transient_pool[i].image);
        transition_to_general(cmd_buf, s_output_diff_radiance.image);
        transition_to_general(cmd_buf, s_output_spec_radiance.image);
        s_pool_textures_transitioned = true;
    }

    /* --- SetCommonSettings --- */
    const QVKUniformBuffer_t *ubo = &vkpt_refdef.uniform_buffer;

    nrd::CommonSettings common_settings = {};

    /* Matrices — engine float[16] column-major matches NRD convention */
    memcpy(common_settings.viewToClipMatrix,     ubo->P,      sizeof(float) * 16);
    memcpy(common_settings.viewToClipMatrixPrev, ubo->P_prev, sizeof(float) * 16);
    memcpy(common_settings.worldToViewMatrix,     ubo->V,      sizeof(float) * 16);
    memcpy(common_settings.worldToViewMatrixPrev, ubo->V_prev, sizeof(float) * 16);
    /* worldPrevToWorldMatrix: identity (already default-initialized) */

    /* Motion vectors: engine stores UV [0,1] space, NRD wants pixel space */
    common_settings.motionVectorScale[0] = (float)s_render_width;
    common_settings.motionVectorScale[1] = (float)s_render_height;
    common_settings.motionVectorScale[2] = 1.0f;
    common_settings.isMotionVectorInWorldSpace = false;

    /* Render resolution */
    common_settings.resourceSize[0]     = s_render_width;
    common_settings.resourceSize[1]     = s_render_height;
    common_settings.resourceSizePrev[0] = s_render_width;   /* TODO: handle resize */
    common_settings.resourceSizePrev[1] = s_render_height;
    common_settings.rectSize[0]         = s_render_width;
    common_settings.rectSize[1]         = s_render_height;
    common_settings.rectSizePrev[0]     = s_render_width;
    common_settings.rectSizePrev[1]     = s_render_height;

    /* Frame index */
    common_settings.frameIndex = (uint32_t)(qvk.frame_counter & 0xFFFFFFFF);

    /* Camera jitter [-0.5, +0.5] pixels from TAA/DLSS */
    common_settings.cameraJitter[0]     = ubo->sub_pixel_jitter[0];
    common_settings.cameraJitter[1]     = ubo->sub_pixel_jitter[1];
    common_settings.cameraJitterPrev[0] = s_prev_jitter[0];
    common_settings.cameraJitterPrev[1] = s_prev_jitter[1];
    s_prev_jitter[0] = ubo->sub_pixel_jitter[0];
    s_prev_jitter[1] = ubo->sub_pixel_jitter[1];

    /* Defaults */
    common_settings.denoisingRange      = 500000.0f;
    common_settings.disocclusionThreshold = 0.01f;
    common_settings.accumulationMode    = nrd::AccumulationMode::CONTINUE;

    nrd::Result nrd_result = nrd::SetCommonSettings(*s_nrd_instance, common_settings);
    if (nrd_result != nrd::Result::SUCCESS) {
        Com_EPrintf("[NRD] SetCommonSettings failed: %u\n", (unsigned)nrd_result);
        return VK_ERROR_UNKNOWN;
    }

    /* --- SetDenoiserSettings (RELAX with NRD defaults) --- */
    nrd::RelaxSettings relax_settings = {};   /* C++ default member initializers */

    nrd::Identifier denoiser_id = 0;
    nrd_result = nrd::SetDenoiserSettings(*s_nrd_instance, denoiser_id, &relax_settings);
    if (nrd_result != nrd::Result::SUCCESS) {
        Com_EPrintf("[NRD] SetDenoiserSettings failed: %u\n", (unsigned)nrd_result);
        return VK_ERROR_UNKNOWN;
    }

    /* --- GetComputeDispatches --- */
    const nrd::DispatchDesc *dispatch_descs = nullptr;
    uint32_t dispatch_count = 0;

    nrd_result = nrd::GetComputeDispatches(*s_nrd_instance, &denoiser_id, 1,
                                           dispatch_descs, dispatch_count);
    if (nrd_result != nrd::Result::SUCCESS) {
        Com_EPrintf("[NRD] GetComputeDispatches failed: %u\n", (unsigned)nrd_result);
        return VK_ERROR_UNKNOWN;
    }

    if (dispatch_count == 0)
        return VK_SUCCESS;

    /* Reset NRD descriptor pool — all previous set allocations are freed */
    vkResetDescriptorPool(qvk.device, s_desc_pool, 0);

    /* Allocate set0 (constant buffer + immutable samplers) — shared by
     * all dispatches, re-bound with different dynamic offsets.           */
    VkDescriptorSet set0 = VK_NULL_HANDLE;
    {
        VkDescriptorSetAllocateInfo alloc_info = {};
        alloc_info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool     = s_desc_pool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts        = &s_desc_set_layout_cb_samplers;

        VkResult res = vkAllocateDescriptorSets(qvk.device, &alloc_info, &set0);
        if (res != VK_SUCCESS) {
            Com_EPrintf("[NRD] Failed to allocate set0: %d\n", res);
            return res;
        }

        /* Write CB descriptor — dynamic offset selects per-dispatch data */
        VkDescriptorBufferInfo buf_info = {};
        buf_info.buffer = s_constant_buffers[frame_idx].buffer;
        buf_info.offset = 0;
        buf_info.range  = inst->constantBufferMaxDataSize;

        VkWriteDescriptorSet write = {};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = set0;
        write.dstBinding      = lib->spirvBindingOffsets.constantBufferOffset;
        write.descriptorCount = 1;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        write.pBufferInfo     = &buf_info;

        vkUpdateDescriptorSets(qvk.device, 1, &write, 0, nullptr);
    }

    /* Get UBO alignment requirement for CB sub-allocation */
    VkPhysicalDeviceProperties dev_props;
    vkGetPhysicalDeviceProperties(qvk.physical_device, &dev_props);
    VkDeviceSize min_align = dev_props.limits.minUniformBufferOffsetAlignment;
    if (min_align == 0) min_align = 1;

    /* Map constant buffer for per-dispatch data uploads */
    uint8_t *cb_mapped = nullptr;
    VkResult map_res = vkMapMemory(qvk.device, s_constant_buffers[frame_idx].memory,
                                   0, VK_WHOLE_SIZE, 0, (void **)&cb_mapped);
    if (map_res != VK_SUCCESS) {
        Com_EPrintf("[NRD] Failed to map constant buffer: %d\n", map_res);
        return map_res;
    }

    VkDeviceSize cb_offset = 0;

    for (uint32_t d = 0; d < dispatch_count; d++) {
        const nrd::DispatchDesc &dd = dispatch_descs[d];

        /* --- Constant buffer sub-allocation --- */
        cb_offset = (cb_offset + min_align - 1) & ~(min_align - 1);

        if (dd.constantBufferDataSize > 0 && dd.constantBufferData &&
            !dd.constantBufferDataMatchesPreviousDispatch) {
            memcpy(cb_mapped + cb_offset, dd.constantBufferData,
                   dd.constantBufferDataSize);
        }

        /* --- Allocate set1 (per-dispatch resource descriptors) --- */
        VkDescriptorSet set1 = VK_NULL_HANDLE;
        {
            VkDescriptorSetAllocateInfo alloc_info = {};
            alloc_info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            alloc_info.descriptorPool     = s_desc_pool;
            alloc_info.descriptorSetCount = 1;
            alloc_info.pSetLayouts        = &s_desc_set_layout_resources;

            VkResult alloc_res = vkAllocateDescriptorSets(qvk.device, &alloc_info, &set1);
            if (alloc_res != VK_SUCCESS) {
                Com_EPrintf("[NRD] Failed to allocate set1 for dispatch %u (%s): %d\n",
                            d, dd.name ? dd.name : "?", alloc_res);
                vkUnmapMemory(qvk.device, s_constant_buffers[frame_idx].memory);
                return alloc_res;
            }
        }

        /* --- Write resource descriptors --- */
        /* Resources are concatenated in resourceRanges order:
         * [TEXTURE range descriptors...] [STORAGE_TEXTURE range descriptors...] */
        const nrd::PipelineDesc &pd = inst->pipelines[dd.pipelineIndex];
        uint32_t resource_idx = 0;

        for (uint32_t r = 0; r < pd.resourceRangesNum; r++) {
            const nrd::ResourceRangeDesc &range = pd.resourceRanges[r];

            for (uint32_t j = 0; j < range.descriptorsNum; j++) {
                if (resource_idx >= dd.resourcesNum) break;
                const nrd::ResourceDesc &rd = dd.resources[resource_idx];

                VkImageView view = resolve_nrd_image_view(rd.type, rd.indexInPool,
                                                          rd.descriptorType);
                if (view == VK_NULL_HANDLE) {
                    resource_idx++;
                    continue;
                }

                VkDescriptorImageInfo img_info = {};
                img_info.imageView   = view;
                img_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

                VkWriteDescriptorSet write = {};
                write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet          = set1;
                write.descriptorCount = 1;
                write.pImageInfo      = &img_info;

                if (range.descriptorType == nrd::DescriptorType::TEXTURE) {
                    write.dstBinding     = lib->spirvBindingOffsets.textureOffset + j;
                    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                } else {
                    write.dstBinding     = lib->spirvBindingOffsets.storageTextureAndBufferOffset + j;
                    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                }

                vkUpdateDescriptorSets(qvk.device, 1, &write, 0, nullptr);
                resource_idx++;
            }
        }

        /* --- Bind pipeline + descriptors and dispatch --- */
        vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
                          s_pipelines[dd.pipelineIndex]);

        uint32_t dynamic_offset = (uint32_t)cb_offset;
        VkDescriptorSet sets[] = { set0, set1 };
        vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
                                s_pipeline_layout, 0, 2, sets,
                                1, &dynamic_offset);

        vkCmdDispatch(cmd_buf, dd.gridWidth, dd.gridHeight, 1);

        /* Advance CB offset past this dispatch's data */
        if (dd.constantBufferDataSize > 0)
            cb_offset += dd.constantBufferDataSize;

        /* Full pipeline barrier between dispatches — conservative but correct.
         * NRD passes may read output of previous pass as input texture.       */
        VkMemoryBarrier mem_barrier = {};
        mem_barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mem_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        mem_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd_buf,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 1, &mem_barrier, 0, nullptr, 0, nullptr);
    }

    vkUnmapMemory(qvk.device, s_constant_buffers[frame_idx].memory);

    return VK_SUCCESS;
}

/* ------------------------------------------------------------------ */
/*  Phase 4: Composite dispatch (NRD outputs → ASVGF_COLOR)            */
/* ------------------------------------------------------------------ */

extern "C" VkResult vkpt_nrd_composite(VkCommandBuffer cmd_buf)
{
    if (!s_initialized || !s_comp_pipeline || !s_comp_desc_set)
        return VK_SUCCESS;

    /* Bind composite pipeline */
    vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, s_comp_pipeline);

    /* Bind descriptor sets: set0=UBO, set1=textures, set2=NRD outputs */
    VkDescriptorSet desc_sets[3];
    desc_sets[0] = qvk.desc_set_ubo;
    desc_sets[1] = qvk_get_current_desc_set_textures();
    desc_sets[2] = s_comp_desc_set;

    vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
        s_comp_pipeline_layout, 0, 3, desc_sets, 0, nullptr);

    /* Dispatch: 16x16 workgroups over the render resolution */
    uint32_t gx = (qvk.gpu_slice_width + 15) / 16;
    uint32_t gy = (qvk.extent_render.height + 15) / 16;
    vkCmdDispatch(cmd_buf, gx, gy, 1);

    /* Barrier: composite wrote IMG_ASVGF_COLOR, next pass (interleave) reads it */
    VkMemoryBarrier mem_barrier = {};
    mem_barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mem_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mem_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd_buf,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &mem_barrier, 0, nullptr, 0, nullptr);

    return VK_SUCCESS;
}
