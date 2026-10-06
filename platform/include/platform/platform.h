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

/* The per-user directory for saves and crash reports, created if missing:
 * ~/.local/share/pkmemerald on Linux, %APPDATA%\pkmemerald on Windows. No
 * trailing separator. NULL if it can't be determined. Callable before
 * Platform_Init. */
const char *Platform_GetUserDataDir(void);

/* Per-frame hooks. The host main loop calls these once per frame;
 * Platform_FrameEnd presents the framebuffer. */
void Platform_FrameBegin(void);
void Platform_FrameEnd(void);

/* PLATFORM_SCREEN_WIDTH * PLATFORM_SCREEN_HEIGHT pixels, 0x00RRGGBB. */
uint32_t *Platform_GetFramebuffer(void);

/* GBA buttons, with the same bit values as the GBA's KEYINPUT register
 * (but 1 = pressed; the hardware register is active-low). */
#define PLATFORM_BUTTON_A      0x0001
#define PLATFORM_BUTTON_B      0x0002
#define PLATFORM_BUTTON_SELECT 0x0004
#define PLATFORM_BUTTON_START  0x0008
#define PLATFORM_BUTTON_RIGHT  0x0010
#define PLATFORM_BUTTON_LEFT   0x0020
#define PLATFORM_BUTTON_UP     0x0040
#define PLATFORM_BUTTON_DOWN   0x0080
#define PLATFORM_BUTTON_R      0x0100
#define PLATFORM_BUTTON_L      0x0200

/* Drain OS events (window/input) without blocking. */
void Platform_PollEvents(void);

/* GBA buttons currently held on the keyboard or any gamepad, as of the last
 * Platform_PollEvents. */
uint16_t Platform_GetButtons(void);

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
