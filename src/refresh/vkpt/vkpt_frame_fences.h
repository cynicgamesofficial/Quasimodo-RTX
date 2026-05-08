/*
 * Per-frame Vulkan fence create/destroy helpers (C-callable).
 */

#ifndef VKPT_FRAME_FENCES_H
#define VKPT_FRAME_FENCES_H

#include <vulkan/vulkan.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

VkResult vkpt_frame_fences_create(
	VkDevice device,
	VkFence *fences,
	uint32_t count);

void vkpt_frame_fences_destroy(
	VkDevice device,
	VkFence *fences,
	uint32_t count);

#ifdef __cplusplus
}
#endif

#endif /* VKPT_FRAME_FENCES_H */
