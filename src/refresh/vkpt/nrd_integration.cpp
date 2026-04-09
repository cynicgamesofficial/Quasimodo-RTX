/*
 * NRD (NVIDIA Real-Time Denoisers) integration for Quasimodo-RTX.
 *
 * Uses NRI (NVIDIA Rendering Interface) via nrd::Integration to handle
 * all Vulkan plumbing (pipelines, descriptors, barriers, constant buffers)
 * for the NRD RELAX_DIFFUSE_SPECULAR denoiser.
 */

#include <array>
#include <cstring>

#include <NRI.h>
#include <Extensions/NRIHelper.h>
#include <Extensions/NRIRayTracing.h>
#include <Extensions/NRIWrapperVK.h>
#include <NRD.h>
#include <NRDSettings.h>
#include <NRDIntegration.hpp>

extern "C" {
#include "vkpt.h"
}
#include "shader/constants.h"

#ifdef inline
#undef inline
#endif
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace
{
	constexpr nrd::Identifier kNrdDenoiserId = 1;
	constexpr nri::VKBindingOffsets kVkBindingOffsets = { 0, 20, 2, 3 };

	const char* nri_message_type_name(nri::Message message_type)
	{
		switch (message_type)
		{
		case nri::Message::INFO:
			return "INFO";
		case nri::Message::WARNING:
			return "WARNING";
		case nri::Message::ERROR:
			return "ERROR";
		default:
			return "UNKNOWN";
		}
	}

	void NRI_CALL nri_message_callback(nri::Message message_type, const char* file, uint32_t line, const char* message, void*)
	{
		Com_WPrintf("NRD/NRI %s: %s (%s:%u)\n", nri_message_type_name(message_type), message, file, line);
	}

	void NRI_CALL nri_abort_execution(void*)
	{
	}

	nrd::Integration g_nrd;
	bool g_nrd_available = false;
	bool g_nrd_ready = false;
	bool g_nrd_warned_unavailable = false;
	uint16_t g_nrd_width = 0;
	uint16_t g_nrd_height = 0;
	float g_prev_jitter[2] = { 0.0f, 0.0f };
	bool g_has_prev_jitter = false;

	VkFormat get_vkpt_image_format(QVK_IMAGES image)
	{
		switch (image)
		{
#define IMG_DO(_name, _binding, _vkformat, _glslformat, _w, _h) case VKPT_IMG_##_name: return VK_FORMAT_##_vkformat;
		LIST_IMAGES
		LIST_IMAGES_A_B
#undef IMG_DO
		default:
			return VK_FORMAT_UNDEFINED;
		}
	}

	nrd::Resource make_vk_resource(QVK_IMAGES image, const nri::AccessLayoutStage& state)
	{
		nrd::Resource resource = {};
		resource.vk.image = (VKNonDispatchableHandle)qvk.images[image];
		resource.vk.format = (VKEnum)get_vkpt_image_format(image);
		resource.state = state;
		resource.userArg = nullptr;
		return resource;
	}

	VkResult recreate_nrd_integration()
	{
		if (g_nrd_ready)
		{
			g_nrd.Destroy();
			g_nrd_ready = false;
		}

		const nrd::DenoiserDesc denoiser_descs[] = {
			{ kNrdDenoiserId, nrd::Denoiser::RELAX_DIFFUSE_SPECULAR }
		};

		nrd::InstanceCreationDesc instance_creation_desc = {};
		instance_creation_desc.denoisers = denoiser_descs;
		instance_creation_desc.denoisersNum = 1;

		nri::QueueFamilyVKDesc queue_family = {};
		queue_family.queueNum = 1;
		queue_family.queueType = nri::QueueType::GRAPHICS;
		queue_family.familyIndex = (uint32_t)qvk.queue_idx_graphics;

		nri::DeviceCreationVKDesc device_creation_desc = {};
		device_creation_desc.callbackInterface.MessageCallback = nri_message_callback;
		device_creation_desc.callbackInterface.AbortExecution = nri_abort_execution;
		device_creation_desc.vkBindingOffsets = kVkBindingOffsets;
		device_creation_desc.vkExtensions.instanceExtensions = qvk.enabled_instance_extensions;
		device_creation_desc.vkExtensions.instanceExtensionNum = qvk.num_enabled_instance_extensions;
		device_creation_desc.vkExtensions.deviceExtensions = qvk.enabled_device_extensions;
		device_creation_desc.vkExtensions.deviceExtensionNum = qvk.num_enabled_device_extensions;
		device_creation_desc.vkInstance = (VKHandle)qvk.instance;
		device_creation_desc.vkDevice = (VKHandle)qvk.device;
		device_creation_desc.vkPhysicalDevice = (VKHandle)qvk.physical_device;
		device_creation_desc.queueFamilies = &queue_family;
		device_creation_desc.queueFamilyNum = 1;
		device_creation_desc.minorVersion = 2;

		nrd::IntegrationCreationDesc integration_desc = {};
		strncpy(integration_desc.name, "QuasimodoNRD", sizeof(integration_desc.name) - 1);
		integration_desc.resourceWidth = (uint16_t)qvk.gpu_slice_width;
		integration_desc.resourceHeight = (uint16_t)qvk.extent_render.height;
		integration_desc.queuedFrameNum = MAX_FRAMES_IN_FLIGHT;
		integration_desc.autoWaitForIdle = true;
		integration_desc.enableWholeLifetimeDescriptorCaching = false;

		nrd::Result result = g_nrd.RecreateVK(integration_desc, instance_creation_desc, device_creation_desc);
		if (result != nrd::Result::SUCCESS)
		{
			Com_EPrintf("NRD: failed to create RELAX_DIFFUSE_SPECULAR instance\n");
			g_nrd_available = false;
			return VK_ERROR_INITIALIZATION_FAILED;
		}

		nrd::RelaxSettings relax_settings = {};
		relax_settings.enableAntiFirefly = true;
		relax_settings.minHitDistanceWeight = 0.03f;
		relax_settings.confidenceDrivenRelaxationMultiplier = 1.0f;
		relax_settings.confidenceDrivenLuminanceEdgeStoppingRelaxation = 0.5f;
		relax_settings.confidenceDrivenNormalEdgeStoppingRelaxation = 0.5f;

		result = g_nrd.SetDenoiserSettings(kNrdDenoiserId, &relax_settings);
		if (result != nrd::Result::SUCCESS)
		{
			Com_EPrintf("NRD: failed to configure RELAX settings\n");
			g_nrd.Destroy();
			return VK_ERROR_INITIALIZATION_FAILED;
		}

		g_nrd_width = integration_desc.resourceWidth;
		g_nrd_height = integration_desc.resourceHeight;
		g_nrd_ready = true;
		g_has_prev_jitter = false;

		return VK_SUCCESS;
	}
}

