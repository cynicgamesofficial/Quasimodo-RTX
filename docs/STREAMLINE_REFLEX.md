# NVIDIA Reflex via Streamline — Implementation Notes

This document describes how NVIDIA Reflex is integrated into Q2RTX using the **Streamline SDK** (not the legacy standalone Vulkan Reflex SDK).

---

## 1. Overview

- **API used:** Streamline Reflex + PCL (PC Latency) markers. No `NvLowLatencyVk.*`.
- **Integration mode:** Manual hooking (`eUseManualHooking`) with **dynamic loading** of `sl.interposer.dll`. If the DLL is missing, Reflex is disabled and the game runs normally.
- **Vulkan hooks:** All mandatory Vulkan functions listed in `sl_hooks.h` (present, swapchain create/destroy/images/acquire, device wait idle, surface destroy) are routed through SL proxy functions resolved from `sl.interposer.dll`. This ensures `presentCommon()` fires every frame.
- **Platform:** Windows only (guarded by `#ifdef _WIN32` / `IF(WIN32)` where relevant).

---

## 2. Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│  Client (C)                                                      │
│  - CL_Frame()        → R_Reflex_SleepAtFrameStart() [first op]   │
│  - Key_Event(MOUSE1) → R_Reflex_TriggerFlash()                   │
└─────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────┐
│  inc/refresh/refresh.h  —  extern callback pointers              │
│  src/client/refresh.c   —  R_Reflex_* = NULL until RTX register  │
└─────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────┐
│  Renderer (src/refresh/vkpt/main.c)                              │
│  - Uses SL_vk* proxies for all hooked Vulkan calls               │
│  - R_BeginFrame_RTX()  → markers (token obtained by Sleep)       │
│  - R_EndFrame_RTX()    → markers + SL_vkQueuePresentKHR          │
│  - R_Reflex_SleepAtFrameStart_RTX(), R_Reflex_TriggerFlash_RTX()  │
└─────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────┐
│  Streamline wrapper (C++), src/refresh/vkpt/                     │
│  - streamline_reflex.h   —  C API + VK proxy function pointers   │
│  - streamline_reflex.cpp —  Two-phase init (PreInit/PostInit),   │
│                             slInit, slSetVulkanInfo, VK proxy    │
│                             resolution, PCL markers, Sleep       │
└─────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────┐
│  Streamline SDK (third/release or streamline/)                   │
│  - sl.interposer.dll, sl.common.dll, sl.reflex.dll, sl.pcl.dll    │
└─────────────────────────────────────────────────────────────────┘
```

- The **client** never calls Streamline directly; it only calls the renderer callbacks.
- The **wrapper** is the only code that includes Streamline headers and talks to the SDK; the rest of the engine uses the C API in `streamline_reflex.h`.

---

## 3. Files Touched

| Path | Role |
|------|------|
| **New** `src/refresh/vkpt/streamline_reflex.h` | C API: `SLReflex_PreInit`, `SLReflex_PostInit`, `SLReflex_Shutdown`, `SLReflex_Sleep`, `SLReflex_SetMode`, marker functions, `sl_reflex` state, `reflex_frame_id`, Vulkan proxy function pointers (`SL_vk*`). |
| **New** `src/refresh/vkpt/streamline_reflex.cpp` | Two-phase init (PreInit before Vulkan, PostInit after device). Loads `sl.interposer.dll`, resolves SL core + feature APIs, resolves VK proxy functions via SL's `vkGetDeviceProcAddr`/`vkGetInstanceProcAddr`. |
| `inc/refresh/refresh.h` | Declares `R_Reflex_SleepAtFrameStart`, `R_Reflex_TriggerFlash`. |
| `src/client/refresh.c` | Defines those pointers and sets them to `NULL` until RTX registers. |
| `src/refresh/vkpt/main.c` | Registers RTX implementations, uses `SL_vk*` proxies for all hooked Vulkan calls, calls `SLReflex_*` at frame boundaries and present, adds `cl_reflex` cvar. |
| `src/client/main.c` | Calls `R_Reflex_SleepAtFrameStart()` at the very start of `CL_Frame` (before `CL_ProcessEvents`). |
| `src/client/keys.c` | On `K_MOUSE1` down, calls `R_Reflex_TriggerFlash()` before any other key handling. |
| `src/CMakeLists.txt` | Adds `streamline_reflex.cpp` (Windows), Streamline include path, optional post-build copy of SL DLLs from `streamline/bin/x64/`. |
| `.gitignore` | Ignores `streamline/bin/`, `streamline/lib/`. |

---

## 4. Per-Frame and Input Flow

### Frame (every frame when rendering)

1. **`CL_Frame()`**
   - First operation: `R_Reflex_SleepAtFrameStart()` → `SLReflex_Sleep()`.
   - Sleep obtains a **new frame token** from SL first, then calls `slReflexSleep(*token)`.

2. **`R_BeginFrame_RTX()`**
   - Apply `cl_reflex` → `SLReflex_SetMode(want, 0)` on change.
   - Markers: **SimStart**, **SimEnd**, **RenderStart** (token already valid from Sleep).
   - Then existing logic (fence wait, acquire image via `SL_vkAcquireNextImageKHR`, etc.).

3. **`R_EndFrame_RTX()`**
   - Before present: **RenderEnd**, **PresentStart**.
   - `SL_vkQueuePresentKHR(...)` — through Streamline proxy (triggers `presentCommon()`).
   - After present: **PresentEnd**.
   - Then `qvk.frame_counter++`.

### Input (flash marker)

- **`Key_Event(key, down, time)`**
  - If `key == K_MOUSE1` and `down`: call `R_Reflex_TriggerFlash()`.
  - That calls `SLReflex_Marker_InputSample(reflex_frame_id)` (ePCLatencyPing) and `SLReflex_Marker_TriggerFlash(reflex_frame_id)` (unconditional, no `cl_reflex` check).

---

## 5. Init and Shutdown Order

### Init (NVIDIA-mandated order)

```
SLReflex_PreInit()               ← slInit with eUseManualHooking
    ↓
