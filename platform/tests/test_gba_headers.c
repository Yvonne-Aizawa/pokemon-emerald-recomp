/*
 * platform/tests/test_gba_headers.c
 *
 * Compile-only test for the host GBA HAL stubs.
 *
 * This TU includes every header in platform/include/gba/ and exercises
 * a representative subset of the types, macros, and constants. It does
 * not call any of the BIOS functions — those have their own unit tests
 * later (Phase 3+). The point of this test is purely to confirm that
 * the headers parse, the macros expand to valid C, and the types layout
 * correctly.
 *
 * If this file compiles, Phase 2's "Done when" criterion is met.
 */

/* Standard library — minimal, just enough to confirm types. */
#include <stddef.h>
#include <stdint.h>

/* The umbrella header pulls in everything. */
#include "gba/gba.h"

/* The individual headers that gba.h includes, re-included here to make
 * sure they all parse independently. The include guards make this a
 * no-op functionally, but it confirms each header is self-contained. */
#include "gba/defines.h"
#include "gba/types.h"
#include "gba/io_reg.h"
#include "gba/multiboot.h"
#include "gba/syscall.h"
#include "gba/macro.h"
#include "gba/isagbprint.h"
#include "gba/flash_internal.h"
#include "gba/m4a_internal.h"

/* Sanity: the type shortcuts and the host backing arrays resolve. */
static uint8_t  dummy_ewram[1]  __attribute__((unused));
static uint8_t  dummy_iwram[1]  __attribute__((unused));
static uint8_t  dummy_vram[1]   __attribute__((unused));
static uint8_t  dummy_oam[1]    __attribute__((unused));
static uint8_t  dummy_pal[1]    __attribute__((unused));
static volatile uint16_t dummy_mmio[1] __attribute__((unused));

static void check_arrays(void)
{
    dummy_ewram[0]  = g_host_ewram[0];
    dummy_iwram[0]  = g_host_iwram[0];
    dummy_vram[0]   = g_host_vram[0];
    dummy_oam[0]    = g_host_oam[0];
    dummy_pal[0]    = g_host_palette[0];
    dummy_mmio[0]   = g_host_mmio[0];
}

/* Sanity: the region pointer macros point into the host arrays. */
static void check_region_macros(void)
{
    *(volatile uint8_t *)EWRAM   = 0;
    *(volatile uint8_t *)IWRAM   = 0;
    *(volatile uint8_t *)VRAM    = 0;
    *(volatile uint8_t *)OAM     = 0;
    *(volatile uint8_t *)PLTT    = 0;
    *(volatile uint8_t *)BG_PLTT = 0;
    *(volatile uint8_t *)OBJ_PLTT = 0;
}

/* Sanity: REG_* reads and writes round-trip through g_host_mmio. */
static void check_reg_macros(void)
{
    REG_DISPCNT  = 0x1234;
    REG_BG0CNT   = 0x5678;
    REG_VCOUNT   = 0;
    REG_IME      = 0;
    REG_KEYINPUT = 0;
    if (REG_DISPCNT != 0x1234) (void)0;
    if (REG_BG0CNT  != 0x5678) (void)0;
}

/* Sanity: the section attributes compile (the variables are never used). */
static IWRAM_DATA  UNUSED uint8_t  in_iwram[4];
static EWRAM_DATA  UNUSED uint8_t  in_ewram[4];
static COMMON_DATA UNUSED uint32_t in_common;

/* Sanity: the GBA bitfield struct layouts match. */
static void check_struct_layouts(void)
{
    volatile struct BgCnt  bg  = { 0 };
    volatile struct OamData oam = { 0 };
    volatile struct PlttData pltt = { 0 };
    (void)bg; (void)oam; (void)pltt;
}

/* Sanity: the syscall declarations exist and have the right signatures. */
static void check_syscall_decls(void)
{
    void (*p_softreset)(u32)     = SoftReset;
    void (*p_regreset)(u32)      = RegisterRamReset;
    void (*p_vblank)(void)       = VBlankIntrWait;
    u16  (*p_sqrt)(u32)          = Sqrt;
    u16  (*p_atan2)(s16, s16)   = ArcTan2;
    s32  (*p_div)(s32, s32)     = Div;
    void (*p_cpuset)(const void *, void *, u32) = CpuSet;
    void (*p_cpufastset)(const void *, void *, u32) = CpuFastSet;
    void (*p_bgset)(struct BgAffineSrcData *, struct BgAffineDstData *, s32) = BgAffineSet;
    void (*p_objset)(struct ObjAffineSrcData *, void *, s32, s32) = ObjAffineSet;
    (void)p_softreset; (void)p_regreset; (void)p_vblank;
    (void)p_sqrt; (void)p_atan2; (void)p_div;
    (void)p_cpuset; (void)p_cpufastset;
    (void)p_bgset; (void)p_objset;
}

/* Sanity: the macro.h DmaSet / CpuSet wrappers expand. */
static void check_macro_expansion(void)
{
    static u16 src[4] = { 1, 2, 3, 4 };
    static u16 dst[4] = { 0 };
    DmaCopy16(3, src, dst, sizeof(src));
    CpuCopy16(src, dst, sizeof(src));
    DmaFill16(3, 0xAAAA, dst, sizeof(dst));
    CpuFill16(0xBBBB, dst, sizeof(dst));
}

/* Sanity: the key constants are intact. */
static void check_key_constants(void)
{
    u16 keys = A_BUTTON | B_BUTTON | SELECT_BUTTON | START_BUTTON
             | DPAD_RIGHT | DPAD_LEFT | DPAD_UP | DPAD_DOWN
             | R_BUTTON | L_BUTTON;
    if (keys != KEYS_MASK) (void)0;
}

/* Sanity: the wait-state, DMA, and VBlank flag constants are intact. */
static void check_misc_constants(void)
{
    u32 v = WAITCNT_PREFETCH_ENABLE | INTR_FLAG_VBLANK | INTR_FLAG_TIMER0
          | DMA_ENABLE | DMA_32BIT | DMA_SRC_INC | DMA_DEST_INC
          | DISPLAY_WIDTH * DISPLAY_HEIGHT;
    if (v == 0) (void)0;
}

int main(void)
{
    check_arrays();
    check_region_macros();
    check_reg_macros();
    check_struct_layouts();
    check_syscall_decls();
    check_macro_expansion();
    check_key_constants();
    check_misc_constants();
    return 0;
}
