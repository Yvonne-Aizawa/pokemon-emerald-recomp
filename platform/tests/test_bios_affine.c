/*
 * platform/tests/test_bios_affine.c
 *
 * The host BgAffineSet / ObjAffineSet (host_hal.c) against known BIOS
 * results: identity, 90-degree rotation, scaling, and the reference point.
 */

#include <stdio.h>

#include "gba/gba.h"

static int sFailures;

static void Check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        sFailures++;
}

int main(void)
{
    struct BgAffineSrcData src = {
        .texX = 128 << 8, .texY = 128 << 8,   /* texture centre (8.8) */
        .scrX = 120, .scrY = 80,              /* lands on screen centre */
        .sx = 0x100, .sy = 0x100, .alpha = 0, /* 1.0x, no rotation */
    };
    struct BgAffineDstData dst;
    struct ObjAffineSrcData obj = { .xScale = 0x200, .yScale = 0x100, .rotation = 0x4000 };
    s16 matrix[4];

    BgAffineSet(&src, &dst, 1);
    Check(dst.pa == 0x100 && dst.pb == 0 && dst.pc == 0 && dst.pd == 0x100, "identity matrix");
    Check(dst.dx == (8 << 8) && dst.dy == (48 << 8), "screen (0,0) maps to texture (8,48)");

    src.alpha = 0x4000;  /* 90 degrees */
    BgAffineSet(&src, &dst, 1);
    Check(dst.pa == 0 && dst.pb == -0x100 && dst.pc == 0x100 && dst.pd == 0, "90-degree rotation");

    src.alpha = 0x40FF;  /* the BIOS ignores the angle's low byte */
    BgAffineSet(&src, &dst, 1);
    Check(dst.pa == 0 && dst.pb == -0x100 && dst.pc == 0x100 && dst.pd == 0, "only the angle's top 8 bits count");

    src.alpha = 0;
    src.sx = 0x80;       /* 0.5 -> texture steps half a pixel per screen pixel */
    BgAffineSet(&src, &dst, 1);
    Check(dst.pa == 0x80 && dst.pd == 0x100, "scaling is 8.8 fixed point");

    ObjAffineSet(&obj, matrix, 1, 2);
    Check(matrix[0] == 0 && matrix[1] == -0x200 && matrix[2] == 0x100 && matrix[3] == 0,
          "ObjAffineSet: scaled 90-degree rotation, packed with offset 2");

    printf(sFailures ? "bios_affine: %d FAILED\n" : "bios_affine: all tests passed\n", sFailures);
    return sFailures != 0;
}
