/*
 * NRD (NVIDIA Real-Time Denoisers) integration wrapper for Quasimodo-RTX.
 *
 * C-friendly interface.  The NRI-based C++ backend lives in
 * nrd_integration.cpp; the C dispatch wrapper (pipeline creation, input
 * preparation, merge) lives in nrd.c.
 */

#ifndef NRD_INTEGRATION_H
#define NRD_INTEGRATION_H

#include "shared/shared.h"
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Backend (nrd_integration.cpp) ---- */

/* Initialise the NRI-backed NRD denoiser.  Called from vkpt_nrd_initialize(). */
VkResult vkpt_nrd_backend_initialize(void);

/* Destroy the NRI-backed NRD denoiser.  Called from vkpt_nrd_destroy(). */
void     vkpt_nrd_backend_destroy(void);

/* Run RELAX_DIFFUSE_SPECULAR denoise via NRI.
 * Reads IMG_NRD_* inputs, writes IMG_NRD_OUT_* outputs.
 * Called from vkpt_nrd_filter() in nrd.c.  */
VkResult vkpt_nrd_backend_denoise(VkCommandBuffer cmd_buf, bool reset_history);

/* ---- C dispatch wrapper (nrd.c) ---- */

/* Create pipeline layout + call backend init.  Init-table "nrd" entry. */
VkResult vkpt_nrd_initialize(void);

/* Destroy backend + pipeline layout.  Init-table "nrd" entry. */
VkResult vkpt_nrd_destroy(void);

/* Create compute pipelines from shader modules.  Init-table "nrd|" entry. */
VkResult vkpt_nrd_create_pipelines(void);

/* Destroy compute pipelines.  Init-table "nrd|" entry. */
VkResult vkpt_nrd_destroy_pipelines(void);

/* Record input-packing dispatch (nrd_prepare.comp).
 * Reads engine GBuffers, writes NRD-formatted input images. */
VkResult vkpt_nrd_prepare_inputs(VkCommandBuffer cmd_buf);

/* Run NRI denoise + merge dispatch (nrd_merge.comp).
 * Composites ASVGF-denoised LF + NRD-denoised HF+spec → ASVGF_COLOR. */
VkResult vkpt_nrd_filter(VkCommandBuffer cmd_buf, bool reset_history);

#ifdef __cplusplus
}
#endif

#endif /* NRD_INTEGRATION_H */
