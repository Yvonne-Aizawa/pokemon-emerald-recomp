/*
 * platform/include/platform/platform.h
 *
 * Public interface of the PC platform layer.
 *
 * Phase 1: declarations only; platform.c provides no-op stubs.
 * Phase 5+ will replace platform.c with an SDL2-backed implementation.
 */

#ifndef PLATFORM_PLATFORM_H
#define PLATFORM_PLATFORM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Lifecycle. */
int  Platform_Init(void);
void Platform_Shutdown(void);

/* Per-frame hooks. The host main loop calls these once per frame. */
void Platform_FrameBegin(void);
void Platform_FrameEnd(void);

/* Drain OS events (window/input) without blocking. */
void Platform_PollEvents(void);

/* Time. */
uint32_t Platform_GetTicks(void);   /* milliseconds since Platform_Init */
void     Platform_SleepMs(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_PLATFORM_H */
