/*
 * RAII-backed storage for swapchain VkImageView handles (C-callable).
 */

#include "vkpt_swapchain_views.h"

#include <algorithm>
#include <cstring>
#include <vector>

static VkDevice g_device = VK_NULL_HANDLE;
static std::vector<VkImageView> g_views;

VkResult
vkpt_swapchain_views_create(
	VkDevice device,
	VkFormat format,
	uint32_t image_count,
	const VkImage *images)
{
	vkpt_swapchain_views_destroy();

	if (!device || image_count == 0 || !images)
		return VK_ERROR_INITIALIZATION_FAILED;

	g_device = device;
	g_views.resize(image_count);
	std::fill(g_views.begin(), g_views.end(), VK_NULL_HANDLE);

	for (uint32_t i = 0; i < image_count; i++)
	{
		VkImageViewCreateInfo img_create_info;
		memset(&img_create_info, 0, sizeof(img_create_info));
		img_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		img_create_info.image = images[i];
		img_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		img_create_info.format = format;
#if 1
		img_create_info.components.r = VK_COMPONENT_SWIZZLE_R;
		img_create_info.components.g = VK_COMPONENT_SWIZZLE_G;
		img_create_info.components.b = VK_COMPONENT_SWIZZLE_B;
		img_create_info.components.a = VK_COMPONENT_SWIZZLE_A;
#endif
		img_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		img_create_info.subresourceRange.baseMipLevel = 0;
		img_create_info.subresourceRange.levelCount = 1;
		img_create_info.subresourceRange.baseArrayLayer = 0;
		img_create_info.subresourceRange.layerCount = 1;

		VkResult res = vkCreateImageView(device, &img_create_info, NULL, &g_views[i]);
		if (res != VK_SUCCESS)
		{
			vkpt_swapchain_views_destroy();
			return res;
		}
	}

	return VK_SUCCESS;
}

void
vkpt_swapchain_views_destroy(void)
{
	if (g_device != VK_NULL_HANDLE)
	{
		for (VkImageView v : g_views)
		{
			if (v != VK_NULL_HANDLE)
				vkDestroyImageView(g_device, v, NULL);
		}
	}

	g_views.clear();
	g_device = VK_NULL_HANDLE;
}

VkImageView *
vkpt_swapchain_views_data(void)
{
	return g_views.empty() ? NULL : g_views.data();
}

uint32_t
vkpt_swapchain_views_count(void)
{
	return (uint32_t)g_views.size();
}
