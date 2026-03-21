# Streamline Portability + Runtime Audit Log (2026-03-21)

## 1) Scope of today's work

This document records the full set of Streamline-related work performed on March 21, 2026:

- Portability refactor to remove developer-local Streamline SDK path assumptions.
- Runtime loading-path hardening for interposer + plugins.
- Windows post-build deployment updates for Streamline runtime DLLs.
- Clean rebuild validation using project build scripts.
- Investigation of the Streamline warning:
  - `validateCommonConstants ... cameraPinholeOffset should not be left as invalid`
- Comparison audit against the historical project:
  - `D:\DISILLUSION\DE- ENGINES\old no warning\Q2RTX`

No renderer architecture changes were made in this pass (no lifecycle reordering for Streamline init/post-init paths).

## 2) Code and build-system changes made today

### 2.1 Canonical SDK root (repo-local)

- File: `CMakeLists.txt`
- Change: Added canonical cache variable:
  - `STREAMLINE_ROOT = ${PROJECT_SOURCE_DIR}/Third Parties/NVIDIA`

Purpose:
- Remove dependency on machine-local SDK location.
- Make configure/build portable across machines from a clean clone.

### 2.2 Streamline build wiring and DLL deployment

- File: `src/CMakeLists.txt`
- Changes:
  - Replaced old external SDK include path usage with:
    - `${STREAMLINE_ROOT}/include`
    - `${STREAMLINE_ROOT}/bin/x64`
    - `${STREAMLINE_ROOT}/bin/x64/development`
  - Added runtime source selection:
    - prefer `development` runtime when present
    - fall back to non-development runtime if needed
  - Updated post-build deployment:
    - copy runtime DLL set to `streamline/bin/x64`
    - copy `sl.interposer.dll` next to `q2rtx.exe`

Purpose:
- Keep runtime location portable and executable-relative.
- Preserve development runtime behavior used for Streamline diagnostics/overlay.

### 2.3 Runtime interposer and plugin-path fallback chain

- File: `src/refresh/vkpt/streamline_reflex.cpp`
- Changes in load-path construction:
  - Removed `..\\third\\release\\...` assumptions.
  - Added executable-relative/repo-relative fallbacks:
    - `streamline\\bin\\x64`
    - `Third Parties\\NVIDIA\\bin\\x64\\development`
    - `Third Parties\\NVIDIA\\bin\\x64`

Purpose:
- Keep runtime startup independent from developer drive/path layout.

### 2.4 Pinhole-offset warning fix

- File: `src/refresh/vkpt/streamline_reflex.cpp`
- Change in `SLDLSS_Evaluate` constants setup:
  - Added:
    - `consts.cameraPinholeOffset = { 0.0f, 0.0f };`

Why:
- Streamline common validation warns when `cameraPinholeOffset` is left at `INVALID_FLOAT`.
- Q2RTX was previously leaving it invalid.

Expected behavior impact:
- Removes one-time Streamline warning.
- Supplies neutral pinhole offset (no optical center shift).
- No lifecycle/order changes.

## 3) Build validation done today

Build workflow used (per project scripts):

- `.\dev.ps1 clean`
- `.\dev.ps1 configure`
- `.\dev.ps1 build`

Result:
- Clean succeeded.
- Configure succeeded (outside sandbox where MSVC toolchain is visible).
- Build succeeded.
- Runtime deployment step copied Streamline runtime DLLs to `streamline/bin/x64` and `sl.interposer.dll` next to executable.

## 4) Runtime warning investigation summary

Investigated warning:

- `[SL] ... commonEntry.cpp:470[validateCommonConstants] Value consts.cameraPinholeOffset should not be left as invalid`

Local SDK evidence:

- `include/sl_consts.h`:
  - `float2` default constructor initializes to `INVALID_FLOAT`.
  - `Constants::cameraPinholeOffset` is a `float2`.
- `source/plugins/sl.common/commonEntry.cpp`:
  - `validateCommonConstants` checks `cameraPinholeOffset` and warns if invalid.
  - Validation runs once in `slSetConstants` (`SL_RUN_ONCE`).

Conclusion:
- Warning was caused by uninitialized `cameraPinholeOffset` in Q2RTX constants path.
- Not caused by Streamline SDK relocation itself.

## 5) Comparison audit vs "old no warning" project

Reference project:
- `D:\DISILLUSION\DE- ENGINES\old no warning\Q2RTX`

### 5.1 Key finding: old project also had the warning

Old project logs contain the same warning:

- `baseq2/logs/console.log` line ~698:
  - `validateCommonConstants ... cameraPinholeOffset should not be left as invalid`

This demonstrates the issue existed before today's portability refactor.

### 5.2 DLL/runtime comparison

Compared hashes/lengths for key runtime DLLs across:

- current repo root
- current `streamline/bin/x64`
- old repo root
- old `streamline/bin/x64`
- old external SDK development path

Result:
- `sl.interposer.dll`, `sl.common.dll`, `sl.dlss.dll`, `sl.reflex.dll`, `sl.pcl.dll`, `sl.imgui.dll` matched across compared active runtime sources.
- Both old and current root `sl.interposer.dll` matched development build (not production).
- `sl.interposer.json` and `sl.common.json` content matched between current and old projects.

### 5.3 Loading-chain differences (intentional portability changes)

Old code:
- preferred `..\\third\\release\\...` path candidates first.

Current code:
- prefers executable-local `streamline\\bin\\x64` first,
- then repo-local `Third Parties\\NVIDIA\\...` fallbacks.

Assessment:
- Path resolution strategy changed for portability.
- Effective core runtime binaries remained equivalent in audited environment.

## 6) Final assessment

- The pinhole warning was a pre-existing integration gap, not a new regression from SDK relocation.
- Today's explicit `cameraPinholeOffset = {0,0}` initialization is a targeted integration fix, not a renderer behavior rewrite.
- Streamline path/deployment changes improved portability and removed developer-machine path dependence.
- No evidence was found that today's portability refactor broke the previously working DLSS/Reflex runtime chain.

