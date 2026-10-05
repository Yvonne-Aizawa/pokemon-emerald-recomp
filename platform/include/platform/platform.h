/*
 * platform/include/platform/platform.h
 *
 * Public interface of the PC platform layer: lifecycle, window, OS events,
 * time. Implemented on SDL2 by platform/src/platform_sdl2.c.
 */

#ifndef PLATFORM_PLATFORM_H
#define PLATFORM_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The GBA screen. The framebuffer holds one frame as 0x00RRGGBB pixels,
 * row-major; the window shows it scaled up by an integer factor. */
#define PLATFORM_SCREEN_WIDTH  240
#define PLATFORM_SCREEN_HEIGHT 160

struct PlatformConfig
{
    const char *dataDir;    /* converted game data (assets/) */
    const char *saveDir;    /* save files (saves/) */
    int scale;              /* initial window size, in multiples of 240x160 */
};

/* Lifecycle. Platform_Init returns 0 on success. */
int  Platform_Init(const struct PlatformConfig *config);
void Platform_Shutdown(void);

const char *Platform_GetDataDir(void);
const char *Platform_GetSaveDir(void);

/* Per-frame hooks. The host main loop calls these once per frame;
 * Platform_FrameEnd presents the framebuffer. */
void Platform_FrameBegin(void);
void Platform_FrameEnd(void);

/* PLATFORM_SCREEN_WIDTH * PLATFORM_SCREEN_HEIGHT pixels, 0x00RRGGBB. */
uint32_t *Platform_GetFramebuffer(void);

/* Drain OS events (window/input) without blocking. */
void Platform_PollEvents(void);

/* True once the user has asked to quit (window closed, Ctrl-C, ...). */
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
