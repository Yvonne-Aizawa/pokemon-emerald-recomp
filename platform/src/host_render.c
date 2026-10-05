/*
 * platform/src/host_render.c
 *
 * GBA display emulation; see platform/host_render.h.
 *
 * Phase 5: no layers are drawn yet, so this shows what a GBA shows with
 * every layer off -- white during forced blank, otherwise the backdrop
 * colour (BG palette entry 0). That already follows the game's palette
 * changes and fades. Background layers (Phase 10) and sprites (Phase 11)
 * are drawn on top of this.
 */

#include "platform/host_render.h"
#include "platform/platform.h"

#include "global.h"

/* BGR555 (the GBA's colour format) to 0x00RRGGBB, spreading each 5-bit
 * channel over 8 bits so 31 maps to 255. */
static uint32_t ColorToRgb(u16 color)
{
    uint32_t r = color & 0x1F;
    uint32_t g = (color >> 5) & 0x1F;
    uint32_t b = (color >> 10) & 0x1F;

    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);
    return (r << 16) | (g << 8) | b;
}

void Host_RenderFrame(uint32_t *framebuffer)
{
    uint32_t fill;
    int i;

    if (REG_DISPCNT & DISPCNT_FORCED_BLANK)
        fill = 0xFFFFFF;
    else
        fill = ColorToRgb(*(const u16 *)BG_PLTT);

    for (i = 0; i < PLATFORM_SCREEN_WIDTH * PLATFORM_SCREEN_HEIGHT; i++)
        framebuffer[i] = fill;
}
