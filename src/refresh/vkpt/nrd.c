/*
Copyright (C) 2018 Christoph Schied
Copyright (C) 2019, NVIDIA CORPORATION. All rights reserved.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "vkpt.h"

enum
{
	NRD_PREP_PIPELINE,
	NRD_MERGE_PIPELINE,
	NRD_NUM_PIPELINES
};

static VkPipeline pipeline_nrd[NRD_NUM_PIPELINES];
static VkPipelineLayout pipeline_layout_nrd;

VkResult vkpt_nrd_backend_initialize(void);
void vkpt_nrd_backend_destroy(void);
VkResult vkpt_nrd_backend_denoise(VkCommandBuffer cmd_buf, bool reset_history);

#define BARRIER_COMPUTE(cmd_buf, img) \
	do { \
		VkImageSubresourceRange subresource_range = { \
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, \
			.baseMipLevel = 0, \
			.levelCount = 1, \
			.baseArrayLayer = 0, \
			.layerCount = 1 \
		}; \
		IMAGE_BARRIER(cmd_buf, \
			.image = img, \
			.subresourceRange = subresource_range, \
			.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, \
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT, \
			.oldLayout = VK_IMAGE_LAYOUT_GENERAL, \
			.newLayout = VK_IMAGE_LAYOUT_GENERAL); \
	} while (0)

VkResult
vkpt_nrd_initialize(void)
{
	VkDescriptorSetLayout desc_set_layouts[] = {
		qvk.desc_set_layout_ubo,
		qvk.desc_set_layout_textures,
	};

	CREATE_PIPELINE_LAYOUT(qvk.device, &pipeline_layout_nrd,
		.setLayoutCount = LENGTH(desc_set_layouts),
		.pSetLayouts = desc_set_layouts);
	ATTACH_LABEL_VARIABLE(pipeline_layout_nrd, PIPELINE_LAYOUT);

	return vkpt_nrd_backend_initialize();
}

VkResult
vkpt_nrd_destroy(void)
{
	vkpt_nrd_backend_destroy();
	vkDestroyPipelineLayout(qvk.device, pipeline_layout_nrd, NULL);
	pipeline_layout_nrd = VK_NULL_HANDLE;

	return VK_SUCCESS;
}

VkResult
vkpt_nrd_create_pipelines(void)
{
	if (qvk.shader_modules[QVK_MOD_NRD_PREPARE_COMP] == VK_NULL_HANDLE)
	{
		Com_EPrintf("NRD: missing shader module for nrd_prepare.comp\n");
		return VK_ERROR_INITIALIZATION_FAILED;
	}
	if (qvk.shader_modules[QVK_MOD_NRD_MERGE_COMP] == VK_NULL_HANDLE)
	{
		Com_EPrintf("NRD: missing shader module for nrd_merge.comp\n");
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	VkComputePipelineCreateInfo pipeline_info[NRD_NUM_PIPELINES] = {
		[NRD_PREP_PIPELINE] = {
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = SHADER_STAGE(QVK_MOD_NRD_PREPARE_COMP, VK_SHADER_STAGE_COMPUTE_BIT),
			.layout = pipeline_layout_nrd,
		},
		[NRD_MERGE_PIPELINE] = {
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = SHADER_STAGE(QVK_MOD_NRD_MERGE_COMP, VK_SHADER_STAGE_COMPUTE_BIT),
			.layout = pipeline_layout_nrd,
		},
	};

	_VK(vkCreateComputePipelines(qvk.device, 0, LENGTH(pipeline_info), pipeline_info, 0, pipeline_nrd));

	return VK_SUCCESS;
}

VkResult
vkpt_nrd_destroy_pipelines(void)
{
	for (int i = 0; i < NRD_NUM_PIPELINES; i++)
		vkDestroyPipeline(qvk.device, pipeline_nrd[i], NULL);

	return VK_SUCCESS;
}

VkResult
vkpt_nrd_prepare_inputs(VkCommandBuffer cmd_buf)
{
	VkDescriptorSet desc_sets[] = {
		qvk.desc_set_ubo,
		qvk_get_current_desc_set_textures(),
	};

	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_VIEW_DEPTH_A]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_VIEW_DIRECTION]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_NORMAL_A]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_METALLIC_A]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_COLOR_HF]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_COLOR_SPEC]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_MOTION]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_SPEC_HIT_DIST_A]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_GRAD_HF_SPEC_PONG]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_ATROUS_PING_HF]);

	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_nrd[NRD_PREP_PIPELINE]);
	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout_nrd, 0, LENGTH(desc_sets), desc_sets, 0, 0);

	vkCmdDispatch(cmd_buf,
		(qvk.gpu_slice_width + 15) / 16,
		(qvk.extent_render.height + 15) / 16,
		1);

	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_NRD_NORMAL_ROUGHNESS]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_NRD_DIFF_RADIANCE_HITDIST]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_NRD_SPEC_RADIANCE_HITDIST]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_NRD_DIFF_CONFIDENCE]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_NRD_SPEC_CONFIDENCE]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_NRD_DISOCCLUSION_MIX]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_NRD_VIEWZ]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_NRD_MV]);

	return VK_SUCCESS;
}

VkResult
vkpt_nrd_filter(VkCommandBuffer cmd_buf, bool reset_history)
{
	VkDescriptorSet desc_sets[] = {
		qvk.desc_set_ubo,
		qvk_get_current_desc_set_textures(),
	};

	VkResult result = vkpt_nrd_backend_denoise(cmd_buf, reset_history);
	if (result != VK_SUCCESS)
		return result;

	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_ATROUS_PING_LF_SH]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_ATROUS_PING_LF_COCG]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_GRAD_LF_PONG]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_GRAD_HF_SPEC_PONG]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_NRD_OUT_DIFF_RADIANCE_HITDIST]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_NRD_OUT_SPEC_RADIANCE_HITDIST]);

	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_nrd[NRD_MERGE_PIPELINE]);
	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout_nrd, 0, LENGTH(desc_sets), desc_sets, 0, 0);

	vkCmdDispatch(cmd_buf,
		(qvk.gpu_slice_width + 15) / 16,
		(qvk.extent_render.height + 15) / 16,
		1);

	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_COLOR]);

	return VK_SUCCESS;
}
