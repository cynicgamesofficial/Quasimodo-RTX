# Q2RTX Build Configuration

**Date:** March 12, 2026  
**Platform:** Windows 10 (10.0.19045)  
**Build type:** RelWithDebInfo (Release with Debug Info)

---

## Environment

| Component | Version/Path |
|-----------|--------------|
| CMake | 4.2.0-rc2 |
| Git | 2.49.0.windows.1 |
| Vulkan SDK | `D:\DISILLUSION\DE- ENGINES\VKQ\APIS` |
| Visual Studio | 2022 (MSVC 19.38.33145.0) |
| Windows SDK | 10.0.26100.0 |

---

## CMake Configuration

**Generator:** Visual Studio 17 2022  
**Architecture:** x64

```powershell
mkdir -p build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

**Flags used:**
- `-G "Visual Studio 17 2022"` — Visual Studio 2022 solution generator
- `-A x64` — 64-bit target
- `-DCMAKE_BUILD_TYPE=RelWithDebInfo` — Release build with debug symbols (recommended for integration work)

---

## Submodules

All submodules were initialized and updated:

```powershell
git submodule update --init --recursive
```

| Submodule | Path | Purpose |
|-----------|------|---------|
| zlib | `extern/zlib` | Compression |
| curl | `extern/curl` | HTTP |
| SDL2 | `extern/SDL2` | Windowing, input, audio |
| stb | `extern/stb` | Image loading |
| tinyobjloader-c | `extern/tinyobjloader-c` | OBJ loading |
| Vulkan-Headers | `extern/Vulkan-Headers` | Vulkan API headers |
| glslang | `extern/glslang` | Shader compilation (SDK used on Windows) |
| openal-soft | `extern/openal-soft` | Audio |

---

## Build Command

```powershell
cd build
cmake --build . --config RelWithDebInfo --parallel
```

---

## Output Locations

| Output | Path |
|--------|------|
| Main executable | `q2rtx.exe` (repo root) |
| Dedicated server | `q2rtxded.exe` (repo root) |
| Game DLL | `baseq2/gamex86_64.dll` |
| Compiled shaders | `baseq2/shader_vkpt/*.spv` |
| Libraries (zlib, etc.) | `build/Bin/RelWithDebInfo/` |

---

## CMake Options (Available)

| Option | Default (Windows) | Purpose |
|--------|-------------------|---------|
| `CONFIG_BUILD_GLSLANG` | OFF | Build glslang from source instead of Vulkan SDK |
| `CONFIG_BUILD_IPO` | OFF | Interprocedural optimization |
| `CONFIG_BUILD_SHADER_DEBUG_INFO` | OFF | Shader debug symbols |
| `USE_SYSTEM_*` | OFF | Use system libraries (zlib, SDL2, OpenAL, curl) |

---

## Workspace Settings (Cursor/VS Code)

The `.vscode/settings.json` includes:

- **CMake:** build directory `build`, Ninja generator, configure on open
- **C/C++:** C11, C++17, MSVC x64 IntelliSense
- **File associations:** `.glsl`, `.vert`, `.frag`, `.comp`, `.rgen`, `.rchit`, `.rmiss` → GLSL
- **Search exclude:** `build/`, `extern/`

---

## Run Command

```powershell
.\q2rtx.exe +set vid_renderer vkpt
```

Requires game assets in `baseq2/`.
