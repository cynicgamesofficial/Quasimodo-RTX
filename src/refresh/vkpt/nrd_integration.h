/*
 * NRD (NVIDIA Real-Time Denoisers) integration wrapper for Quasimodo-RTX.
 *
 * C-friendly interface that isolates NRD C++ headers from the rest of
 * the engine.  Uses the direct NRD API (no NRI dependency).
 *
 * Phase 0: scaffolding — create/destroy NRD instance.
 * Phase 1: Vulkan resource creation — pipelines, textures, samplers,
 *          descriptor sets, constant buffer.
 */

#ifndef NRD_INTEGRATION_H
#define NRD_INTEGRATION_H

#include "shared/shared.h"
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise NRD: create instance with RELAX_DIFFUSE_SPECULAR and
 * allocate all Vulkan resources (pipelines, textures, samplers, descriptors).
 * Requires a valid Vulkan device in qvk.
 */
qboolean vkpt_nrd_init(uint16_t render_width, uint16_t render_height);

/* Tear down the NRD instance and free all Vulkan + CPU-side state. */
void     vkpt_nrd_destroy(void);

/* Returns qtrue when the NRD instance is live and ready for use. */
qboolean vkpt_nrd_is_initialized(void);

/* Recreate resolution-dependent resources (pool textures) after a
 * render resolution change.  Pipelines / samplers are NOT recreated. */
qboolean vkpt_nrd_resize(uint16_t render_width, uint16_t render_height);

#ifdef __cplusplus
}
#endif

#endif /* NRD_INTEGRATION_H */
