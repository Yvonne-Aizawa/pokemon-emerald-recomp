/*
 * platform/src/input_sdl2.h
 *
 * Keyboard and gamepad input for the SDL2 backend (internal to
 * platform_sdl2.c; the public API is Platform_GetButtons in platform.h).
 */

#ifndef PLATFORM_INPUT_SDL2_H
#define PLATFORM_INPUT_SDL2_H

#include <SDL.h>
#include <stdint.h>

void Input_Init(void);
void Input_Shutdown(void);

/* Feed every SDL event through here. */
void Input_HandleEvent(const SDL_Event *event);

/* Held GBA buttons (PLATFORM_BUTTON_* bits). */
uint16_t Input_GetButtons(void);

#endif /* PLATFORM_INPUT_SDL2_H */
