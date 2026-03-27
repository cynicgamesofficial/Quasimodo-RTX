/*
 * Streamline Reflex wrapper for Q2RTX.
 *
 * C-friendly interface that isolates Streamline C++ headers from the
 * rest of the engine.  The implementation dynamically loads
 * sl.interposer.dll at runtime so the game still starts when the DLL
 * is absent.
 *
 * Init is split into two phases so that slInit runs BEFORE Vulkan
 * device creation and slSetVulkanInfo runs AFTER device creation but
 * BEFORE the first swapchain is created (manual hooking requirement).
 */

#ifndef STREAMLINE_REFLEX_H
#define STREAMLINE_REFLEX_H

#include "shared/shared.h"
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    REFLEX_MODE_OFF       = 0,
    REFLEX_MODE_ON        = 1,
    REFLEX_MODE_ON_BOOST  = 2,
} ReflexMode;

typedef struct {
    qboolean  initialized;
    qboolean  supported;
    qboolean  lowLatencyAvailable;
    qboolean  latencyReportAvailable;
    uint32_t  statsWindowMessage;
    ReflexMode mode;
    qboolean  mode_applied;
    uint64_t  frameCounter;
} streamline_reflex_state_t;

typedef enum {
    SL_DLSS_QUALITY_QUALITY = 0,
    SL_DLSS_QUALITY_BALANCED = 1,
    SL_DLSS_QUALITY_PERFORMANCE = 2,
    SL_DLSS_QUALITY_ULTRA_PERFORMANCE = 3,
    SL_DLSS_QUALITY_DLAA = 4,
} streamline_dlss_quality_t;

typedef struct {
    qboolean available;
    uint32_t num_instance_extensions;
    const char **instance_extensions;
    uint32_t num_device_extensions;
    const char **device_extensions;
    uint32_t num_features12;
    const char **features12;
    uint32_t num_features13;
    const char **features13;
    uint32_t num_graphics_queues_required;
    uint32_t num_compute_queues_required;
} streamline_vk_requirements_t;

typedef struct {
    VkCommandBuffer cmd_buf;

    VkImage color_input;
    VkImageView color_input_view;
    VkFormat color_input_format;
    uint32_t color_input_width;
    uint32_t color_input_height;

    VkImage color_output;
    VkImageView color_output_view;
    VkFormat color_output_format;
    uint32_t color_output_width;
    uint32_t color_output_height;

    VkImage depth;
    VkImageView depth_view;
    VkFormat depth_format;
    uint32_t depth_width;
    uint32_t depth_height;

    VkImage motion_vectors;
    VkImageView motion_vectors_view;
    VkFormat motion_vectors_format;
    uint32_t motion_vectors_width;
    uint32_t motion_vectors_height;

    float camera_view_to_clip[16];
    float clip_to_camera_view[16];
    float clip_to_prev_clip[16];
    float prev_clip_to_clip[16];

    float camera_pos[3];
    float camera_up[3];
    float camera_right[3];
    float camera_fwd[3];

    float jitter_offset_x;
    float jitter_offset_y;
    float mvec_scale_x;
    float mvec_scale_y;
    float camera_near;
    float camera_far;
    float camera_fov;
    float camera_aspect_ratio;

    streamline_dlss_quality_t quality;
    qboolean reset;
    qboolean hdr;
    qboolean use_auto_exposure;
} streamline_dlss_evaluate_params_t;

extern streamline_reflex_state_t sl_reflex;
extern uint64_t reflex_frame_id;

/*
 * Phase 1: load sl.interposer.dll and call slInit.
 * MUST be called BEFORE vkCreateDevice / vkCreateSwapchain.
 * Returns qtrue if Streamline loaded and slInit succeeded.
 */
qboolean SLReflex_PreInit(void);

/*
 * Phase 2: call slSetVulkanInfo, resolve feature functions, resolve
 * Vulkan proxy entry points from sl.interposer.dll.
 * MUST be called AFTER vkCreateDevice but BEFORE vkCreateSwapchainKHR.
 */
qboolean SLReflex_PostInit(VkInstance instance, VkPhysicalDevice physicalDevice,
                            VkDevice device, uint32_t graphicsQueueFamily,
                            uint32_t graphicsQueueIndex);

void     SLReflex_Shutdown(void);

void     SLReflex_Sleep(void);
void     SLReflex_SetMode(ReflexMode mode, uint32_t frameLimitUs);

void     SLReflex_Marker_SimStart(void);
void     SLReflex_Marker_SimEnd(void);
void     SLReflex_Marker_RenderStart(void);
void     SLReflex_Marker_RenderEnd(void);
void     SLReflex_Marker_PresentStart(void);
void     SLReflex_Marker_PresentEnd(void);

void     SLReflex_Marker_InputSample(void);
void     SLReflex_Marker_TriggerFlash(void);

qboolean SLReflex_IsLowLatencyAvailable(void);

qboolean SLDLSS_IsAvailable(void);
qboolean SLDLSS_GetVulkanRequirements(streamline_vk_requirements_t *requirements);
qboolean SLDLSS_GetOptimalSettings(streamline_dlss_quality_t quality, uint32_t output_width, uint32_t output_height,
                                   uint32_t *render_width, uint32_t *render_height);
qboolean SLDLSS_Evaluate(const streamline_dlss_evaluate_params_t *params);

/*
 * Vulkan proxy function pointers resolved from sl.interposer.dll.
 * When non-NULL the engine MUST use these instead of the loader-resolved
 * versions for the calls listed in sl_hooks.h -- otherwise presentCommon()
 * is never invoked and Streamline cannot function.
 *
 * When NULL (SL not loaded) the engine falls back to its normal Vulkan calls.
 *
 * NOTE: vkCreateWin32SurfaceKHR is omitted because SDL creates the
 * surface internally and we cannot intercept that call.
 */
#ifdef _WIN32
extern PFN_vkQueuePresentKHR          SL_vkQueuePresentKHR;
extern PFN_vkCreateSwapchainKHR       SL_vkCreateSwapchainKHR;
extern PFN_vkDestroySwapchainKHR      SL_vkDestroySwapchainKHR;
extern PFN_vkGetSwapchainImagesKHR    SL_vkGetSwapchainImagesKHR;
extern PFN_vkAcquireNextImageKHR      SL_vkAcquireNextImageKHR;
extern PFN_vkDeviceWaitIdle           SL_vkDeviceWaitIdle;
extern PFN_vkDestroySurfaceKHR        SL_vkDestroySurfaceKHR;
#endif

#ifdef __cplusplus
}
#endif

#endif /* STREAMLINE_REFLEX_H */
