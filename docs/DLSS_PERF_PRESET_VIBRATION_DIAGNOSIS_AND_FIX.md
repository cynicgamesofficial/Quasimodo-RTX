# Q2RTX DLSS SR: Performance-Preset Vibration Diagnosis and Targeted Fix

Date: 2026-03-14
Branch: master
Scope: Conservative targeted diagnosis and low-risk correction only

## 1. Problem Statement

After DLSS SR bring-up and prior hardening work, the branch still showed a subtle micro-vibration in lower DLSS quality presets (most visible in Performance-class behavior), while Quality/Balanced looked stable.

The task for this pass was explicitly conservative:
- do not redesign the renderer
- do not reopen solved architecture
- only apply a change if there is a clear, high-confidence mismatch

## 2. References Used

Local references audited:
- `D:\DISILLUSION\DE- ENGINES\third\docs\ProgrammingGuide.md`
- `D:\DISILLUSION\DE- ENGINES\third\docs\ProgrammingGuideDLSS.md`
- `D:\DISILLUSION\DE- ENGINES\third\release\include\sl_consts.h`
- `D:\DISILLUSION\DE- ENGINES\third\release\source\plugins\sl.dlss\dlssEntry.cpp`
- `D:\DISILLUSION\DE- ENGINES\DLSS_Sample_App\ngx_dlss_demo\DemoMain.cpp`
- `D:\DISILLUSION\DE- ENGINES\DLSS_Sample_App\ngx_dlss_demo\NGXWrapper.cpp`

Q2RTX path audited:
- `src/refresh/vkpt/main.c`
- `src/refresh/vkpt/streamline_reflex.cpp`
- `src/refresh/vkpt/asvgf.c`
- `src/refresh/vkpt/shader/checkerboard_interleave.comp`
- `src/refresh/vkpt/shader/primary_rays.rgen`
- `src/refresh/vkpt/shader/reflect_refract.rgen`
- `src/refresh/vkpt/shader/projection.glsl`

## 3. Findings

### 3.1 Jitter generation and submission

Q2RTX and sample-app behavior are aligned:
- Halton-style jitter (`halton(base, frame) - 0.5`) in pixel space
- jitter submitted per frame as separate DLSS constants

No direct jitter amplitude or phase bug was found in this pass.

### 3.2 Motion-vector semantic mismatch (confirmed root mismatch)

Confirmed mismatch in Q2RTX DLSS constants:
- Q2RTX motion vectors are generated against unjittered projection transforms.
- Jitter is supplied separately through `Constants::jitterOffset`.
- But `consts.motionVectorsJittered` was set to `true`.

Why this matters:
- Streamline DLSS plugin uses this field to set DLSS creation/evaluation behavior (`MVJittered` path).
- This flag is not cosmetic metadata.
- At lower presets (higher upscale ratio), semantic mismatch can present as subtle temporal instability.

### 3.3 MGPU depth synchronization hole (secondary correctness fix)

The DLSS depth input now comes from `FLAT_DEPTH`, which is built from `PT_VIEW_DEPTH_A` in interleave.
In MGPU mode, interleave already copied PT motion/color from GPU1 to GPU0, but not PT view depth.
That can produce inconsistent depth input in MGPU and destabilize temporal reconstruction.

## 4. Changes Applied

## 4.1 Fix motion vector jitter metadata

File:
- `src/refresh/vkpt/streamline_reflex.cpp`

Change:
- `consts.motionVectorsJittered` changed from `sl::Boolean::eTrue` to `sl::Boolean::eFalse`.

Rationale:
- Aligns DLSS metadata with actual motion vector semantics in Q2RTX.
- Keeps jitter submission via `jitterOffset` unchanged.
- Minimal and low-risk.

## 4.2 Fix MGPU depth copy for interleave/DLSS depth path

File:
- `src/refresh/vkpt/asvgf.c`

Changes:
- Added MGPU copy for `VKPT_IMG_PT_VIEW_DEPTH_A` from GPU1 to GPU0 in `vkpt_interleave`.
- Added explicit barrier for `VKPT_IMG_FLAT_DEPTH` after interleave dispatch.

Rationale:
- Ensures DLSS depth input consistency in MGPU path.
- Keeps single-GPU behavior unchanged.

## 5. Build Verification

Build flow used (per project rules):
- `.\dev.ps1 build`

Result:
- Build succeeded after running outside sandbox when MSBuild FileTracker access required elevation.

## 6. Risk Assessment

Risk level: low

Why:
- No pipeline resequencing
- No shader algorithm rewrite
- No DRS/FSR/TAA arbitration changes
- No API surface or cvar behavior changes
- Only semantic correction and MGPU data-consistency fix

## 7. Expected Runtime Effect

Expected improvements:
- Reduced subtle micro-vibration in lower DLSS presets caused by incorrect motion metadata.
- Improved MGPU stability/correctness for depth-driven temporal reconstruction.

Not claimed in this pass:
- elimination of all low-preset shimmer (some degradation remains normal at larger upscale ratios)
- broad visual quality retuning

## 8. Test Protocol

Use same camera path and scenes for A/B:
- sky/horizon edges
- distant geometry silhouettes
- slow pan and rotate-then-stop

Commands:
1. `pt_dlss 1`
2. `pt_dlss_quality 0`
3. `pt_dlss_quality 1`
4. `pt_dlss_quality 2`
5. `pt_dlss_quality 3`

What to watch:
- stable image lock when camera comes to rest
- reduction of fine-frame wobble in lower presets
- no regressions in Quality/Balanced

## 9. Files Changed in This Pass

- `src/refresh/vkpt/streamline_reflex.cpp`
- `src/refresh/vkpt/asvgf.c`
- `docs/DLSS_PERF_PRESET_VIBRATION_DIAGNOSIS_AND_FIX.md`

