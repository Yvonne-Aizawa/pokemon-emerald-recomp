/*
 * platform/include/gba/gba.h
 *
 * Host-side replacement for `refrence/include/gba/gba.h`.
 *
 * Pulls in the host stubs for every GBA HAL header. The include order
 * matters: defines.h is first because io_reg.h evaluates REG_BASE.
 */

#ifndef PLATFORM_GBA_GBA_H
#define PLATFORM_GBA_GBA_H

#include "gba/defines.h"      /* REG_BASE, EWRAM, IWRAM, VRAM, OAM, PLTT, ... */
#include "gba/types.h"        /* u8/16/32, vu8/16/32, struct BgCnt, struct OamData, ... */
#include "gba/io_reg.h"       /* REG_DISPCNT, REG_BG0CNT, REG_IME, ... */
#include "gba/multiboot.h"    /* struct MultiBootParam */
#include "gba/syscall.h"      /* CpuSet, CpuFastSet, LZ77UnComp*, RLUnComp*, BgAffineSet, ... */
#include "gba/macro.h"        /* CpuCopy16, DmaSet, DmaFill16, ... */
#include "gba/isagbprint.h"   /* DebugPrintf, AGB_ASSERT, ... */
#include "gba/flash_internal.h"
#include "gba/m4a_internal.h"

#endif /* PLATFORM_GBA_GBA_H */
