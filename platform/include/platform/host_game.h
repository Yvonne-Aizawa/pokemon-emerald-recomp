/*
 * platform/include/platform/host_game.h
 *
 * Entry points into the game (refrence/src/main.c, patched by
 * platform/patches/main.c.patch). Declared here so the platform layer can
 * call them without pulling in the game's headers.
 */

#ifndef PLATFORM_HOST_GAME_H
#define PLATFORM_HOST_GAME_H

#ifdef __cplusplus
extern "C" {
#endif

/* Boot the game: hardware/engine init, up to (not including) the main loop. */
void AgbMain(void);

/* One frame: the game's main-loop iteration, then its V-count and V-blank
 * interrupt handlers. Register with Host_SetFrameCallback. */
void HostMain_RunFrame(void);

/* Just the V-count and V-blank interrupt handlers. */
void HostMain_RaiseVBlankInterrupts(void);

/* Called by the BIOS VBlankIntrWait (host_hal.c), i.e. when game code waits
 * for V-blank from inside a frame (crash screen, debug tools). It should
 * finish the frame: interrupts, display, input, timing. NULL: returns at once. */
extern void (*gHostVBlankIntrWaitHandler)(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_HOST_GAME_H */
