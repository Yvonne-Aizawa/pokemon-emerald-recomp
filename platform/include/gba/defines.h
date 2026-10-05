/*
 * platform/include/gba/defines.h
 *
 * Host-side replacement for `refrence/include/gba/defines.h`.
 *
 * Differences from the GBA original:
 *   - Section attributes (IWRAM_DATA, EWRAM_DATA, COMMON_DATA, ARM_FUNC, ...)
 *     are no-ops on the host: we don't have a linker that places symbols in
 *     0x02000000 / 0x03000000 ranges, and we don't compile for the ARM target.
 *   - REG_BASE is redirected to point into a host-side `g_host_mmio[]` array
 *     (defined in platform/src/host_hal.c) so every `REG_*` macro keeps
 *     working but reads/writes host memory instead of physical GBA I/O.
 *   - EWRAM, IWRAM, VRAM, OAM, PLTT (and their *_ADDR/SIZE cousins) are
 *     redirected to host-side byte arrays of the same nominal sizes.
 *   - The original "address" constants (EWRAM_START 0x02000000 etc.) are kept
 *     as integer constants for any code that does range checks; those checks
 *     are simply always-false on host, which is harmless for Phase 2 and
 *     surfaced in Phase 3 when we exercise them.
 *
 * Type aliases (`u8`, `vu16`, `s32`, `bool8`, ...) are defined in gba/types.h.
 * This file does not depend on gba/types.h to avoid a cycle.
 */

#ifndef PLATFORM_GBA_DEFINES_H
#define PLATFORM_GBA_DEFINES_H

#include <stddef.h>
#include <stdint.h>

/* --------------------------------------------------------------------- */
/* Booleans                                                              */
/* --------------------------------------------------------------------- */
#define TRUE  1
#define FALSE 0

/* --------------------------------------------------------------------- */
/* Section / linkage attributes — no-ops on host                        */
/* --------------------------------------------------------------------- */
#define IWRAM_DATA   /* empty: host .bss */
#define EWRAM_DATA   /* empty: host .bss */
#define IWRAM_INIT   /* empty */
#define EWRAM_INIT   /* empty */
#define COMMON_DATA  /* empty */
#define UNUSED          __attribute__((unused))
#define USED            __attribute__((used))
#define KEEP_SECTION    __attribute__((section(".text.consts")))
#define DEPRECATED(msg) __attribute__((deprecated(msg)))

/* ARM-only target attribute — host builds are x86_64 / aarch64, so this is
 * a no-op. The original was needed because some functions must run on the
 * ARM7TDMI's IWRAM for speed; on host we have caches and don't care. */
#define ARM_FUNC

/* These were already conditional on MODERN in the original. Keep them. */
#define NOINLINE         __attribute__((noinline))
#define ALIGNED(n)       __attribute__((aligned(n)))
#define PACKED           __attribute__((packed))
#define TRANSPARENT      __attribute__((__transparent_union__))
#define ALWAYS_INLINE    inline __attribute__((always_inline))
#define NONNULL          __attribute__((__nonnull__))

/* --------------------------------------------------------------------- */
/* MMIO and memory regions                                              */
/* --------------------------------------------------------------------- */

/* Host-side backing arrays. Defined in platform/src/host_hal.c. */
extern uint8_t  g_host_ewram[];     /* 256 KiB, mirrors EWRAM */
extern uint8_t  g_host_iwram[];     /*  32 KiB, mirrors IWRAM */
extern uint8_t  g_host_vram[];      /*  96 KiB, mirrors VRAM  */
extern uint8_t  g_host_oam[];       /*   1 KiB, mirrors OAM   */
extern uint8_t  g_host_palette[];   /*   1 KiB, mirrors PLTT  */
extern volatile uint16_t g_host_mmio[]; /*  4 KiB, mirrors IO regs 0x04000000 */

/* Memory region base addresses. On the GBA these are plain integers
 * (`#define VRAM 0x6000000`), and the game relies on that: it does integer
 * arithmetic on them and mixes them with pointers, e.g. menu.c's
 *     void *addr = (void *)(charBase * 0x4000 + ...);
 *     RequestDma3Fill(..., VRAM + addr, ...);
 * so we keep them integers too -- the host address of the backing array as a
 * uintptr_t. Code that dereferences them casts to a pointer first, exactly as
 * on hardware (`*(u16 *)PLTT = RGB_WHITE;`). */
#define EWRAM      ((uintptr_t)g_host_ewram)
#define IWRAM      ((uintptr_t)g_host_iwram)
#define VRAM       ((uintptr_t)g_host_vram)
#define OAM        ((uintptr_t)g_host_oam)
#define PLTT       ((uintptr_t)g_host_palette)
#define BG_PLTT    PLTT
#define OBJ_PLTT   (PLTT + BG_PLTT_SIZE)

