#ifndef GUARD_GBA_SYSCALL_H
#define GUARD_GBA_SYSCALL_H

#define RESET_EWRAM      0x01
#define RESET_IWRAM      0x02
#define RESET_PALETTE    0x04
#define RESET_VRAM       0x08
#define RESET_OAM        0x10
#define RESET_SIO_REGS   0x20
#define RESET_SOUND_REGS 0x40
#define RESET_REGS       0x80
#define RESET_ALL        0xFF

void SoftReset(u32 resetFlags);

void RegisterRamReset(u32 resetFlags);

void VBlankIntrWait(void);

u16 Sqrt(u32 num);

u16 ArcTan2(s16 x, s16 y);

#define CPU_SET_SRC_FIXED 0x01000000
#define CPU_SET_16BIT     0x00000000
#define CPU_SET_32BIT     0x04000000

void CpuSet(const void *src, void *dest, u32 control);

#if MODERN
// NOTE: Assumes 16-bit CpuSets unless control is a constant and has
// CPU_SET_32BIT set.
#define CpuSet(src, dest, control) \
    do \
    { \
        _Static_assert(_Alignof(src) >= __builtin_choose_expr(__builtin_constant_p(control), ((control) & CPU_SET_32BIT) ? 4 : 2, 2), "source potentially unaligned"); \
        _Static_assert(_Alignof(dest) >= __builtin_choose_expr(__builtin_constant_p(control), ((control) & CPU_SET_32BIT) ? 4 : 2, 2), "destination potentially unaligned"); \
        CpuSet(src, dest, control); \
    } while (0)
#endif

#define CPU_FAST_SET_SRC_FIXED 0x01000000

void CpuFastSet(const void *src, void *dest, u32 control);

/* Upstream wraps CpuFastSet in _Static_asserts that src/dest are 4-aligned.
 * That holds on the GBA because -mabi=apcs-gnu aligns *every* struct to 4,
 * but not under the x86 ABI (e.g. struct ItemSlot is 2-aligned, which fails
 * in player_pc.c). The requirement is a BIOS one; the host CpuFastSet in
 * host_hal.c copies bytewise and doesn't need it, so the asserts are omitted. */

void BgAffineSet(struct BgAffineSrcData *src, struct BgAffineDstData *dest, s32 count);

void ObjAffineSet(struct ObjAffineSrcData *src, void *dest, s32 count, s32 offset);

void LZ77UnCompWram(const u32 *src, void *dest);

void LZ77UnCompVram(const u32 *src, void *dest);

void RLUnCompWram(const u32 *src, void *dest);

void RLUnCompVram(const u32 *src, void *dest);

int MultiBoot(struct MultiBootParam *mp);

s32 Div(s32 num, s32 denom);

#endif // GUARD_GBA_SYSCALL_H
