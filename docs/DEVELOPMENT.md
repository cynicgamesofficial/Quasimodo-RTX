# Q2RTX Windows Development Guide

This document describes the developer workflow for building and running NVIDIA Q2RTX on Windows.

---

## Prerequisites

| Requirement | Notes |
|-------------|-------|
| **Windows 10/11** | 64-bit |
| **CMake** | 3.15+ (3.8 minimum per project) |
| **Visual Studio 2022** | With "Desktop development with C++" workload |
| **Vulkan SDK** | 1.2.162+ from [LunarG](https://www.lunarg.com/vulkan-sdk/) |
| **Git** | 2.15+ for submodules |

Ensure `VULKAN_SDK` environment variable is set to your Vulkan SDK install path (e.g. `C:\VulkanSDK\1.3.xxx`).

---

## Quick Start

```powershell
# From repository root
.\dev.ps1 submodules
.\dev.ps1 configure
.\dev.ps1 build
.\dev.ps1 run
```

---

## Windows Workflow

### 1. Initialize submodules

```powershell
.\dev.ps1 submodules
```

Runs `git submodule update --init --recursive`. Required before first configure.

### 2. Configure

```powershell
.\dev.ps1 configure
```

- Ensures `build/` exists
- Runs CMake with: Visual Studio 17 2022, x64, RelWithDebInfo
- Fails if `VULKAN_SDK` is missing

### 3. Build

```powershell
.\dev.ps1 build
```

- Runs `cmake --build build --config RelWithDebInfo --parallel`
- Outputs executable path on success

### 4. Rebuild (incremental clean + configure + build)

```powershell
.\dev.ps1 rebuild
```

Deletes generated files inside `build/`, reconfigures, and rebuilds. Use when switching branches or after CMake changes.

### 5. Clean

```powershell
.\dev.ps1 clean
```

Removes generated files inside `build/` only. Does not delete source files.

### 6. Clean build (full wipe)

```powershell
.\dev.ps1 clean-build
```

Deletes the entire `build/` directory, recreates it, configures, and builds. Use when you need a completely fresh build.

### 7. Run

```powershell
.\dev.ps1 run
```

Runs `q2rtx.exe` with `+set vid_renderer vkpt`. Warns if assets are missing instead of failing.

### 8. Shaders only

```powershell
.\dev.ps1 shaders
```

Compiles Vulkan shaders into `baseq2/shader_vkpt/`. Reuses the existing `compile_shaders.bat` when present.

### 9. Doctor (diagnostics)

```powershell
.\dev.ps1 doctor
```

Reports status: Git, CMake, Visual Studio, Vulkan SDK, submodules, build folder, executable, assets.

---

## Build Configuration

| Setting | Value |
|---------|-------|
| Generator | Visual Studio 17 2022 |
| Architecture | x64 |
| Configuration | RelWithDebInfo |
| Build directory | `build/` |
| Executable | `q2rtx.exe` (repo root) |
| Game DLL | `baseq2/gamex86_64.dll` |
| Shaders | `baseq2/shader_vkpt/*.spv` |

---

## Shader Workflow

Shaders are compiled by the CMake `shaders` target, invoked via:

1. **Full build:** `.\dev.ps1 build` (client depends on shaders)
2. **Shaders only:** `.\dev.ps1 shaders`

The `shaders.ps1` script uses `compile_shaders.bat` when it exists, otherwise runs `cmake --build build --target shaders`.

---

## Rebuild Workflow

When to use each:

- **`.\dev.ps1 build`** — Normal incremental build
- **`.\dev.ps1 rebuild`** — Clean build artifacts, reconfigure, rebuild (keeps `build/` folder)
- **`.\dev.ps1 clean-build`** — Delete `build/`, recreate, configure, build (full reset)

---

## Runtime Asset Requirements

The game requires assets in `baseq2/`:

- `q2rtx_media.pkz` — From Q2RTX releases
- `blue_noise.pkz` — From Q2RTX releases
- `pak0.pak`, `pak1.pak`, etc. — From original Quake II

See [readme.md](../readme.md) for details. Development builds can run with minimal assets; the run script warns if expected files are missing.

---

## Troubleshooting

| Issue | Solution |
|-------|----------|
| `VULKAN_SDK` not found | Set env var to SDK path, restart terminal |
| Submodules empty | Run `.\dev.ps1 submodules` |
| Build fails "CMakeLists.txt not found" in extern | Submodules not initialized; run submodules |
| `q2rtx.exe` not found after build | Executable outputs to repo root; verify no build errors |
| Shader compile errors | Ensure `glslangValidator` is on PATH (Vulkan SDK `bin/`) |
| SDL2/OpenAL not found | Submodules must be initialized |
| 32-bit build | Use `-A x64` (handled by configure script) |

---

## Script Locations

All dev scripts live under `scripts/dev/`:

- `_common.ps1` — Shared utilities
- `configure.ps1`, `build.ps1`, `rebuild.ps1`, `clean.ps1`, `clean-build.ps1`
- `run.ps1`, `shaders.ps1`, `doctor.ps1`, `submodules.ps1`

Invoke via `.\dev.ps1 <command>` from repo root.
