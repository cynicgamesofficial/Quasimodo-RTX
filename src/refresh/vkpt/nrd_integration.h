/*
 * NRD (NVIDIA Real-Time Denoisers) integration wrapper for Quasimodo-RTX.
 *
 * C-friendly interface that isolates NRD C++ headers from the rest of
 * the engine.  Uses the direct NRD API (no NRI dependency).
 *
 * Phase 0: scaffolding only — create/destroy NRD instance.
 */

#ifndef NRD_INTEGRATION_H
#define NRD_INTEGRATION_H

#include "shared/shared.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise NRD: create instance with RELAX_DIFFUSE_SPECULAR.
 * Safe to call when NRD is not compiled in (returns qfalse).
 * Does NOT allocate GPU resources — that happens in later phases.
 */
qboolean vkpt_nrd_init(uint16_t render_width, uint16_t render_height);

/* Tear down the NRD instance and free all CPU-side state. */
void     vkpt_nrd_destroy(void);

/* Returns qtrue when the NRD instance is live and ready for use. */
qboolean vkpt_nrd_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* NRD_INTEGRATION_H */
