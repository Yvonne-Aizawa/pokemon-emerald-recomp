/*
 * platform/src/host_flash.c
 *
 * Stand-in for the GBA's 1 Mbit flash save chip (refrence/src/agb_flash*.c,
 * excluded from the host build).
 *
 * The chip is emulated in memory: 32 sectors of 4 KiB, reading 0xFF when
 * erased like real flash. Saving works within a session but nothing reaches
 * disk yet; Phase 8 backs this with files under the save directory.
 */

/* libc first: global.h defines function-like macros that clash with it. */
#include <string.h>

#include "global.h"
#include "gba/flash_internal.h"
#include "agb_flash.h"

#define HOST_FLASH_SECTOR_SIZE  0x1000u
#define HOST_FLASH_SECTOR_COUNT (FLASH_ROM_SIZE_1M / HOST_FLASH_SECTOR_SIZE)

static u8 sFlash[FLASH_ROM_SIZE_1M];
static bool8 sFlashInitialized;

static u8 *SectorPtr(u16 sectorNum)
{
    return &sFlash[(u32)sectorNum * HOST_FLASH_SECTOR_SIZE];
}

static u16 HostEraseFlashChip(void)
{
    memset(sFlash, 0xFF, sizeof(sFlash));
    return 0;
}

static u16 HostEraseFlashSector(u16 sectorNum)
{
    if (sectorNum >= HOST_FLASH_SECTOR_COUNT)
        return 0x80FF;
    memset(SectorPtr(sectorNum), 0xFF, HOST_FLASH_SECTOR_SIZE);
    return 0;
}

static u16 HostProgramFlashByte(u16 sectorNum, u32 offset, u8 data)
{
    if (sectorNum >= HOST_FLASH_SECTOR_COUNT || offset >= HOST_FLASH_SECTOR_SIZE)
        return 0x8000;
    SectorPtr(sectorNum)[offset] = data;
    return 0;
}

static u16 HostProgramFlashSector(u16 sectorNum, u8 *src)
{
    if (sectorNum >= HOST_FLASH_SECTOR_COUNT)
        return 0x80FF;
    memcpy(SectorPtr(sectorNum), src, HOST_FLASH_SECTOR_SIZE);
    return 0;
}

static u16 HostWaitForFlashWrite(u8 phase, u8 *addr, u8 lastData)
{
    (void)phase;
    (void)addr;
    (void)lastData;
    return 0;
}

u16 (*ProgramFlashByte)(u16, u32, u8) = HostProgramFlashByte;
u16 (*ProgramFlashSector)(u16, u8 *) = HostProgramFlashSector;
u16 (*EraseFlashChip)(void) = HostEraseFlashChip;
u16 (*EraseFlashSector)(u16) = HostEraseFlashSector;
u16 (*WaitForFlashWrite)(u8, u8 *, u8) = HostWaitForFlashWrite;

/* Returns 0 when a supported chip is found, which is always. */
u16 IdentifyFlash(void)
{
    if (!sFlashInitialized)
    {
        HostEraseFlashChip();
        sFlashInitialized = TRUE;
    }
    return 0;
}

/* The flash timer bounds how long hardware writes may take; host writes are
 * instant. */
u16 SetFlashTimerIntr(u8 timerNum, void (**intrFunc)(void))
{
    (void)timerNum;
    (void)intrFunc;
    return 0;
}

void ReadFlash(u16 sectorNum, u32 offset, u8 *dest, u32 size)
{
    if (sectorNum >= HOST_FLASH_SECTOR_COUNT || offset + size > HOST_FLASH_SECTOR_SIZE)
        return;
    memcpy(dest, SectorPtr(sectorNum) + offset, size);
}

/* Returns 0 on success, or the address of the first byte that didn't verify. */
u32 ProgramFlashSectorAndVerify(u16 sectorNum, u8 *src)
{
    if (HostEraseFlashSector(sectorNum) != 0 || HostProgramFlashSector(sectorNum, src) != 0)
        return 1;
    return 0;
}
