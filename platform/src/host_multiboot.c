/*
 * platform/src/host_multiboot.c
 *
 * Host replacement for `refrence/src/multiboot.c`.
 *
 * Multiboot uploads a program to another GBA over the link cable (used by
 * the Berry Fix program). The upstream file drives SIO registers directly and
 * contains a hand-timed ARM busy-wait (MultiBootWaitCycles), so it is excluded
 * from the host build. These stubs behave as if no client GBA is ever
 * connected: probing never finds one and a transfer never completes.
 */

#include "global.h"
#include "multiboot.h"

#include <string.h>

void MultiBootInit(struct MultiBootParam *mp)
{
    memset(mp, 0, sizeof(*mp));
}

int MultiBootMain(struct MultiBootParam *mp)
{
    (void)mp;
    return 0;
}

void MultiBootStartProbe(struct MultiBootParam *mp)
{
    (void)mp;
}

void MultiBootStartMaster(struct MultiBootParam *mp, const u8 *srcp, int length, u8 palette_color, s8 palette_speed)
{
    (void)mp;
    (void)srcp;
    (void)length;
    (void)palette_color;
    (void)palette_speed;
}

int MultiBootCheckComplete(struct MultiBootParam *mp)
{
    (void)mp;
    return 0;
}
