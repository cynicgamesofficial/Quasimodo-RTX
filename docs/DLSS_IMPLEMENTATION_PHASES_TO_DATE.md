# Q2RTX DLSS SR Integration: Completed Phases and Finalization Roadmap

Date: 2026-03-13  
Workspace: `D:\DISILLUSION\DE- ENGINES\Q2RTX`

## Scope

This document summarizes:

1. What has already been implemented for DLSS SR in this branch
2. What was fixed during bring-up and stabilization
3. What remains to reach a production-quality ("final") DLSS integration

The goal is practical engineering clarity, not marketing status.

## Executive Status

DLSS SR is integrated and rendering at the correct seam (post-interleave, pre-bloom/tonemap), with Streamline manual-hooking and feature requirements wired correctly.  
The earlier Streamline context failure (`kFeatureDLSS context is missing`) has been fixed.

Recent stabilization work also removed mixed resolution policy behavior while DLSS is active and cleaned old FSR-facing menu controls that caused debugging confusion.

This is beyond bring-up, but not yet final-quality.

## Phase History (Completed)

### Phase 0 - Streamline/Reflex Baseline (pre-DLSS)

Existing branch work established a valid Streamline foundation:

- Reflex + PCL integration
- Manual hooking path
- Present/acquire interception path
- Debug overlay path enablement
- Queue/device registration fixes

This foundation is a hard prerequisite for DLSS SR integration.

### Phase 1 - DLSS SR Architecture Audit and Seam Decision

The renderer was audited to map the real frame order and pick the DLSS seam.

Key outcome:

- Correct DLSS insertion seam is after `vkpt_interleave` and before bloom/tonemap.
- Post-tonemap FSR seam is not acceptable for DLSS SR.

### Phase 2 - Minimal DLSS SR Integration (First Working Frames)

Implemented core phase-1 DLSS path:

- DLSS feature loaded via Streamline
- DLSS Vulkan requirements queried pre-device and merged into Vulkan creation
- DLSS evaluate inserted at scene-upscale seam
- Legacy conflicting paths gated while DLSS is active:
  - FSR pass skipped
  - TAAU upscale path bypassed
  - DRS feedback loop disabled
- Per-frame fallback to TAA if DLSS evaluate fails
- Initial reset/jitter/camera constants wiring

Primary files:

- `src/refresh/vkpt/main.c`
- `src/refresh/vkpt/streamline_reflex.cpp`
- `src/refresh/vkpt/streamline_reflex.h`

### Phase 3 - Bring-up Failure Fix (`kFeatureDLSS context is missing`)

Root issue was Streamline runtime/feature context bring-up integrity, not broad renderer design.

Fixes included:

- Plugin discovery/path wiring improvements
- Safer Streamline identity setup behavior
- Restored DLSS/ImGUI diagnostic visibility
- Improved runtime diagnostics for load/setup/evaluate path

Primary file:

- `src/refresh/vkpt/streamline_reflex.cpp`

### Phase 4 - Resolution Policy Stabilization + Menu Cleanup (current)

This phase addressed a key debugging/behavior issue: DLSS-active sizing could fall back into legacy fixed/DRS policy when optimal settings were not available in a frame.

Implemented changes:

- Enforced one authoritative DLSS-active render-size path
- If `slDLSSGetOptimalSettings` is unavailable/invalid, fallback is explicit native-size render (not legacy fixed/DRS scale)
- Added concise DLSS sizing diagnostics showing:
  - output/display resolution
  - active render resolution
  - allocation extent
  - source (`optimal` vs `native_fallback`)
- Added one-time log that legacy fixed/dynamic resolution controls are ignored while DLSS is active
- Moved feedback resolution-scale assignment to use updated frame render extent
- Hid obsolete AMD FSR menu controls
- Hid resolution submenu entry that exposed old fixed/dynamic scaling controls used for FSR-era workflows

Primary file changes:

- `src/refresh/vkpt/main.c`
- `baseq2/q2rtx.menu`

## Current Technical State (What is Working)

### Streamline/DLSS Runtime

- Streamline initializes with required features (Reflex/PCL/DLSS/ImGUI)
- DLSS requirements are queried and merged before Vulkan device creation
- DLSS feature functions resolve and evaluate path executes
- Streamline overlay/console diagnostics are available again

### Render Pipeline Placement

DLSS path is active at:

`interleave -> DLSS evaluate -> bloom -> tonemap -> final blit -> HUD/UI -> present`

This matches the intended integration seam.