extern "C" VkResult vkpt_nrd_backend_initialize(void)
{
	g_nrd_available = (qvk.device_count == 1) && qvk.supports_nrd;
	g_nrd_ready = false;
	g_nrd_warned_unavailable = false;
	g_has_prev_jitter = false;

	if (!g_nrd_available)
	{
		if (qvk.device_count != 1)
			Com_WPrintf("NRD: device-group rendering is not supported by the current integration, falling back to ASVGF\n");
		else
			Com_WPrintf("NRD: required Vulkan dynamic rendering / synchronization2 / extended dynamic state support is unavailable, falling back to ASVGF\n");
	}

	return VK_SUCCESS;
}

extern "C" void vkpt_nrd_backend_destroy(void)
{
	if (g_nrd_ready)
	{
		g_nrd.Destroy();
		g_nrd_ready = false;
	}

	g_nrd_available = false;
	g_has_prev_jitter = false;
}

extern "C" VkResult vkpt_nrd_backend_denoise(VkCommandBuffer cmd_buf, bool reset_history)
{
	if (!g_nrd_available)
	{
		if (!g_nrd_warned_unavailable)
		{
			Com_WPrintf("NRD: alternative denoiser path unavailable on this device configuration, keeping ASVGF output\n");
			g_nrd_warned_unavailable = true;
		}
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}

	bool recreated = false;
	if (!g_nrd_ready || g_nrd_width != (uint16_t)qvk.gpu_slice_width || g_nrd_height != (uint16_t)qvk.extent_render.height)
	{
		VkResult recreate_result = recreate_nrd_integration();
		if (recreate_result != VK_SUCCESS)
			return recreate_result;

		recreated = true;
	}

	const QVKUniformBuffer_t* ubo = &vkpt_refdef.uniform_buffer;

	nrd::CommonSettings common_settings = {};
	memcpy(common_settings.viewToClipMatrix, ubo->P, sizeof(common_settings.viewToClipMatrix));
	memcpy(common_settings.viewToClipMatrixPrev, ubo->P_prev, sizeof(common_settings.viewToClipMatrixPrev));
	memcpy(common_settings.worldToViewMatrix, ubo->V, sizeof(common_settings.worldToViewMatrix));
	memcpy(common_settings.worldToViewMatrixPrev, ubo->V_prev, sizeof(common_settings.worldToViewMatrixPrev));
	common_settings.motionVectorScale[0] = 1.0f;
	common_settings.motionVectorScale[1] = 1.0f;
	common_settings.motionVectorScale[2] = 1.0f;
	common_settings.cameraJitter[0] = ubo->sub_pixel_jitter[0];
	common_settings.cameraJitter[1] = ubo->sub_pixel_jitter[1];
	if (g_has_prev_jitter && !reset_history && !recreated)
	{
		common_settings.cameraJitterPrev[0] = g_prev_jitter[0];
		common_settings.cameraJitterPrev[1] = g_prev_jitter[1];
	}
	else
	{
		common_settings.cameraJitterPrev[0] = common_settings.cameraJitter[0];
		common_settings.cameraJitterPrev[1] = common_settings.cameraJitter[1];
	}
	common_settings.resourceSize[0] = g_nrd_width;
	common_settings.resourceSize[1] = g_nrd_height;
	common_settings.resourceSizePrev[0] = g_nrd_width;
	common_settings.resourceSizePrev[1] = g_nrd_height;
	common_settings.rectSize[0] = g_nrd_width;
	common_settings.rectSize[1] = g_nrd_height;
	common_settings.rectSizePrev[0] = g_nrd_width;
	common_settings.rectSizePrev[1] = g_nrd_height;
	common_settings.viewZScale = 1.0f;
	common_settings.denoisingRange = PRIMARY_RAY_T_MAX * 2.0f;
	common_settings.disocclusionThreshold = 0.01f;
	common_settings.disocclusionThresholdAlternate = 0.08f;
	common_settings.frameIndex = qvk.frame_counter;
	common_settings.accumulationMode = (reset_history || recreated || !g_has_prev_jitter)
		? nrd::AccumulationMode::CLEAR_AND_RESTART
		: nrd::AccumulationMode::CONTINUE;
	common_settings.isMotionVectorInWorldSpace = false;
	common_settings.isHistoryConfidenceAvailable = true;
	common_settings.isDisocclusionThresholdMixAvailable = true;
	common_settings.enableValidation = false;

	g_nrd.NewFrame();

	if (g_nrd.SetCommonSettings(common_settings) != nrd::Result::SUCCESS)
	{
		Com_EPrintf("NRD: SetCommonSettings failed\n");
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	const nri::AccessLayoutStage initial_state = {
		nri::AccessBits::SHADER_RESOURCE,
		nri::Layout::GENERAL,
		nri::StageBits::COMPUTE_SHADER
	};

	nrd::ResourceSnapshot snapshot;
	snapshot.restoreInitialState = true;
	snapshot.SetResource(nrd::ResourceType::IN_MV, make_vk_resource(VKPT_IMG_PT_MOTION, initial_state));
	snapshot.SetResource(nrd::ResourceType::IN_NORMAL_ROUGHNESS, make_vk_resource(VKPT_IMG_NRD_NORMAL_ROUGHNESS, initial_state));
	snapshot.SetResource(nrd::ResourceType::IN_VIEWZ, make_vk_resource(VKPT_IMG_PT_VIEW_DEPTH_A, initial_state));
	snapshot.SetResource(nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST, make_vk_resource(VKPT_IMG_NRD_DIFF_RADIANCE_HITDIST, initial_state));
	snapshot.SetResource(nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST, make_vk_resource(VKPT_IMG_NRD_SPEC_RADIANCE_HITDIST, initial_state));
	snapshot.SetResource(nrd::ResourceType::IN_DIFF_CONFIDENCE, make_vk_resource(VKPT_IMG_NRD_DIFF_CONFIDENCE, initial_state));
	snapshot.SetResource(nrd::ResourceType::IN_SPEC_CONFIDENCE, make_vk_resource(VKPT_IMG_NRD_SPEC_CONFIDENCE, initial_state));
	snapshot.SetResource(nrd::ResourceType::IN_DISOCCLUSION_THRESHOLD_MIX, make_vk_resource(VKPT_IMG_NRD_DISOCCLUSION_MIX, initial_state));
	snapshot.SetResource(nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST, make_vk_resource(VKPT_IMG_NRD_OUT_DIFF_RADIANCE_HITDIST, initial_state));
	snapshot.SetResource(nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST, make_vk_resource(VKPT_IMG_NRD_OUT_SPEC_RADIANCE_HITDIST, initial_state));

	nri::CommandBufferVKDesc command_buffer_desc = {};
	command_buffer_desc.vkCommandBuffer = (VKHandle)cmd_buf;
	command_buffer_desc.queueType = nri::QueueType::GRAPHICS;

	const nrd::Identifier denoisers[] = { kNrdDenoiserId };
	g_nrd.DenoiseVK(denoisers, 1, command_buffer_desc, snapshot);

	g_prev_jitter[0] = common_settings.cameraJitter[0];
	g_prev_jitter[1] = common_settings.cameraJitter[1];
	g_has_prev_jitter = true;

	return VK_SUCCESS;
}
