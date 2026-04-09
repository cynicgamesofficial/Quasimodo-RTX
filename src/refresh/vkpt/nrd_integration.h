/*
 * NRD (NVIDIA Real-Time Denoisers) integration wrapper for Quasimodo-RTX.
 *
 * C-friendly interface that isolates NRD C++ headers from the rest of
 * the engine.  Uses the direct NRD API (no NRI dependency).
 *
 * Phase 0: scaffolding — create/destroy NRD instance.
 * Phase 1: Vulkan resource creation — pipelines, textures, samplers,
 *          descriptor sets, constant buffer.
 * Phase 2: Input packing — GBuffers → NRD input format.
 * Phase 3: NRD dispatch — SetCommonSettings → GetComputeDispatches → record.
 * Phase 4: Output routing — NRD denoised output → composite → ASVGF_COLOR.
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

/* Recreate resolution-dependent resources (pool textures, input images)
 * after a render resolution change.  Pipelines / samplers are NOT recreated. */
qboolean vkpt_nrd_resize(uint16_t render_width, uint16_t render_height);

/* Create / destroy the packing compute pipeline (call during shader reload). */
VkResult vkpt_nrd_create_pipelines(void);
VkResult vkpt_nrd_destroy_pipelines(void);

/* Record the input-packing compute dispatch into cmd_buf.
 * Reads engine GBuffers, writes NRD-formatted input images.
 * Must be called AFTER path tracing and BEFORE NRD dispatch. */
VkResult vkpt_nrd_pack_inputs(VkCommandBuffer cmd_buf);

/* Record NRD denoiser dispatches into cmd_buf.
 * Fills CommonSettings from engine UBO, sets RELAX denoiser defaults,
 * then records all compute dispatches returned by NRD.
 * Must be called AFTER vkpt_nrd_pack_inputs().
 * Set reset_history to true on teleport / scene change / first frame. */
VkResult vkpt_nrd_dispatch(VkCommandBuffer cmd_buf, qboolean reset_history);

/* Composite NRD denoised output with material properties.
 * Reads NRD denoised diffuse+specular, raw LF (indirect), and GBuffer
 * materials, writes the composited result to ASVGF_COLOR.
 * Must be called AFTER vkpt_nrd_dispatch(). */
VkResult vkpt_nrd_composite(VkCommandBuffer cmd_buf);

#ifdef __cplusplus
}
#endif

#endif /* NRD_INTEGRATION_H */
