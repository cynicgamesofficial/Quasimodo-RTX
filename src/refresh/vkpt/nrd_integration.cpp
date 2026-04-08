/*
 * NRD (NVIDIA Real-Time Denoisers) integration wrapper for Quasimodo-RTX.
 *
 * Phase 0: scaffolding — create/destroy NRD instance, verify library
 * linkage, log version and capability info.  No GPU resources yet.
 */

#include "NRD.h"
#include "nrd_integration.h"

#include <stdio.h>

/* ------------------------------------------------------------------ */
/*  Module state                                                       */
/* ------------------------------------------------------------------ */

static nrd::Instance *s_nrd_instance = nullptr;
static bool           s_initialized  = false;

/* ------------------------------------------------------------------ */
/*  Public API (C linkage)                                             */
/* ------------------------------------------------------------------ */

extern "C" qboolean vkpt_nrd_init(uint16_t render_width, uint16_t render_height)
{
    if (s_initialized) {
        return qtrue;
    }

    /* Log library info */
    const nrd::LibraryDesc *lib = nrd::GetLibraryDesc();
    if (!lib) {
        fprintf(stderr, "[NRD] GetLibraryDesc() returned NULL\n");
        return qfalse;
    }

    printf("[NRD] Library v%u.%u.%u  normal_enc=%u  roughness_enc=%u  denoisers=%u\n",
           lib->versionMajor, lib->versionMinor, lib->versionBuild,
           (unsigned)lib->normalEncoding, (unsigned)lib->roughnessEncoding,
           lib->supportedDenoisersNum);

    printf("[NRD] SPIRV binding offsets: sampler=%u texture=%u cb=%u storage=%u\n",
           lib->spirvBindingOffsets.samplerOffset,
           lib->spirvBindingOffsets.textureOffset,
           lib->spirvBindingOffsets.constantBufferOffset,
           lib->spirvBindingOffsets.storageTextureAndBufferOffset);

    /* Request RELAX_DIFFUSE_SPECULAR denoiser */
    nrd::DenoiserDesc denoisers[1] = {};
    denoisers[0].denoiser   = nrd::Denoiser::RELAX_DIFFUSE_SPECULAR;
    denoisers[0].identifier = 0;

    nrd::InstanceCreationDesc desc = {};
    desc.denoisers    = denoisers;
    desc.denoisersNum = 1;

    nrd::Result result = nrd::CreateInstance(desc, s_nrd_instance);
    if (result != nrd::Result::SUCCESS) {
        fprintf(stderr, "[NRD] CreateInstance failed (result=%u)\n", (unsigned)result);
        return qfalse;
    }

    /* Query instance descriptor to verify it's valid */
    const nrd::InstanceDesc *inst = nrd::GetInstanceDesc(*s_nrd_instance);
    if (inst) {
        printf("[NRD] Instance created: %u pipelines, %u permanent textures, %u transient textures\n",
               inst->pipelinesNum, inst->permanentPoolSize, inst->transientPoolSize);
        printf("[NRD] Constant buffer max size: %u bytes\n", inst->constantBufferMaxDataSize);
    }

    s_initialized = true;
    printf("[NRD] Phase 0 scaffolding ready (render %ux%u)\n", render_width, render_height);

    (void)render_width;
    (void)render_height;

    return qtrue;
}

extern "C" void vkpt_nrd_destroy(void)
{
    if (s_nrd_instance) {
        nrd::DestroyInstance(*s_nrd_instance);
        s_nrd_instance = nullptr;
        printf("[NRD] Instance destroyed\n");
    }
    s_initialized = false;
}

extern "C" qboolean vkpt_nrd_is_initialized(void)
{
    return s_initialized ? qtrue : qfalse;
}
