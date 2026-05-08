/*
 * RAII-backed storage for swapchain VkImageView handles (C-callable).
 */

#ifndef VKPT_SWAPCHAIN_VIEWS_H
#define VKPT_SWAPCHAIN_VIEWS_H

#include <vulkan/vulkan.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

VkResult vkpt_swapchain_views_create(
	VkDevice device,
	VkFormat format,
	uint32_t image_count,
	const VkImage *images);

void vkpt_swapchain_views_destroy(void);

/* Borrowed pointer into helper-owned storage; valid only between successful
 * create_swapchain() and destroy_swapchain(). Use vkpt_swapchain_views_data(). */
VkImageView *vkpt_swapchain_views_data(void);
uint32_t vkpt_swapchain_views_count(void);

#ifdef __cplusplus
}
#endif

#endif /* VKPT_SWAPCHAIN_VIEWS_H */