init_vulkan()                    ← vkCreateInstance + vkCreateDevice
    ↓
SLReflex_PostInit(instance,      ← slSetVulkanInfo + resolve VK proxies
  physDevice, device, ...)           + slReflexSetOptions (initial call)
    ↓
create_swapchain()               ← uses SL_vkCreateSwapchainKHR proxy
```

This satisfies the NVIDIA requirement that `slInit` is called **before** `vkCreateDevice`, and `slSetVulkanInfo` is called **after** device creation but **before** the first swapchain.

### Shutdown

```
SLReflex_Shutdown()              ← slShutdown (before Vulkan teardown)
    ↓
destroy_vulkan()                 ← vkDestroyDevice etc.
```

---

## 6. Vulkan Proxy Routing (Manual Hooking)

Per `ProgrammingGuideManualHooking.md` section 4.2, all Vulkan functions listed in `sl_hooks.h` must go through SL proxy functions. The wrapper resolves these from `sl.interposer.dll`'s exported `vkGetDeviceProcAddr`/`vkGetInstanceProcAddr`:

| Proxy Variable | Replaces | Used In |
|---|---|---|
| `SL_vkQueuePresentKHR` | `vkQueuePresentKHR` | `R_EndFrame_RTX` |
| `SL_vkCreateSwapchainKHR` | `vkCreateSwapchainKHR` | `create_swapchain` |
| `SL_vkDestroySwapchainKHR` | `vkDestroySwapchainKHR` | `destroy_swapchain` |
| `SL_vkGetSwapchainImagesKHR` | `vkGetSwapchainImagesKHR` | `create_swapchain` |
| `SL_vkAcquireNextImageKHR` | `vkAcquireNextImageKHR` | `R_BeginFrame_RTX` |
| `SL_vkDeviceWaitIdle` | `vkDeviceWaitIdle` | All wait sites |
| `SL_vkDestroySurfaceKHR` | `vkDestroySurfaceKHR` | `destroy_vulkan` |

When `SL_vk*` is NULL (SL not loaded), native Vulkan functions are used as fallback.

**Note:** `vkCreateWin32SurfaceKHR` is not proxied because SDL creates the surface internally and we cannot intercept that call. This is acceptable for Reflex-only integration.

---

## 7. Console Variable

| Cvar | Default | Archive | Meaning |
|------|---------|--------|--------|
| `cl_reflex` | `1` | Yes | `0` = Off, `1` = On, `2` = On + Boost |

Applied at the start of each `R_BeginFrame_RTX()` when the cvar value changes. `slReflexSetOptions` is called at initialization and on every cvar change, including when set to Off (per NVIDIA requirement).

---

## 8. Build and Runtime

### CMake

- Streamline is optional. SDK path: `CMAKE_SOURCE_DIR/../third/release` (overridable via cache `STREAMLINE_SDK_DIR`).
- If `third/release/include/sl.h` exists and target is Windows:
  - `streamline_reflex.cpp` is compiled.
  - Include path: `third/release/include`.
  - Post-build: if DLLs exist in `streamline/bin/x64/`, they are copied to the repo root (runtime dir of `q2rtx.exe`).

### Required DLLs (at runtime)

Place in `streamline/bin/x64/` (or same directory as `q2rtx.exe`):

- `sl.interposer.dll`
- `sl.common.dll`
- `sl.reflex.dll`
- `sl.pcl.dll`

The wrapper first tries `sl.interposer.dll` in the current directory, then `streamline\bin\x64\sl.interposer.dll`. Other DLLs must be loadable from the same directory (or PATH). If `sl.interposer.dll` is not found, Reflex is disabled and a single console message is printed.

---

## 9. Design Rules

- **Sleep** at the very start of `CL_Frame`, before input. Frame token obtained before sleep.
- **Flash marker** on **K_MOUSE1 down** only, unconditional (menus, pause, etc.).
- **No** `NvLowLatencyVk.*` or standalone Reflex Vulkan SDK.
- **No** GPU/vendor checks in engine code; only Streamline's reported state (e.g. `lowLatencyAvailable`) is used for UI.
- **All six timing markers** (SimStart/End, RenderStart/End, PresentStart/End) run every frame when Reflex is initialized, regardless of `cl_reflex` (latency measurement still needs them).
- **`slReflexSetOptions`** called at least once during init and on every cvar change, even when mode is Off.
- **All hooked Vulkan calls** routed through SL proxy to ensure `presentCommon()` fires.
- **Renderer callbacks** only: client never calls Streamline directly.

---

## 10. Quick Reference: Call Order

```
CL_Frame()
  ├─ R_Reflex_SleepAtFrameStart()   ← first: get token + sleep
  ├─ CL_ProcessEvents()
  ├─ … simulation …
  └─ SCR_UpdateScreen()
       └─ R_BeginFrame_RTX()
            ├─ SLReflex_SetMode(cl_reflex)
            ├─ SimStart, SimEnd, RenderStart
            ├─ SL_vkAcquireNextImageKHR
            └─ …
       └─ R_RenderFrame_RTX()
       └─ R_EndFrame_RTX()
            ├─ RenderEnd, PresentStart
            ├─ SL_vkQueuePresentKHR  ← triggers presentCommon()
            └─ PresentEnd

Key_Event(K_MOUSE1, down=true)
  └─ R_Reflex_TriggerFlash()  → ePCLatencyPing + eTriggerFlash
```
