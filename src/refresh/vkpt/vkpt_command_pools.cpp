/*
 * Command pool create/destroy helpers (C-callable).
 */

#include "vkpt_command_pools.h"

#include <cstring>

VkResult
vkpt_command_pools_create(
	VkDevice device,
	uint32_t queue_family_graphics,
	uint32_t queue_family_transfer,
	VkCommandPool *pool_graphics,
	VkCommandPool *pool_transfer)
{
	if (device == VK_NULL_HANDLE || !pool_graphics || !pool_transfer)
		return VK_ERROR_INITIALIZATION_FAILED;

	vkpt_command_pools_destroy(device, pool_graphics, pool_transfer);

	VkCommandPoolCreateInfo cmd_pool_create_info;
	memset(&cmd_pool_create_info, 0, sizeof(cmd_pool_create_info));
	cmd_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cmd_pool_create_info.queueFamilyIndex = queue_family_graphics;
	cmd_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

	VkResult res = vkCreateCommandPool(device, &cmd_pool_create_info, NULL, pool_graphics);
	if (res != VK_SUCCESS)
	{
		*pool_graphics = VK_NULL_HANDLE;
		*pool_transfer = VK_NULL_HANDLE;
		return res;
	}

	cmd_pool_create_info.queueFamilyIndex = queue_family_transfer;
	res = vkCreateCommandPool(device, &cmd_pool_create_info, NULL, pool_transfer);
	if (res != VK_SUCCESS)
	{
		vkDestroyCommandPool(device, *pool_graphics, NULL);
		*pool_graphics = VK_NULL_HANDLE;
		*pool_transfer = VK_NULL_HANDLE;
		return res;
	}

	return VK_SUCCESS;
}

void
vkpt_command_pools_destroy(
	VkDevice device,
	VkCommandPool *pool_graphics,
	VkCommandPool *pool_transfer)
{
	if (!pool_graphics && !pool_transfer)
		return;

	if (device == VK_NULL_HANDLE)
	{
		if (pool_graphics)
			*pool_graphics = VK_NULL_HANDLE;
		if (pool_transfer)
			*pool_transfer = VK_NULL_HANDLE;
		return;
	}

	if (pool_graphics && *pool_graphics != VK_NULL_HANDLE)
	{
		vkDestroyCommandPool(device, *pool_graphics, NULL);
		*pool_graphics = VK_NULL_HANDLE;
	}

	if (pool_transfer && *pool_transfer != VK_NULL_HANDLE)
	{
		vkDestroyCommandPool(device, *pool_transfer, NULL);
		*pool_transfer = VK_NULL_HANDLE;
	}
}
