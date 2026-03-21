/*
 * Streamline Reflex wrapper - dynamic-load implementation.
 *
 * Loads sl.interposer.dll at runtime so the game is still playable
 * when the DLL or the driver is absent.  All Streamline C++ types
 * stay confined to this translation unit.
 *
 * Init is split into PreInit (before Vulkan device) and PostInit
 * (after device, before swapchain) per NVIDIA manual hooking guide.
 * Vulkan proxy functions for all sl_hooks.h entries are resolved from
 * sl.interposer.dll so presentCommon() fires every frame.
 */

#include <vulkan/vulkan.h>

#include "sl.h"
#include "sl_reflex.h"
#include "sl_pcl.h"
#include "sl_dlss.h"
#include "sl_helpers_vk.h"
#include "sl_consts.h"

#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

extern "C" {
#include "streamline_reflex.h"
#include "common/common.h"
#include "common/cvar.h"
}

/* ------------------------------------------------------------------ */
/*  State visible to the rest of the engine                           */
/* ------------------------------------------------------------------ */

streamline_reflex_state_t sl_reflex;
uint64_t reflex_frame_id;

PFN_vkQueuePresentKHR       SL_vkQueuePresentKHR;
PFN_vkCreateSwapchainKHR    SL_vkCreateSwapchainKHR;
PFN_vkDestroySwapchainKHR   SL_vkDestroySwapchainKHR;
PFN_vkGetSwapchainImagesKHR SL_vkGetSwapchainImagesKHR;
PFN_vkAcquireNextImageKHR   SL_vkAcquireNextImageKHR;
PFN_vkDeviceWaitIdle        SL_vkDeviceWaitIdle;
PFN_vkDestroySurfaceKHR     SL_vkDestroySurfaceKHR;

/* ------------------------------------------------------------------ */
/*  Private state                                                     */
/* ------------------------------------------------------------------ */

static HMODULE             s_sl_module;
static sl::FrameToken     *s_frame_token;
static bool                s_preinit_done;
static wchar_t             s_plugin_path_storage[4][MAX_PATH];
static const wchar_t      *s_plugin_paths[4];
static uint32_t            s_num_plugin_paths;

static PFN_vkGetDeviceProcAddr   s_sl_vkGetDeviceProcAddr;
static PFN_vkGetInstanceProcAddr s_sl_vkGetInstanceProcAddr;

/* Dynamically-resolved SL core API */
using Fn_slInit                = sl::Result(const sl::Preferences&, uint64_t);
using Fn_slShutdown            = sl::Result();
using Fn_slSetVulkanInfo       = sl::Result(const sl::VulkanInfo&);
using Fn_slGetNewFrameToken    = sl::Result(sl::FrameToken*&, const uint32_t*);
using Fn_slGetFeatureFunction  = sl::Result(sl::Feature, const char*, void*&);
using Fn_slIsFeatureSupported  = sl::Result(sl::Feature, const sl::AdapterInfo&);
using Fn_slGetFeatureRequirements = sl::Result(sl::Feature, sl::FeatureRequirements&);
using Fn_slSetTagForFrame      = sl::Result(const sl::FrameToken&, const sl::ViewportHandle&, const sl::ResourceTag*, uint32_t, sl::CommandBuffer*);
using Fn_slSetConstants        = sl::Result(const sl::Constants&, const sl::FrameToken&, const sl::ViewportHandle&);
using Fn_slEvaluateFeature     = sl::Result(sl::Feature, const sl::FrameToken&, const sl::BaseStructure**, uint32_t, sl::CommandBuffer*);

static Fn_slInit               *p_slInit;
static Fn_slShutdown           *p_slShutdown;
static Fn_slSetVulkanInfo      *p_slSetVulkanInfo;
static Fn_slGetNewFrameToken   *p_slGetNewFrameToken;
static Fn_slGetFeatureFunction *p_slGetFeatureFunction;
static Fn_slIsFeatureSupported *p_slIsFeatureSupported;
static Fn_slGetFeatureRequirements *p_slGetFeatureRequirements;
static Fn_slSetTagForFrame     *p_slSetTagForFrame;
static Fn_slSetConstants       *p_slSetConstants;
static Fn_slEvaluateFeature    *p_slEvaluateFeature;

/* Dynamically-resolved feature functions */
static PFun_slReflexGetState     *p_slReflexGetState;
static PFun_slReflexSleep        *p_slReflexSleep;
static PFun_slReflexSetOptions   *p_slReflexSetOptions;
static PFun_slPCLSetMarker       *p_slPCLSetMarker;
static PFun_slDLSSGetOptimalSettings *p_slDLSSGetOptimalSettings;
static PFun_slDLSSSetOptions     *p_slDLSSSetOptions;

static bool                       s_dlss_available;
static bool                       s_dlss_requirements_valid;
static sl::FeatureRequirements    s_dlss_requirements;
static sl::ViewportHandle         s_dlss_viewport(0);

/* ------------------------------------------------------------------ */
/*  Helpers                                                           */
/* ------------------------------------------------------------------ */

static void sl_log_callback(sl::LogType type, const char *msg)
{
    switch (type) {
    case sl::LogType::eError: Com_EPrintf("[SL] %s\n", msg); break;
    case sl::LogType::eWarn:  Com_WPrintf("[SL] %s\n", msg); break;
    default:                  Com_DPrintf("[SL] %s\n", msg); break;
    }
}

