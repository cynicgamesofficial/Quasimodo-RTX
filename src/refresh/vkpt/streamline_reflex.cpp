/*
 * Streamline Reflex wrapper – dynamic-load implementation.
 *
 * Loads sl.interposer.dll at runtime so the game is still playable
 * when the DLL or the driver is absent.  All Streamline C++ types
 * stay confined to this translation unit.
 */

#define VK_VERSION_1_0 1
#include <vulkan/vulkan.h>

#include "sl.h"
#include "sl_reflex.h"
#include "sl_pcl.h"
#include "sl_helpers_vk.h"
#include "sl_consts.h"

#include <Windows.h>
#include <cstdio>

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

/* ------------------------------------------------------------------ */
/*  Private state                                                     */
/* ------------------------------------------------------------------ */

static HMODULE             s_sl_module;
static sl::FrameToken     *s_frame_token;

/* Dynamically-resolved SL core API */
using Fn_slInit                = sl::Result(const sl::Preferences&, uint64_t);
using Fn_slShutdown            = sl::Result();
using Fn_slSetVulkanInfo       = sl::Result(const sl::VulkanInfo&);
using Fn_slGetNewFrameToken    = sl::Result(sl::FrameToken*&, const uint32_t*);
using Fn_slGetFeatureFunction  = sl::Result(sl::Feature, const char*, void*&);
using Fn_slIsFeatureSupported  = sl::Result(sl::Feature, const sl::AdapterInfo&);

static Fn_slInit               *p_slInit;
static Fn_slShutdown           *p_slShutdown;
static Fn_slSetVulkanInfo      *p_slSetVulkanInfo;
static Fn_slGetNewFrameToken   *p_slGetNewFrameToken;
static Fn_slGetFeatureFunction *p_slGetFeatureFunction;
static Fn_slIsFeatureSupported *p_slIsFeatureSupported;

/* Dynamically-resolved feature functions */
static PFun_slReflexGetState     *p_slReflexGetState;
static PFun_slReflexSleep        *p_slReflexSleep;
static PFun_slReflexSetOptions   *p_slReflexSetOptions;
static PFun_slPCLSetMarker       *p_slPCLSetMarker;

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

    return p_slReflexGetState && p_slReflexSleep && p_slReflexSetOptions && p_slPCLSetMarker;
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

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

