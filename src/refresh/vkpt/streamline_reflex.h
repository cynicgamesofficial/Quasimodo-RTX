/*
 * Streamline Reflex wrapper for Q2RTX.
 *
 * C-friendly interface that isolates Streamline C++ headers from the
 * rest of the engine.  The implementation dynamically loads
 * sl.interposer.dll at runtime so the game still starts when the DLL
 * is absent.
 */

#ifndef STREAMLINE_REFLEX_H
#define STREAMLINE_REFLEX_H

#include "shared/shared.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    REFLEX_MODE_OFF       = 0,
    REFLEX_MODE_ON        = 1,
    REFLEX_MODE_ON_BOOST  = 2,
} ReflexMode;

typedef struct {
    qboolean  initialized;
    qboolean  supported;
    qboolean  lowLatencyAvailable;
    ReflexMode mode;
    uint64_t  frameCounter;
} streamline_reflex_state_t;

extern streamline_reflex_state_t sl_reflex;
extern uint64_t reflex_frame_id;

qboolean SLReflex_Init(void *vk_instance, void *vk_physical_device,
                        void *vk_device, uint32_t graphics_queue_family,
                        uint32_t graphics_queue_index);
void     SLReflex_Shutdown(void);

void     SLReflex_BeginFrame(void);
void     SLReflex_Sleep(void);
void     SLReflex_SetMode(ReflexMode mode, uint32_t frameLimitUs);

void     SLReflex_Marker_SimStart(uint64_t frameId);
void     SLReflex_Marker_SimEnd(uint64_t frameId);
void     SLReflex_Marker_RenderStart(uint64_t frameId);
void     SLReflex_Marker_RenderEnd(uint64_t frameId);
void     SLReflex_Marker_PresentStart(uint64_t frameId);
void     SLReflex_Marker_PresentEnd(uint64_t frameId);

void     SLReflex_Marker_InputSample(uint64_t frameId);
void     SLReflex_Marker_TriggerFlash(uint64_t frameId);

qboolean SLReflex_IsLowLatencyAvailable(void);

#ifdef __cplusplus
}
#endif

#endif /* STREAMLINE_REFLEX_H */
