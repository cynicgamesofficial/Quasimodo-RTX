# ReSTIR DI Milestone 1 Evaluation Results

## Visual Findings
- Frame 1: ReSTIR is noisier than legacy (expected, only 2 candidates).
- Frame 8: ReSTIR converges rapidly, but some shadow boiling is visible on polygon lights.
- Frame 32: Both paths are visually similar, with minor differences in emissive and dynamic light regions.
- Frame 128: Nearly identical, but ReSTIR may show slight ghosting at disocclusion edges and reduced sky polygon illumination (by design).

## Performance Findings
- ReSTIR adds two compute passes (initial, temporal) per frame.
- No significant performance regression observed at 1280x720 on RTX 4060; frame times remain stable.
- Profiler markers confirm correct pass sequencing and timing.

## Correctness Findings
- Reservoirs are correctly packed/unpacked and reused temporally.
- Target function matches legacy for all light types except sky polygons (excluded by design).
- RNG streams and candidate selection match plan.
- No first-frame garbage; temporal reuse skips when invalid.

## Confirmed Failure Modes
- F1: Noisier than legacy on frame 1 (confirmed)
- F3: Shadow boiling on polygon lights (confirmed)
- F5: Missing sky polygon light illumination (confirmed)
- F4: Ghosting at disocclusion edges (minor, observed)
- F2, F6, F7, F8: Not significant in this test scene

## Recommended Patch 4
Implement a debug visualization mode for reservoir contents (e.g., show M, W, or candidate selection as color overlay) to aid in diagnosing failure modes and tuning temporal reuse.

## Why That Patch 4 Beats The Alternatives
- Directly exposes reservoir state for visual debugging, making it easier to identify and fix issues like shadow boiling, ghosting, or candidate starvation.
- Safer and more informative than adding more cvars or logging, as it provides immediate feedback in context.
- Minimal code risk: can be implemented as a conditional branch in the shading path, toggled by a debug cvar.
