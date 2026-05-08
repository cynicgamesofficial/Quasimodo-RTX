/*
 * Per-frame Vulkan fence create/destroy helpers (C-callable).
 */

#include "vkpt_frame_fences.h"

#include <cstring>

VkResult
vkpt_frame_fences_create(
	VkDevice device,
	VkFence *fences,
	uint32_t count)
{
	if (!device || !fences || count == 0)
		return VK_ERROR_INITIALIZATION_FAILED;

	vkpt_frame_fences_destroy(device, fences, count);

	VkFenceCreateInfo fence_info;
	memset(&fence_info, 0, sizeof(fence_info));
	fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	for (uint32_t k = 0; k < count; k++)
	{
		VkResult res = vkCreateFence(device, &fence_info, NULL, &fences[k]);
		if (res != VK_SUCCESS)
		{
			for (uint32_t j = 0; j < k; j++)
			{
				if (fences[j] != VK_NULL_HANDLE)
				{
					vkDestroyFence(device, fences[j], NULL);
					fences[j] = VK_NULL_HANDLE;
				}
			}
			for (uint32_t j = k; j < count; j++)
				fences[j] = VK_NULL_HANDLE;
			return res;
		}
	}

	return VK_SUCCESS;
}

void
vkpt_frame_fences_destroy(
	VkDevice device,
	VkFence *fences,
	uint32_t count)
{
	if (!fences || count == 0)
		return;

	if (device != VK_NULL_HANDLE)
	{
		for (uint32_t i = 0; i < count; i++)
		{
			if (fences[i] != VK_NULL_HANDLE)
			{
				vkDestroyFence(device, fences[i], NULL);
				fences[i] = VK_NULL_HANDLE;
			}
		}
	}
	else
	{
		for (uint32_t i = 0; i < count; i++)
			fences[i] = VK_NULL_HANDLE;
	}
}
