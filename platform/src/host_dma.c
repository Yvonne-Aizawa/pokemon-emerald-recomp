/*
 * platform/src/host_dma.c
 *
 * GBA DMA controller (4 channels), as driven by the DmaSet family of macros
 * in gba/macro.h.
 *
 * Immediate transfers (DMA_START_NOW) happen inside Host_DmaSet, as they do
 * on hardware (the CPU is halted until they finish). Transfers timed to
 * V-blank or H-blank, and the sound FIFO "special" timing, are stored in the
 * channel registers but not run yet: H-blank DMA (scanline effects) needs the
 * scanline renderer (Phase 10) and sound DMA the audio engine (Phase 13).
 */

#include <string.h>

#include "global.h"

/* DMAxCNT_H, i.e. the upper half of the control word DmaSet takes. */
#define CNT_DEST_MASK  0x0060
#define CNT_SRC_MASK   0x0180
#define CNT_32BIT      0x0400
#define CNT_START_MASK 0x3000
#define CNT_ENABLE     0x8000

#define ADDR_INC   0
#define ADDR_DEC   1
#define ADDR_FIXED 2
#define ADDR_RELOAD 3  /* destination only: increments during the transfer */

static void Transfer(u32 dmaNum, const u8 *src, u8 *dest, u32 cnt)
{
    u32 cntH = cnt >> 16;
    u32 count = cnt & 0xFFFF;
    u32 unit = (cntH & CNT_32BIT) ? 4 : 2;
    u32 srcMode = (cntH & CNT_SRC_MASK) >> 7;
    u32 destMode = (cntH & CNT_DEST_MASK) >> 5;
    s32 srcStep = srcMode == ADDR_DEC ? -(s32)unit : srcMode == ADDR_FIXED ? 0 : (s32)unit;
    s32 destStep = destMode == ADDR_DEC ? -(s32)unit : destMode == ADDR_FIXED ? 0 : (s32)unit;
    u32 i;

    /* A count of 0 means the maximum: 0x4000 units, or 0x10000 on DMA3. */
    if (count == 0)
        count = (dmaNum == 3) ? 0x10000 : 0x4000;

    for (i = 0; i < count; i++)
    {
        /* Unit-by-unit, like the hardware: overlapping fills and copies
         * behave as on the GBA. */
        memmove(dest, src, unit);
        src += srcStep;
        dest += destStep;
    }
}

void Host_DmaSet(u32 dmaNum, const void *src, void *dest, u32 control)
{
    vu32 *dmaRegs = (vu32 *)(REG_ADDR_DMA0 + 12 * dmaNum);
    u32 cntH = control >> 16;

    dmaRegs[0] = (u32)(uintptr_t)src;
    dmaRegs[1] = (u32)(uintptr_t)dest;
    dmaRegs[2] = control;

    if (!(cntH & CNT_ENABLE) || (cntH & CNT_START_MASK) != DMA_START_NOW)
        return;

    Transfer(dmaNum, src, dest, control);

    /* An immediate transfer clears its enable bit when done. */
    dmaRegs[2] = control & ~((u32)CNT_ENABLE << 16);
}