/* Address constants — kept for any range-check logic. On host these are
 * just numbers; runtime comparisons will be false but the code still
 * compiles. We don't try to map host addresses into the GBA layout. */
#define EWRAM_START 0x02000000u
#define EWRAM_END   (EWRAM_START + 0x40000u)
#define IWRAM_START 0x03000000u
#define IWRAM_END   (IWRAM_START + 0x8000u)
#define ROM_START   0x08000000u
#define ROM_END     0x0A000000u

/* Sizes. Match the GBA layout exactly so a host `memset(VRAM, 0, VRAM_SIZE)`
 * clears the same number of bytes. */
#define EWRAM_SIZE   0x40000u
#define IWRAM_SIZE   0x8000u
#define BG_PLTT_SIZE 0x200u
#define OBJ_PLTT_SIZE 0x200u
#define PLTT_SIZE    (BG_PLTT_SIZE + OBJ_PLTT_SIZE)

/* VRAM layout. Mirrors the GBA's split into text-mode (0x06000000-0x06010000)
 * and bitmap-mode (0x06010000-0x06018000) regions, plus a small OBJ region
 * accessed as 16-bit and 32-bit chunks. */
#define VRAM_SIZE         0x18000u
#define BG_VRAM           VRAM
#define BG_VRAM_SIZE      0x10000u
#define BG_CHAR_SIZE      0x4000u
#define BG_SCREEN_SIZE    0x800u
#define BG_CHAR_ADDR(n)   (BG_VRAM + (BG_CHAR_SIZE * (n)))
#define BG_SCREEN_ADDR(n) (BG_VRAM + (BG_SCREEN_SIZE * (n)))
#define BG_TILE_H_FLIP(n) (0x400u + (n))
#define BG_TILE_V_FLIP(n) (0x800u + (n))
#define NUM_BACKGROUNDS   4

/* text-mode BG */
#define OBJ_VRAM0         (VRAM + 0x10000u)
#define OBJ_VRAM0_SIZE    0x8000u
/* bitmap-mode BG */
#define OBJ_VRAM1         (VRAM + 0x14000u)
#define OBJ_VRAM1_SIZE    0x4000u

#define OAM_SIZE          0x400u
#define ROM_HEADER_SIZE   0xC0u

/* --------------------------------------------------------------------- */
/* Display dimensions                                                    */
/* --------------------------------------------------------------------- */
#define TILE_WIDTH   8
#define TILE_HEIGHT  8
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  160
#define DISPLAY_TILE_WIDTH  (DISPLAY_WIDTH  / TILE_WIDTH)
#define DISPLAY_TILE_HEIGHT (DISPLAY_HEIGHT / TILE_HEIGHT)

/* Tile sizes in bytes. */
#define TILE_SIZE(bpp)   ((bpp) * TILE_WIDTH * TILE_HEIGHT / 8)
#define TILE_SIZE_1BPP   TILE_SIZE(1)
#define TILE_SIZE_4BPP   TILE_SIZE(4)
#define TILE_SIZE_8BPP   TILE_SIZE(8)
#define TILE_OFFSET_4BPP(n) ((n) * TILE_SIZE_4BPP)
#define TILE_OFFSET_8BPP(n) ((n) * TILE_SIZE_8BPP)
#define TOTAL_OBJ_TILE_COUNT 1024u

/* Palette sizes. */
#define PLTT_SIZEOF(n)   ((n) * sizeof(uint16_t))
#define PLTT_SIZE_4BPP   PLTT_SIZEOF(16)
#define PLTT_SIZE_8BPP   PLTT_SIZEOF(256)
#define PLTT_OFFSET_4BPP(n) ((n) * PLTT_SIZE_4BPP)

/* --------------------------------------------------------------------- */
/* I/O register base — this is what makes REG_* work.                    */
/* --------------------------------------------------------------------- */
#define REG_BASE ((uintptr_t)g_host_mmio)

/* --------------------------------------------------------------------- */
/* Audio / interrupt pointers — Phase 13 (audio) and Phase 5 (input)     */
/* replace these with real state. For now we just provide enough to link. */
/* --------------------------------------------------------------------- */
/* On hardware these are fixed IWRAM words read by the BIOS/IRQ handler and
 * written by the game (main.c assigns both), so they must be lvalues. */
extern void *g_host_sound_info;
extern uint16_t g_host_intr_check;
extern void *g_host_intr_vector;
#define SOUND_INFO_PTR (*(struct SoundInfo **)&g_host_sound_info)
#define INTR_CHECK     g_host_intr_check
#define INTR_VECTOR    g_host_intr_vector

#endif /* PLATFORM_GBA_DEFINES_H */