static bool sl_path_exists(const wchar_t *path, bool want_directory)
{
    DWORD attrs = GetFileAttributesW(path);
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return false;
    if (want_directory)
        return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static void sl_log_loaded_module_path(void)
{
    if (!s_sl_module)
        return;

    wchar_t path_w[MAX_PATH];
    DWORD len = GetModuleFileNameW(s_sl_module, path_w, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return;

    char path_utf8[MAX_PATH * 3];
    int bytes = WideCharToMultiByte(CP_UTF8, 0, path_w, -1, path_utf8, (int)sizeof(path_utf8), nullptr, nullptr);
    if (bytes > 0)
        Com_Printf("Streamline: loaded interposer from '%s'.\n", path_utf8);
}

static bool sl_build_absolute_from_exe(const wchar_t *relative_suffix, wchar_t *out_path, size_t out_count)
{
    if (!relative_suffix || !out_path || out_count == 0)
        return false;

    wchar_t exe_path[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return false;

    wchar_t *last_slash = wcsrchr(exe_path, L'\\');
    if (!last_slash)
        return false;
    last_slash[1] = L'\0';

    wchar_t joined[MAX_PATH];
    if (_snwprintf_s(joined, _countof(joined), _TRUNCATE, L"%s%s", exe_path, relative_suffix) < 0)
        return false;

    DWORD full_len = GetFullPathNameW(joined, (DWORD)out_count, out_path, nullptr);
    return (full_len > 0 && full_len < out_count);
}

static void sl_build_plugin_search_paths(void)
{
    s_num_plugin_paths = 0;

    if (sl_build_absolute_from_exe(L"streamline\\bin\\x64", s_plugin_path_storage[s_num_plugin_paths], _countof(s_plugin_path_storage[0])) &&
        sl_path_exists(s_plugin_path_storage[s_num_plugin_paths], true)) {
        s_plugin_paths[s_num_plugin_paths] = s_plugin_path_storage[s_num_plugin_paths];
        s_num_plugin_paths++;
    }

    if (s_num_plugin_paths < _countof(s_plugin_paths) &&
        sl_build_absolute_from_exe(L"Third Parties\\NVIDIA\\bin\\x64\\development", s_plugin_path_storage[s_num_plugin_paths], _countof(s_plugin_path_storage[0])) &&
        sl_path_exists(s_plugin_path_storage[s_num_plugin_paths], true)) {
        s_plugin_paths[s_num_plugin_paths] = s_plugin_path_storage[s_num_plugin_paths];
        s_num_plugin_paths++;
    }

    if (s_num_plugin_paths < _countof(s_plugin_paths) &&
        sl_build_absolute_from_exe(L"Third Parties\\NVIDIA\\bin\\x64", s_plugin_path_storage[s_num_plugin_paths], _countof(s_plugin_path_storage[0])) &&
        sl_path_exists(s_plugin_path_storage[s_num_plugin_paths], true)) {
        s_plugin_paths[s_num_plugin_paths] = s_plugin_path_storage[s_num_plugin_paths];
        s_num_plugin_paths++;
    }
}

template <typename T>
static T *sl_get_proc(const char *name)
{
    return reinterpret_cast<T *>(GetProcAddress(s_sl_module, name));
}

static bool resolve_feature_functions(void)
{
    void *fn = nullptr;

    if (p_slGetFeatureFunction(sl::kFeatureReflex, "slReflexGetState", fn) == sl::Result::eOk)
        p_slReflexGetState = reinterpret_cast<PFun_slReflexGetState *>(fn);

    fn = nullptr;
    if (p_slGetFeatureFunction(sl::kFeatureReflex, "slReflexSleep", fn) == sl::Result::eOk)
        p_slReflexSleep = reinterpret_cast<PFun_slReflexSleep *>(fn);

    fn = nullptr;
    if (p_slGetFeatureFunction(sl::kFeatureReflex, "slReflexSetOptions", fn) == sl::Result::eOk)
        p_slReflexSetOptions = reinterpret_cast<PFun_slReflexSetOptions *>(fn);

    fn = nullptr;
    if (p_slGetFeatureFunction(sl::kFeaturePCL, "slPCLSetMarker", fn) == sl::Result::eOk)
        p_slPCLSetMarker = reinterpret_cast<PFun_slPCLSetMarker *>(fn);

    fn = nullptr;
    if (p_slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetOptimalSettings", fn) == sl::Result::eOk)
        p_slDLSSGetOptimalSettings = reinterpret_cast<PFun_slDLSSGetOptimalSettings *>(fn);

    fn = nullptr;
    if (p_slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSSetOptions", fn) == sl::Result::eOk)
        p_slDLSSSetOptions = reinterpret_cast<PFun_slDLSSSetOptions *>(fn);

    s_dlss_available = (p_slDLSSGetOptimalSettings != nullptr) && (p_slDLSSSetOptions != nullptr);

    return p_slReflexGetState && p_slReflexSleep && p_slReflexSetOptions && p_slPCLSetMarker;
}

static void resolve_vulkan_proxies(VkInstance instance, VkDevice device)
{
    if (!s_sl_vkGetDeviceProcAddr || !s_sl_vkGetInstanceProcAddr)
        return;

    SL_vkQueuePresentKHR       = reinterpret_cast<PFN_vkQueuePresentKHR>      (s_sl_vkGetDeviceProcAddr(device, "vkQueuePresentKHR"));
    SL_vkCreateSwapchainKHR    = reinterpret_cast<PFN_vkCreateSwapchainKHR>   (s_sl_vkGetDeviceProcAddr(device, "vkCreateSwapchainKHR"));
    SL_vkDestroySwapchainKHR   = reinterpret_cast<PFN_vkDestroySwapchainKHR>  (s_sl_vkGetDeviceProcAddr(device, "vkDestroySwapchainKHR"));
    SL_vkGetSwapchainImagesKHR = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(s_sl_vkGetDeviceProcAddr(device, "vkGetSwapchainImagesKHR"));
    SL_vkAcquireNextImageKHR   = reinterpret_cast<PFN_vkAcquireNextImageKHR>  (s_sl_vkGetDeviceProcAddr(device, "vkAcquireNextImageKHR"));
    SL_vkDeviceWaitIdle        = reinterpret_cast<PFN_vkDeviceWaitIdle>       (s_sl_vkGetDeviceProcAddr(device, "vkDeviceWaitIdle"));
    SL_vkDestroySurfaceKHR     = reinterpret_cast<PFN_vkDestroySurfaceKHR>    (s_sl_vkGetInstanceProcAddr(instance, "vkDestroySurfaceKHR"));
}

static void query_reflex_state(void)
{
    if (!p_slReflexGetState)
        return;

    sl::ReflexState state{};
    if (p_slReflexGetState(state) == sl::Result::eOk) {
        sl_reflex.lowLatencyAvailable = state.lowLatencyAvailable ? qtrue : qfalse;
    }
}

static void clear_all(void)
{
    p_slInit               = nullptr;
    p_slShutdown           = nullptr;
    p_slSetVulkanInfo      = nullptr;
    p_slGetNewFrameToken   = nullptr;
    p_slGetFeatureFunction = nullptr;
    p_slIsFeatureSupported = nullptr;
    p_slGetFeatureRequirements = nullptr;
    p_slSetTagForFrame     = nullptr;
    p_slSetConstants       = nullptr;
    p_slEvaluateFeature    = nullptr;
    p_slReflexGetState     = nullptr;
    p_slReflexSleep        = nullptr;
    p_slReflexSetOptions   = nullptr;
    p_slPCLSetMarker       = nullptr;
    p_slDLSSGetOptimalSettings = nullptr;
    p_slDLSSSetOptions     = nullptr;

    s_sl_vkGetDeviceProcAddr   = nullptr;
    s_sl_vkGetInstanceProcAddr = nullptr;
    SL_vkQueuePresentKHR       = nullptr;
    SL_vkCreateSwapchainKHR    = nullptr;
    SL_vkDestroySwapchainKHR   = nullptr;
    SL_vkGetSwapchainImagesKHR = nullptr;
    SL_vkAcquireNextImageKHR   = nullptr;
    SL_vkDeviceWaitIdle        = nullptr;
    SL_vkDestroySurfaceKHR     = nullptr;

    s_frame_token = nullptr;
    s_preinit_done = false;
    s_num_plugin_paths = 0;
    s_dlss_available = false;
    s_dlss_requirements_valid = false;
    memset(&s_dlss_requirements, 0, sizeof(s_dlss_requirements));
    memset(&sl_reflex, 0, sizeof(sl_reflex));
    reflex_frame_id = 0;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

extern "C" qboolean SLReflex_PreInit(void)
{
    memset(&sl_reflex, 0, sizeof(sl_reflex));
    reflex_frame_id = 0;
    s_preinit_done = false;
    s_num_plugin_paths = 0;
    s_dlss_available = false;
    s_dlss_requirements_valid = false;
    memset(&s_dlss_requirements, 0, sizeof(s_dlss_requirements));

    sl_build_plugin_search_paths();

    wchar_t local_interposer_path[MAX_PATH];
    wchar_t sdk_dev_interposer_path[MAX_PATH];
    wchar_t sdk_interposer_path[MAX_PATH];
    const wchar_t *dll_paths[5];
    int num_dll_paths = 0;

    dll_paths[num_dll_paths++] = L"sl.interposer.dll";

    if (sl_build_absolute_from_exe(L"streamline\\bin\\x64\\sl.interposer.dll", local_interposer_path, _countof(local_interposer_path)) &&
        sl_path_exists(local_interposer_path, false)) {
        dll_paths[num_dll_paths++] = local_interposer_path;
    }

    if (sl_build_absolute_from_exe(L"Third Parties\\NVIDIA\\bin\\x64\\development\\sl.interposer.dll", sdk_dev_interposer_path, _countof(sdk_dev_interposer_path)) &&
        sl_path_exists(sdk_dev_interposer_path, false)) {
        dll_paths[num_dll_paths++] = sdk_dev_interposer_path;
    }

    if (sl_build_absolute_from_exe(L"Third Parties\\NVIDIA\\bin\\x64\\sl.interposer.dll", sdk_interposer_path, _countof(sdk_interposer_path)) &&
        sl_path_exists(sdk_interposer_path, false)) {
        dll_paths[num_dll_paths++] = sdk_interposer_path;
    }

    dll_paths[num_dll_paths++] = L"streamline\\bin\\x64\\sl.interposer.dll";

    for (int i = 0; i < num_dll_paths && !s_sl_module; i++)
        s_sl_module = LoadLibraryW(dll_paths[i]);

    if (!s_sl_module) {
        Com_Printf("Streamline: sl.interposer.dll not found - Reflex disabled.\n");
        return qfalse;
    }
    sl_log_loaded_module_path();

    p_slInit               = sl_get_proc<Fn_slInit>("slInit");
    p_slShutdown           = sl_get_proc<Fn_slShutdown>("slShutdown");
    p_slSetVulkanInfo      = sl_get_proc<Fn_slSetVulkanInfo>("slSetVulkanInfo");
    p_slGetNewFrameToken   = sl_get_proc<Fn_slGetNewFrameToken>("slGetNewFrameToken");
    p_slGetFeatureFunction = sl_get_proc<Fn_slGetFeatureFunction>("slGetFeatureFunction");
    p_slIsFeatureSupported = sl_get_proc<Fn_slIsFeatureSupported>("slIsFeatureSupported");
    p_slGetFeatureRequirements = sl_get_proc<Fn_slGetFeatureRequirements>("slGetFeatureRequirements");
    p_slSetTagForFrame     = sl_get_proc<Fn_slSetTagForFrame>("slSetTagForFrame");
    p_slSetConstants       = sl_get_proc<Fn_slSetConstants>("slSetConstants");
    p_slEvaluateFeature    = sl_get_proc<Fn_slEvaluateFeature>("slEvaluateFeature");

    s_sl_vkGetDeviceProcAddr   = reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetProcAddress(s_sl_module, "vkGetDeviceProcAddr"));
    s_sl_vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(s_sl_module, "vkGetInstanceProcAddr"));

    if (!p_slInit || !p_slShutdown || !p_slSetVulkanInfo ||
        !p_slGetNewFrameToken || !p_slGetFeatureFunction ||
        !p_slSetTagForFrame || !p_slSetConstants || !p_slEvaluateFeature) {
        Com_EPrintf("Streamline: failed to resolve core API - Reflex disabled.\n");
        FreeLibrary(s_sl_module);
        s_sl_module = nullptr;
        return qfalse;
    }

    sl::Feature features[] = { sl::kFeatureReflex, sl::kFeaturePCL, sl::kFeatureDLSS, sl::kFeatureImGUI };

    sl::Preferences prefs{};
    prefs.showConsole       = true;
    prefs.logLevel          = sl::LogLevel::eDefault;
    prefs.logMessageCallback = sl_log_callback;
    prefs.pathsToPlugins    = s_num_plugin_paths ? s_plugin_paths : nullptr;
    prefs.numPathsToPlugins = s_num_plugin_paths;
    prefs.featuresToLoad    = features;
    prefs.numFeaturesToLoad = 4;
    prefs.engine            = sl::EngineType::eCustom;
    prefs.engineVersion     = "Q2RTX";
    prefs.renderAPI         = sl::RenderAPI::eVulkan;
    prefs.flags             = sl::PreferenceFlags::eDisableCLStateTracking
                            | sl::PreferenceFlags::eUseManualHooking
                            | sl::PreferenceFlags::eUseFrameBasedResourceTagging;

    const char *app_id_env = getenv("SL_APP_ID");
    if (app_id_env && app_id_env[0]) {
        char *end_ptr = nullptr;
        unsigned long parsed = strtoul(app_id_env, &end_ptr, 10);
        if (end_ptr != app_id_env && *end_ptr == '\0' && parsed <= 0xFFFFFFFFUL) {
            prefs.applicationId = (uint32_t)parsed;
            Com_Printf("Streamline DLSS: using applicationId from SL_APP_ID (%u).\n", prefs.applicationId);
        } else {
            Com_WPrintf("Streamline DLSS: invalid SL_APP_ID '%s', using engine/project identity.\n", app_id_env);
        }
    }

    const char *project_id_env = getenv("SL_PROJECT_ID");
    if (project_id_env && project_id_env[0]) {
        prefs.projectId = project_id_env;
        Com_Printf("Streamline DLSS: using projectId from SL_PROJECT_ID ('%s').\n", prefs.projectId);
    } else {
        Com_Printf("Streamline DLSS: no projectId configured; relying on appId/temporary NGX identity.\n");
    }

    sl::Result res = p_slInit(prefs, sl::kSDKVersion);
    if (res != sl::Result::eOk) {
        Com_EPrintf("Streamline: slInit failed (code %d) - Reflex disabled.\n", (int)res);
        FreeLibrary(s_sl_module);
        s_sl_module = nullptr;
        return qfalse;
    }

    s_preinit_done = true;
    Com_Printf("Streamline: slInit completed (manual hooking).\n");
    if (s_num_plugin_paths) {
        Com_Printf("Streamline: configured %u plugin search path(s) for feature loading.\n", s_num_plugin_paths);
    } else {
        Com_WPrintf("Streamline: no explicit plugin search paths configured; relying on DLL local directory.\n");
    }

    if (p_slGetFeatureRequirements) {
        sl::FeatureRequirements req{};
        sl::Result req_res = p_slGetFeatureRequirements(sl::kFeatureDLSS, req);
        if (req_res == sl::Result::eOk) {
            s_dlss_requirements = req;
            s_dlss_requirements_valid = true;
            Com_Printf("Streamline DLSS: requirements ready (instExt=%u devExt=%u vk12=%u vk13=%u gfxQ=%u compQ=%u).\n",
                       req.vkNumInstanceExtensions, req.vkNumDeviceExtensions, req.vkNumFeatures12, req.vkNumFeatures13,
                       req.vkNumGraphicsQueuesRequired, req.vkNumComputeQueuesRequired);
        } else {
            Com_WPrintf("Streamline DLSS: requirements query failed (code %d), DLSS may be unavailable.\n", (int)req_res);
        }
    } else {
        Com_WPrintf("Streamline DLSS: slGetFeatureRequirements not available.\n");
    }

    return qtrue;
}

extern "C" qboolean SLReflex_PostInit(VkInstance instance, VkPhysicalDevice physicalDevice,
                                       VkDevice device, uint32_t graphicsQueueFamily,
                                       uint32_t graphicsQueueIndex)
{
    if (!s_preinit_done) {
        Com_WPrintf("Streamline: PreInit was not called - skipping PostInit.\n");
        return qfalse;
    }

    sl::VulkanInfo vkInfo{};
    vkInfo.device              = device;
    vkInfo.instance            = instance;
    vkInfo.physicalDevice      = physicalDevice;
    vkInfo.graphicsQueueFamily = graphicsQueueFamily;
    vkInfo.graphicsQueueIndex  = graphicsQueueIndex;
    /*
     * Q2RTX uses the graphics queue family for compute as well (the family
     * supports both VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT).  Tell
     * Streamline so it can match present/compute queues correctly.
     */
    vkInfo.computeQueueFamily  = graphicsQueueFamily;
    vkInfo.computeQueueIndex   = graphicsQueueIndex;

    Com_Printf("Streamline: slSetVulkanInfo - graphicsFamily=%u graphicsIndex=%u "
               "computeFamily=%u computeIndex=%u\n",
               vkInfo.graphicsQueueFamily, vkInfo.graphicsQueueIndex,
               vkInfo.computeQueueFamily, vkInfo.computeQueueIndex);

    sl::Result res = p_slSetVulkanInfo(vkInfo);
    if (res != sl::Result::eOk) {
        Com_EPrintf("Streamline: slSetVulkanInfo failed (code %d) - Reflex disabled.\n", (int)res);
        p_slShutdown();
        FreeLibrary(s_sl_module);
        s_sl_module = nullptr;
        clear_all();
        return qfalse;
    }

    if (!resolve_feature_functions()) {
        Com_WPrintf("Streamline: Reflex feature functions not available - Reflex disabled.\n");
        p_slShutdown();
        FreeLibrary(s_sl_module);
        s_sl_module = nullptr;
        clear_all();
        return qfalse;
    }

    if (s_dlss_available && p_slIsFeatureSupported) {
        sl::AdapterInfo adapter_info{};
        adapter_info.vkPhysicalDevice = physicalDevice;
        sl::Result support = p_slIsFeatureSupported(sl::kFeatureDLSS, adapter_info);
        if (support != sl::Result::eOk) {
            Com_WPrintf("Streamline DLSS: adapter support check failed (code %d), DLSS path disabled.\n", (int)support);
            s_dlss_available = false;
        }
    }

    if (s_dlss_available)
        Com_Printf("Streamline DLSS: feature functions resolved.\n");
    else
        Com_WPrintf("Streamline DLSS: feature functions unavailable (missing feature context, plugin load, or support), DLSS path disabled.\n");

    resolve_vulkan_proxies(instance, device);

    if (!SL_vkQueuePresentKHR || !SL_vkCreateSwapchainKHR) {
        Com_WPrintf("Streamline: Vulkan proxy functions incomplete (present=%p, createSC=%p).\n",
                    (void*)SL_vkQueuePresentKHR, (void*)SL_vkCreateSwapchainKHR);
    }

    sl_reflex.initialized = qtrue;
    sl_reflex.supported   = qtrue;
    sl_reflex.mode        = REFLEX_MODE_ON;

    /* NVIDIA requires slReflexSetOptions called at least once, even when Off */
    sl::ReflexOptions initOpts{};
    initOpts.mode = sl::ReflexMode::eLowLatency;
    initOpts.frameLimitUs = 0;
    initOpts.useMarkersToOptimize = true;
    p_slReflexSetOptions(initOpts);

    query_reflex_state();

    Com_Printf("Streamline Reflex initialized (low-latency %s).\n",
               sl_reflex.lowLatencyAvailable ? "available" : "not available");
    return qtrue;
}

extern "C" void SLReflex_Shutdown(void)
{
    if (!s_preinit_done && !sl_reflex.initialized)
        return;

    if (p_slShutdown)
        p_slShutdown();

    if (s_sl_module) {
        FreeLibrary(s_sl_module);
        s_sl_module = nullptr;
    }

    clear_all();

    Com_Printf("Streamline Reflex shut down.\n");
}

extern "C" void SLReflex_BeginFrame(void)
{
    if (!sl_reflex.initialized)
        return;

    uint32_t idx = (uint32_t)(sl_reflex.frameCounter & 0xFFFFFFFF);
    sl::Result res = p_slGetNewFrameToken(s_frame_token, &idx);
    if (res != sl::Result::eOk)
        return;

    reflex_frame_id = sl_reflex.frameCounter;
    sl_reflex.frameCounter++;
}

extern "C" void SLReflex_Sleep(void)
{
    if (!sl_reflex.initialized || !p_slReflexSleep)
        return;

    /*
     * NVIDIA requires: get frame token THEN sleep.
     * Sleep is the first operation each frame, so obtain the token here.
     */
    uint32_t idx = (uint32_t)(sl_reflex.frameCounter & 0xFFFFFFFF);
    sl::FrameToken *sleepToken = nullptr;
    sl::Result res = p_slGetNewFrameToken(sleepToken, &idx);
    if (res != sl::Result::eOk || !sleepToken)
        return;

    s_frame_token = sleepToken;
    reflex_frame_id = sl_reflex.frameCounter;
    sl_reflex.frameCounter++;

    p_slReflexSleep(*s_frame_token);
}

extern "C" void SLReflex_SetMode(ReflexMode mode, uint32_t frameLimitUs)
{
    if (!sl_reflex.initialized || !p_slReflexSetOptions)
        return;

    sl::ReflexOptions opts{};

    switch (mode) {
    case REFLEX_MODE_OFF:       opts.mode = sl::ReflexMode::eOff;                 break;
    case REFLEX_MODE_ON:        opts.mode = sl::ReflexMode::eLowLatency;          break;
    case REFLEX_MODE_ON_BOOST:  opts.mode = sl::ReflexMode::eLowLatencyWithBoost; break;
    default:                    opts.mode = sl::ReflexMode::eOff;                 break;
    }
    opts.frameLimitUs = frameLimitUs;
    opts.useMarkersToOptimize = true;

    p_slReflexSetOptions(opts);
    sl_reflex.mode = mode;

    query_reflex_state();
}

/* ------------------------------------------------------------------ */
/*  PCL Markers                                                       */
/* ------------------------------------------------------------------ */

static inline void set_marker(sl::PCLMarker marker, uint64_t frameId)
{
    if (!sl_reflex.initialized || !s_frame_token || !p_slPCLSetMarker)
        return;
    p_slPCLSetMarker(marker, *s_frame_token);
    (void)frameId;
}

extern "C" void SLReflex_Marker_SimStart(uint64_t frameId)      { set_marker(sl::PCLMarker::eSimulationStart,     frameId); }
extern "C" void SLReflex_Marker_SimEnd(uint64_t frameId)        { set_marker(sl::PCLMarker::eSimulationEnd,       frameId); }
extern "C" void SLReflex_Marker_RenderStart(uint64_t frameId)   { set_marker(sl::PCLMarker::eRenderSubmitStart,   frameId); }
extern "C" void SLReflex_Marker_RenderEnd(uint64_t frameId)     { set_marker(sl::PCLMarker::eRenderSubmitEnd,     frameId); }
extern "C" void SLReflex_Marker_PresentStart(uint64_t frameId)  { set_marker(sl::PCLMarker::ePresentStart,        frameId); }
extern "C" void SLReflex_Marker_PresentEnd(uint64_t frameId)    { set_marker(sl::PCLMarker::ePresentEnd,          frameId); }
extern "C" void SLReflex_Marker_TriggerFlash(uint64_t frameId)  { set_marker(sl::PCLMarker::eTriggerFlash,        frameId); }

extern "C" void SLReflex_Marker_InputSample(uint64_t frameId)
{
    if (!sl_reflex.initialized || !s_frame_token || !p_slPCLSetMarker)
        return;
    p_slPCLSetMarker(sl::PCLMarker::ePCLatencyPing, *s_frame_token);
    (void)frameId;
}

extern "C" qboolean SLReflex_IsLowLatencyAvailable(void)
{
    if (!sl_reflex.initialized)
        return qfalse;
    query_reflex_state();
    return sl_reflex.lowLatencyAvailable;
}

static sl::DLSSMode map_dlss_mode(streamline_dlss_quality_t quality)
{
    switch (quality) {
    case SL_DLSS_QUALITY_BALANCED:
        return sl::DLSSMode::eBalanced;
    case SL_DLSS_QUALITY_PERFORMANCE:
        return sl::DLSSMode::eMaxPerformance;
    case SL_DLSS_QUALITY_ULTRA_PERFORMANCE:
        return sl::DLSSMode::eUltraPerformance;
    case SL_DLSS_QUALITY_QUALITY:
    default:
        return sl::DLSSMode::eMaxQuality;
    }
}

static bool ensure_frame_token_for_dlss(void)
{
    if (s_frame_token)
        return true;

    if (!p_slGetNewFrameToken)
        return false;

    uint32_t idx = (uint32_t)(sl_reflex.frameCounter & 0xFFFFFFFF);
    sl::Result res = p_slGetNewFrameToken(s_frame_token, &idx);
    if (res != sl::Result::eOk || !s_frame_token)
        return false;

    reflex_frame_id = sl_reflex.frameCounter;
    sl_reflex.frameCounter++;
    return true;
}

extern "C" qboolean SLDLSS_IsAvailable(void)
{
    if (!sl_reflex.initialized)
        return qfalse;
    if (!s_dlss_available)
        return qfalse;
    if (!p_slDLSSSetOptions || !p_slDLSSGetOptimalSettings)
        return qfalse;
    if (!p_slSetTagForFrame || !p_slSetConstants || !p_slEvaluateFeature)
        return qfalse;
    return qtrue;
}

extern "C" qboolean SLDLSS_GetVulkanRequirements(streamline_vk_requirements_t *requirements)
{
    if (!requirements)
        return qfalse;

    memset(requirements, 0, sizeof(*requirements));
    if (!s_dlss_requirements_valid)
        return qfalse;

    requirements->available = qtrue;
    requirements->num_instance_extensions = s_dlss_requirements.vkNumInstanceExtensions;
    requirements->instance_extensions = s_dlss_requirements.vkInstanceExtensions;
    requirements->num_device_extensions = s_dlss_requirements.vkNumDeviceExtensions;
    requirements->device_extensions = s_dlss_requirements.vkDeviceExtensions;
    requirements->num_features12 = s_dlss_requirements.vkNumFeatures12;
    requirements->features12 = s_dlss_requirements.vkFeatures12;
    requirements->num_features13 = s_dlss_requirements.vkNumFeatures13;
    requirements->features13 = s_dlss_requirements.vkFeatures13;
    requirements->num_graphics_queues_required = s_dlss_requirements.vkNumGraphicsQueuesRequired;
    requirements->num_compute_queues_required = s_dlss_requirements.vkNumComputeQueuesRequired;
    return qtrue;
}

extern "C" qboolean SLDLSS_GetOptimalSettings(streamline_dlss_quality_t quality, uint32_t output_width, uint32_t output_height,
                                              uint32_t *render_width, uint32_t *render_height)
{
    static bool warned_get_optimal = false;

    if (!SLDLSS_IsAvailable() || !render_width || !render_height)
        return qfalse;

    sl::DLSSOptions options{};
    options.mode = map_dlss_mode(quality);
    options.outputWidth = output_width;
    options.outputHeight = output_height;
    options.colorBuffersHDR = sl::Boolean::eTrue;
    options.useAutoExposure = sl::Boolean::eTrue;

    sl::DLSSOptimalSettings settings{};
    sl::Result res = p_slDLSSGetOptimalSettings(options, settings);
    if (res != sl::Result::eOk) {
        if (!warned_get_optimal) {
            Com_WPrintf("Streamline DLSS: slDLSSGetOptimalSettings failed (code %d).\n", (int)res);
            warned_get_optimal = true;
        }
        return qfalse;
    }
    warned_get_optimal = false;

    *render_width = settings.optimalRenderWidth;
    *render_height = settings.optimalRenderHeight;
    return qtrue;
}

extern "C" qboolean SLDLSS_Evaluate(const streamline_dlss_evaluate_params_t *params)
{
    static bool warned_no_token = false;
    static bool warned_set_tag = false;
    static bool warned_set_consts = false;
    static bool warned_set_options = false;
    static bool warned_evaluate = false;
    static bool logged_set_tag = false;

    if (!params || !SLDLSS_IsAvailable())
        return qfalse;

    if (!ensure_frame_token_for_dlss()) {
        if (!warned_no_token) {
            Com_WPrintf("Streamline DLSS: no frame token available.\n");
            warned_no_token = true;
        }
        return qfalse;
    }
    warned_no_token = false;

    const uint32_t common_usage_flags = VK_IMAGE_USAGE_STORAGE_BIT
        | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
        | VK_IMAGE_USAGE_TRANSFER_DST_BIT
        | VK_IMAGE_USAGE_SAMPLED_BIT
        | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    sl::Resource color_in(sl::ResourceType::eTex2d, (void*)params->color_input, nullptr, (void*)params->color_input_view, VK_IMAGE_LAYOUT_GENERAL);
    color_in.width = params->color_input_width;
    color_in.height = params->color_input_height;
    color_in.nativeFormat = params->color_input_format;
    color_in.mipLevels = 1;
    color_in.arrayLayers = 1;
    color_in.flags = 0;
    color_in.usage = common_usage_flags;

    sl::Resource color_out(sl::ResourceType::eTex2d, (void*)params->color_output, nullptr, (void*)params->color_output_view, VK_IMAGE_LAYOUT_GENERAL);
    color_out.width = params->color_output_width;
    color_out.height = params->color_output_height;
    color_out.nativeFormat = params->color_output_format;
    color_out.mipLevels = 1;
    color_out.arrayLayers = 1;
    color_out.flags = 0;
    color_out.usage = common_usage_flags;

    sl::Resource depth(sl::ResourceType::eTex2d, (void*)params->depth, nullptr, (void*)params->depth_view, VK_IMAGE_LAYOUT_GENERAL);
    depth.width = params->depth_width;
    depth.height = params->depth_height;
    depth.nativeFormat = params->depth_format;
    depth.mipLevels = 1;
    depth.arrayLayers = 1;
    depth.flags = 0;
    depth.usage = common_usage_flags;

    sl::Resource mvec(sl::ResourceType::eTex2d, (void*)params->motion_vectors, nullptr, (void*)params->motion_vectors_view, VK_IMAGE_LAYOUT_GENERAL);
    mvec.width = params->motion_vectors_width;
    mvec.height = params->motion_vectors_height;
    mvec.nativeFormat = params->motion_vectors_format;
    mvec.mipLevels = 1;
    mvec.arrayLayers = 1;
    mvec.flags = 0;
    mvec.usage = common_usage_flags;

    sl::Extent color_in_extent{ 0, 0, params->color_input_width, params->color_input_height };
    sl::Extent color_out_extent{ 0, 0, params->color_output_width, params->color_output_height };
    sl::Extent depth_extent{ 0, 0, params->depth_width, params->depth_height };
    sl::Extent mvec_extent{ 0, 0, params->motion_vectors_width, params->motion_vectors_height };

    sl::ResourceTag tags[] = {
        sl::ResourceTag{ &color_in, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &color_in_extent },
        sl::ResourceTag{ &color_out, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &color_out_extent },
        sl::ResourceTag{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilEvaluate, &depth_extent },
        sl::ResourceTag{ &mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilEvaluate, &mvec_extent },
    };

    sl::CommandBuffer* cmd = reinterpret_cast<sl::CommandBuffer*>(params->cmd_buf);
    sl::Result res = p_slSetTagForFrame(*s_frame_token, s_dlss_viewport, tags, (uint32_t)(sizeof(tags) / sizeof(tags[0])), cmd);
    if (res != sl::Result::eOk) {
        if (!warned_set_tag) {
            Com_WPrintf("Streamline DLSS: slSetTagForFrame failed (code %d).\n", (int)res);
            warned_set_tag = true;
        }
        logged_set_tag = false;
        return qfalse;
    }
    warned_set_tag = false;
    if (!logged_set_tag) {
        Com_Printf("Streamline DLSS: tagged input/output/depth/motion resources.\n");
        logged_set_tag = true;
    }

    sl::Constants consts{};
    memcpy(&consts.cameraViewToClip, params->camera_view_to_clip, sizeof(float) * 16);
    memcpy(&consts.clipToCameraView, params->clip_to_camera_view, sizeof(float) * 16);
    memcpy(&consts.clipToPrevClip, params->clip_to_prev_clip, sizeof(float) * 16);
    memcpy(&consts.prevClipToClip, params->prev_clip_to_clip, sizeof(float) * 16);
    consts.jitterOffset = { params->jitter_offset_x, params->jitter_offset_y };
    consts.mvecScale = { params->mvec_scale_x, params->mvec_scale_y };
    consts.cameraPinholeOffset = { 0.0f, 0.0f };
    consts.cameraPos = { params->camera_pos[0], params->camera_pos[1], params->camera_pos[2] };
    consts.cameraUp = { params->camera_up[0], params->camera_up[1], params->camera_up[2] };
    consts.cameraRight = { params->camera_right[0], params->camera_right[1], params->camera_right[2] };
    consts.cameraFwd = { params->camera_fwd[0], params->camera_fwd[1], params->camera_fwd[2] };
    consts.cameraNear = params->camera_near;
    consts.cameraFar = params->camera_far;
    consts.cameraFOV = params->camera_fov;
    consts.cameraAspectRatio = params->camera_aspect_ratio;
    consts.depthInverted = sl::Boolean::eFalse;
    consts.cameraMotionIncluded = sl::Boolean::eTrue;
    consts.motionVectors3D = sl::Boolean::eFalse;
    consts.motionVectorsDilated = sl::Boolean::eFalse;
    // Q2RTX motion vectors are generated against unjittered projection transforms and
    // jitter is provided separately via Constants::jitterOffset.
    consts.motionVectorsJittered = sl::Boolean::eFalse;
    consts.reset = params->reset ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    consts.orthographicProjection = sl::Boolean::eFalse;

    res = p_slSetConstants(consts, *s_frame_token, s_dlss_viewport);
    if (res != sl::Result::eOk) {
        if (!warned_set_consts) {
            Com_WPrintf("Streamline DLSS: slSetConstants failed (code %d).\n", (int)res);
            warned_set_consts = true;
        }
        return qfalse;
    }
    warned_set_consts = false;

    sl::DLSSOptions options{};
    options.mode = map_dlss_mode(params->quality);
    options.outputWidth = params->color_output_width;
    options.outputHeight = params->color_output_height;
    options.colorBuffersHDR = params->hdr ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    options.useAutoExposure = params->use_auto_exposure ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    options.alphaUpscalingEnabled = sl::Boolean::eFalse;

    res = p_slDLSSSetOptions(s_dlss_viewport, options);
    if (res != sl::Result::eOk) {
        if (!warned_set_options) {
            Com_WPrintf("Streamline DLSS: slDLSSSetOptions failed (code %d).\n", (int)res);
            warned_set_options = true;
        }
        return qfalse;
    }
    warned_set_options = false;

    const sl::BaseStructure* inputs[] = { &s_dlss_viewport };
    res = p_slEvaluateFeature(sl::kFeatureDLSS, *s_frame_token, inputs, (uint32_t)(sizeof(inputs) / sizeof(inputs[0])), cmd);
    if (res != sl::Result::eOk) {
        if (!warned_evaluate) {
            Com_WPrintf("Streamline DLSS: slEvaluateFeature failed (code %d).\n", (int)res);
            warned_evaluate = true;
        }
        return qfalse;
    }
    warned_evaluate = false;

    return qtrue;
}
