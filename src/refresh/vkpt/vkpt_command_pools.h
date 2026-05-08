/*
 * Command pool create/destroy helpers (C-callable).
 */

#ifndef VKPT_COMMAND_POOLS_H
#define VKPT_COMMAND_POOLS_H

#include <vulkan/vulkan.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

VkResult vkpt_command_pools_create(
	VkDevice device,
	uint32_t queue_family_graphics,
	uint32_t queue_family_transfer,
	VkCommandPool *pool_graphics,
	VkCommandPool *pool_transfer);

void vkpt_command_pools_destroy(
	VkDevice device,
	VkCommandPool *pool_graphics,
	VkCommandPool *pool_transfer);

#ifdef __cplusplus
}
#endif

#endif /* VKPT_COMMAND_POOLS_H */
