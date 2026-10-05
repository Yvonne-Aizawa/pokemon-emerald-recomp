/*
 * platform/include/platform/host_render.h
 *
 * The GBA display, emulated on the host: turns the game's video state
 * (display registers, palette RAM, VRAM, OAM) into one frame of pixels.
 */

#ifndef PLATFORM_HOST_RENDER_H
#define PLATFORM_HOST_RENDER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Draw the current frame into a PLATFORM_SCREEN_WIDTH x
 * PLATFORM_SCREEN_HEIGHT buffer of 0x00RRGGBB pixels. */
void Host_RenderFrame(uint32_t *framebuffer);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_HOST_RENDER_H */
