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

#ifdef _WIN32
#include <vulkan/vulkan.h>
#endif

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
    ReflexMode mode;
    uint64_t  frameCounter;
} streamline_reflex_state_t;

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

void     SLReflex_BeginFrame(void);
void     SLReflex_Sleep(void);
void     SLReflex_SetMode(ReflexMode mode, uint32_t frameLimitUs);

void     SLReflex_Marker_SimStart(uint64_t frameId);
void     SLReflex_Marker_SimEnd(uint64_t frameId);
void     SLReflex_Marker_RenderStart(uint64_t frameId);
void     SLReflex_Marker_RenderEnd(uint64_t frameId);
void     SLReflex_Marker_PresentStart(uint64_t frameId);
void     SLReflex_Marker_PresentEnd(uint64_t frameId);

void     SLReflex_Marker_InputSample(uint64_t frameId);
void     SLReflex_Marker_TriggerFlash(uint64_t frameId);

qboolean SLReflex_IsLowLatencyAvailable(void);

/*
 * Vulkan proxy function pointers resolved from sl.interposer.dll.
 * When non-NULL the engine MUST use these instead of the loader-resolved
 * versions for the calls listed in sl_hooks.h — otherwise presentCommon()
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
