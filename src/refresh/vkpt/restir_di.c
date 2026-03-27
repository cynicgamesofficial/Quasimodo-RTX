/*
Copyright (C) 2026 Quasimodo-RTX contributors.

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

enum {
	RESTIR_DI_INITIAL,
	RESTIR_DI_TEMPORAL,
	RESTIR_DI_NUM_PIPELINES
};

static VkPipeline       pipeline_restir_di[RESTIR_DI_NUM_PIPELINES];
static VkPipelineLayout pipeline_layout_restir_di;

#define GROUP_SIZE 16

VkResult
vkpt_restir_di_initialize(void)
{
	VkDescriptorSetLayout desc_set_layouts[] = {
		qvk.desc_set_layout_ubo,
		qvk.desc_set_layout_textures,
		qvk.desc_set_layout_vertex_buffer
	};

	CREATE_PIPELINE_LAYOUT(qvk.device, &pipeline_layout_restir_di,
		.setLayoutCount = LENGTH(desc_set_layouts),
		.pSetLayouts    = desc_set_layouts,
	);
	ATTACH_LABEL_VARIABLE(pipeline_layout_restir_di, PIPELINE_LAYOUT);

	return VK_SUCCESS;
}

VkResult
vkpt_restir_di_destroy(void)
{
	vkDestroyPipelineLayout(qvk.device, pipeline_layout_restir_di, NULL);
	return VK_SUCCESS;
}

VkResult
vkpt_restir_di_create_pipelines(void)
{
	VkComputePipelineCreateInfo pipeline_info[RESTIR_DI_NUM_PIPELINES] = {
		[RESTIR_DI_INITIAL] = {
			.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage  = SHADER_STAGE(QVK_MOD_RESTIR_DI_INITIAL_COMP, VK_SHADER_STAGE_COMPUTE_BIT),
			.layout = pipeline_layout_restir_di,
		},
		[RESTIR_DI_TEMPORAL] = {
			.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage  = SHADER_STAGE(QVK_MOD_RESTIR_DI_TEMPORAL_COMP, VK_SHADER_STAGE_COMPUTE_BIT),
			.layout = pipeline_layout_restir_di,
		},
	};

	_VK(vkCreateComputePipelines(qvk.device, 0, LENGTH(pipeline_info), pipeline_info, 0, pipeline_restir_di));

	return VK_SUCCESS;
}

VkResult
vkpt_restir_di_destroy_pipelines(void)
{
	for (int i = 0; i < RESTIR_DI_NUM_PIPELINES; i++)
		vkDestroyPipeline(qvk.device, pipeline_restir_di[i], NULL);
	return VK_SUCCESS;
}

#define BARRIER_COMPUTE(cmd_buf, img) \
	do { \
		VkImageSubresourceRange subresource_range = { \
			.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT, \
			.baseMipLevel   = 0, \
			.levelCount     = 1, \
			.baseArrayLayer = 0, \
			.layerCount     = 1 \
		}; \
		IMAGE_BARRIER(cmd_buf, \
				.image            = img, \
				.subresourceRange = subresource_range, \
				.srcAccessMask    = VK_ACCESS_SHADER_WRITE_BIT, \
				.dstAccessMask    = VK_ACCESS_SHADER_READ_BIT, \
				.oldLayout        = VK_IMAGE_LAYOUT_GENERAL, \
				.newLayout        = VK_IMAGE_LAYOUT_GENERAL, \
		); \
	} while(0)

VkResult
vkpt_restir_di_dispatch(VkCommandBuffer cmd_buf)
{
	VkDescriptorSet desc_sets[] = {
		qvk.desc_set_ubo,
		qvk_get_current_desc_set_textures(),
		qvk.desc_set_vertex_buffer
	};

	uint32_t wg_x = (qvk.gpu_slice_width + GROUP_SIZE - 1) / GROUP_SIZE;
	uint32_t wg_y = (qvk.extent_render.height + GROUP_SIZE - 1) / GROUP_SIZE;

	// ---- Initial candidate generation ---- //
	BEGIN_PERF_MARKER(cmd_buf, PROFILER_RESTIR_DI_INITIAL);
	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_restir_di[RESTIR_DI_INITIAL]);
	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout_restir_di, 0, LENGTH(desc_sets), desc_sets, 0, 0);
	vkCmdDispatch(cmd_buf, wg_x, wg_y, 1);

	int current_reservoir = VKPT_IMG_RESTIR_RESERVOIR_A + (qvk.frame_counter & 1);
	BARRIER_COMPUTE(cmd_buf, qvk.images[current_reservoir]);
	int current_sample_pos = VKPT_IMG_RESTIR_SAMPLE_POS_A + (qvk.frame_counter & 1);
	BARRIER_COMPUTE(cmd_buf, qvk.images[current_sample_pos]);
	END_PERF_MARKER(cmd_buf, PROFILER_RESTIR_DI_INITIAL);

	// ---- Temporal reuse ---- //
	BEGIN_PERF_MARKER(cmd_buf, PROFILER_RESTIR_DI_TEMPORAL);
	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_restir_di[RESTIR_DI_TEMPORAL]);
	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout_restir_di, 0, LENGTH(desc_sets), desc_sets, 0, 0);
	vkCmdDispatch(cmd_buf, wg_x, wg_y, 1);

	BARRIER_COMPUTE(cmd_buf, qvk.images[current_reservoir]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[current_sample_pos]);
	END_PERF_MARKER(cmd_buf, PROFILER_RESTIR_DI_TEMPORAL);

	return VK_SUCCESS;
}
