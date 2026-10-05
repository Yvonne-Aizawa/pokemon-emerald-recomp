/*
 * platform/include/platform/main_loop.h
 *
 * The host main loop. Replaces the GBA's interrupt-driven loop (AgbMain +
 * VBlankIntrWait): once per frame it polls OS events, calls the registered
 * frame callback (the game's frame: logic, then its V-count/V-blank
 * interrupts), presents, and sleeps until the next frame.
 */

#ifndef PLATFORM_MAIN_LOOP_H
#define PLATFORM_MAIN_LOOP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* GBA frame timing: 280896 CPU cycles at 2^24 Hz, i.e. ~59.7275 Hz. */
#define HOST_FRAME_NS 16742706u

typedef void (*HostFrameCallback)(void);

/* The callback run once per frame (HostMain_RunFrame for the game). NULL
 * disables it. */
void Host_SetFrameCallback(HostFrameCallback callback);

/* Run one frame's work. Called by Host_RunMainLoop. */
void Host_RunFrame(void);

/* Run frames until Platform_QuitRequested(), or until maxFrames have run
 * (0 = no limit). Returns the number of frames run. */
uint32_t Host_RunMainLoop(uint32_t maxFrames);

/* Frames completed since startup. */
uint32_t Host_GetFrameCount(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_MAIN_LOOP_H */
