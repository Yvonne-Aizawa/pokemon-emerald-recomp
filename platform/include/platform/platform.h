/*
 * platform/include/platform/platform.h
 *
 * Public interface of the PC platform layer: lifecycle, OS events, time.
 *
 * Phase 4: platform.c implements this with plain POSIX (no window).
 * Phase 5 replaces it with an SDL2-backed implementation.
 */

#ifndef PLATFORM_PLATFORM_H
#define PLATFORM_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct PlatformConfig
{
    const char *dataDir;    /* converted game data (assets/) */
    const char *saveDir;    /* save files (saves/) */
};

/* Lifecycle. Platform_Init returns 0 on success. */
int  Platform_Init(const struct PlatformConfig *config);
void Platform_Shutdown(void);

const char *Platform_GetDataDir(void);
const char *Platform_GetSaveDir(void);

/* Per-frame hooks. The host main loop calls these once per frame. */
void Platform_FrameBegin(void);
void Platform_FrameEnd(void);

/* Drain OS events (window/input) without blocking. */
void Platform_PollEvents(void);

/* True once the user has asked to quit (window closed, SIGINT, ...). */
bool Platform_QuitRequested(void);
void Platform_RequestQuit(void);

/* Time. */
uint32_t Platform_GetTicks(void);   /* milliseconds since Platform_Init */
uint64_t Platform_GetTimeNs(void);  /* monotonic nanoseconds since Platform_Init */
void     Platform_SleepMs(uint32_t ms);
void     Platform_SleepNs(uint64_t ns);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_PLATFORM_H */