extern "C" qboolean SLReflex_Init(void *vk_instance, void *vk_physical_device,
                                    void *vk_device, uint32_t graphics_queue_family,
                                    uint32_t graphics_queue_index)
{
    memset(&sl_reflex, 0, sizeof(sl_reflex));
    reflex_frame_id = 0;

    /* --- Load sl.interposer.dll --- */
    const wchar_t *dll_paths[] = {
        L"sl.interposer.dll",
        L"streamline\\bin\\x64\\sl.interposer.dll",
    };

    for (int i = 0; i < 2 && !s_sl_module; i++)
        s_sl_module = LoadLibraryW(dll_paths[i]);

    if (!s_sl_module) {
        Com_Printf("Streamline: sl.interposer.dll not found – Reflex disabled.\n");
        return qfalse;
    }

    /* --- Resolve core functions --- */
    p_slInit               = sl_get_proc<Fn_slInit>("slInit");
    p_slShutdown           = sl_get_proc<Fn_slShutdown>("slShutdown");
    p_slSetVulkanInfo      = sl_get_proc<Fn_slSetVulkanInfo>("slSetVulkanInfo");
    p_slGetNewFrameToken   = sl_get_proc<Fn_slGetNewFrameToken>("slGetNewFrameToken");
    p_slGetFeatureFunction = sl_get_proc<Fn_slGetFeatureFunction>("slGetFeatureFunction");
    p_slIsFeatureSupported = sl_get_proc<Fn_slIsFeatureSupported>("slIsFeatureSupported");

    if (!p_slInit || !p_slShutdown || !p_slSetVulkanInfo ||
        !p_slGetNewFrameToken || !p_slGetFeatureFunction) {
        Com_EPrintf("Streamline: failed to resolve core API – Reflex disabled.\n");
        FreeLibrary(s_sl_module);
        s_sl_module = nullptr;
        return qfalse;
    }

    /* --- slInit --- */
    sl::Feature features[] = { sl::kFeatureReflex, sl::kFeaturePCL };

    sl::Preferences prefs{};
    prefs.showConsole       = false;
    prefs.logLevel          = sl::LogLevel::eDefault;
    prefs.logMessageCallback = sl_log_callback;
    prefs.featuresToLoad    = features;
    prefs.numFeaturesToLoad = 2;
    prefs.engine            = sl::EngineType::eCustom;
    prefs.engineVersion     = "Q2RTX";
    prefs.renderAPI         = sl::RenderAPI::eVulkan;
    prefs.flags             = sl::PreferenceFlags::eDisableCLStateTracking
                            | sl::PreferenceFlags::eUseManualHooking;

    sl::Result res = p_slInit(prefs, sl::kSDKVersion);
    if (res != sl::Result::eOk) {
        Com_EPrintf("Streamline: slInit failed (code %d) – Reflex disabled.\n", (int)res);
        FreeLibrary(s_sl_module);
        s_sl_module = nullptr;
        return qfalse;
    }

    /* --- slSetVulkanInfo --- */
    sl::VulkanInfo vkInfo{};
    vkInfo.device          = static_cast<VkDevice>(vk_device);
    vkInfo.instance        = static_cast<VkInstance>(vk_instance);
    vkInfo.physicalDevice  = static_cast<VkPhysicalDevice>(vk_physical_device);
    vkInfo.graphicsQueueFamily = graphics_queue_family;
    vkInfo.graphicsQueueIndex  = graphics_queue_index;

    res = p_slSetVulkanInfo(vkInfo);
    if (res != sl::Result::eOk) {
        Com_EPrintf("Streamline: slSetVulkanInfo failed (code %d) – Reflex disabled.\n", (int)res);
        p_slShutdown();
        FreeLibrary(s_sl_module);
        s_sl_module = nullptr;
        return qfalse;
    }

    /* --- Resolve feature functions --- */
    if (!resolve_feature_functions()) {
        Com_WPrintf("Streamline: Reflex feature functions not available – Reflex disabled.\n");
        p_slShutdown();
        FreeLibrary(s_sl_module);
        s_sl_module = nullptr;
        return qfalse;
    }

    /* --- Query initial state --- */
    sl_reflex.initialized = qtrue;
    sl_reflex.supported   = qtrue;
    sl_reflex.mode        = REFLEX_MODE_ON;

    query_reflex_state();

    Com_Printf("Streamline Reflex initialized (low-latency %s).\n",
               sl_reflex.lowLatencyAvailable ? "available" : "not available");
    return qtrue;
}

extern "C" void SLReflex_Shutdown(void)
{
    if (!sl_reflex.initialized)
        return;

    if (p_slShutdown)
        p_slShutdown();

    if (s_sl_module) {
        FreeLibrary(s_sl_module);
        s_sl_module = nullptr;
    }

    p_slInit               = nullptr;
    p_slShutdown           = nullptr;
    p_slSetVulkanInfo      = nullptr;
    p_slGetNewFrameToken   = nullptr;
    p_slGetFeatureFunction = nullptr;
    p_slIsFeatureSupported = nullptr;
    p_slReflexGetState     = nullptr;
    p_slReflexSleep        = nullptr;
    p_slReflexSetOptions   = nullptr;
    p_slPCLSetMarker       = nullptr;

    s_frame_token = nullptr;
    memset(&sl_reflex, 0, sizeof(sl_reflex));
    reflex_frame_id = 0;

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
    if (!sl_reflex.initialized || !s_frame_token || !p_slReflexSleep)
        return;

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