### Legacy Path Arbitration

When DLSS is active:

- FSR pass is not run
- TAAU upscale path is bypassed
- DRS regulator does not steer render scale

### Sizing Policy (now)

DLSS-active sizing is now coherent:

- Output size = swapchain/display size
- Render size = DLSS optimal render size (or explicit native fallback)
- Allocation size = tracked separately and logged distinctly

## What Is Still Provisional / Not Final

These are the remaining technical gaps before calling integration "final quality":

### 1) Depth Semantics for DLSS (High priority)

Current depth source is still provisional.  
Need to validate that the tagged depth matches DLSS expectations (range, semantic meaning, frame consistency) across difficult content.

### 2) Motion Vector Fidelity (High priority)

Need a dedicated verification pass for:

- space conventions
- jitter interaction
- scale correctness
- disocclusion behavior

DLSS can "run" with imperfect vectors but quality will regress.

### 3) Exposure Strategy (Medium-high priority)

Current path uses auto-exposure for safety.  
Need to decide if project should provide explicit exposure for better consistency, and validate against HDR/SDR transitions.

### 4) History Reset Policy Hardening (Medium-high priority)

Resets exist, but final policy must be audited for all discontinuities:

- map transitions
- camera cuts/teleports
- resize/swapchain rebuild
- mode toggles
- temporal invalidation events

### 5) Output Target Semantics Cleanup (Medium priority)

DLSS currently writes to the buffer expected by downstream post-process.  
Need to confirm there are no hidden TAA assumptions tied to that target in edge paths.

### 6) DRS + DLSS Coexistence Policy (Medium priority)

Current choice is intentionally strict (DRS disabled while DLSS active).  
For final integration, decide whether to:

- keep this policy permanently for simplicity
- or implement bounded dynamic resolution using DLSS min/max optimal settings

### 7) Productization/UI Completeness (Medium priority)

Missing final user-facing work:

- clear DLSS mode UI exposure
- disable/hide incompatible controls contextually
- polished status text/tooltips

### 8) Extended Feature Scope (Lower priority for SR completion)

Not part of SR finalization, should be separate tracks:

- DLAA mode
- Frame Generation
- Ray Reconstruction

## Remaining Phases to Reach Final Integration

### Phase 5 - Correctness Hardening (must-do)

1. Depth source audit and fix
2. Motion vector and jitter conformance audit
3. Reset policy completion
4. Exposure strategy decision and implementation
5. Validate matrix/constant correctness against problematic camera motion

Exit criteria:

- No major ghosting/disocclusion regressions attributable to bad inputs
- Stable behavior across map loads, resizes, and mode toggles

### Phase 6 - Quality and Performance Validation (must-do)

1. Scene-by-scene artifact testing (high motion, foliage-like detail, thin geometry, emissive-heavy scenes)
2. Performance profiling across quality modes and common resolutions
3. Verify overlay counters and runtime telemetry match expected behavior

Exit criteria:

- Predictable quality scaling by mode
- No unexplained frame-time spikes tied to DLSS path

### Phase 7 - UX/Productization + Regression Safety (must-do)

1. Final UI policy for DLSS controls and incompatible controls
2. Better fallback reason reporting when DLSS is unavailable/fails
3. Add regression checklist for future renderer changes touching DLSS inputs

Exit criteria:

- User controls are coherent
- Failure modes are understandable
- Future maintenance risk reduced

## Practical "Done vs Left" Summary

### Done

- Correct seam integration
- Streamline requirements/init/evaluate path
- Bring-up context failure fixed
- Legacy path arbitration (FSR/TAAU/DRS) for DLSS-active mode
- DLSS-active resolution policy stabilization
- Old FSR-facing menu controls hidden
- Resolution submenu entry with old fixed/dynamic scale controls hidden

### Left

- Depth/motion/exposure correctness hardening
- Reset policy completion and edge-case validation
- Final quality/performance qualification
- Final UX/productization pass

## Recommended Immediate Next Work Order

1. Depth semantics validation and correction
2. Motion vector + jitter conformance validation
3. Exposure policy decision (auto-only vs explicit handoff)
4. Reset event coverage audit
5. Per-map artifact sweep with capture protocol
6. Final UI mode/status polish

## Related Repo Documentation

- `docs/DLSS_PHASE1_IMPLEMENTATION.md`
- `docs/STREAMLINE_DLSS_CONTEXT_FIX_LOG.md`
- `docs/STREAMLINE_REFLEX.md`
