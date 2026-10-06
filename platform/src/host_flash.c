/*
 * platform/src/host_flash.c
 *
 * Stand-in for the GBA's 1 Mbit flash save chip (reference/src/agb_flash*.c,
 * excluded from the host build).
 *
 * The chip is emulated in memory -- 32 sectors of 4 KiB, reading 0xFF when
 * erased like real flash -- and persisted to a file (see platform/host_save.h):
 * every erase/program marks it dirty, and Host_SaveFlush writes the image out
 * atomically. A crash mid-save then looks like a power cut mid-save on
 * hardware, which the game's two-slot save scheme (save.c) recovers from.
 */

/* libc first: global.h defines function-like macros that clash with it. */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "gba/flash_internal.h"
#include "agb_flash.h"

#include "platform/host_fs.h"
#include "platform/host_save.h"

#define HOST_FLASH_SECTOR_SIZE  0x1000u
#define HOST_FLASH_SECTOR_COUNT (FLASH_ROM_SIZE_1M / HOST_FLASH_SECTOR_SIZE)

static u8 sFlash[FLASH_ROM_SIZE_1M];
static bool8 sFlashInitialized;

static char sSavePath[4096];
static bool8 sSaveEnabled;  /* a save file is attached */
static bool8 sDirty;        /* changed since the last flush */

static u8 *SectorPtr(u16 sectorNum)
{
    return &sFlash[(u32)sectorNum * HOST_FLASH_SECTOR_SIZE];
}

static u16 HostEraseFlashChip(void)
{
    memset(sFlash, 0xFF, sizeof(sFlash));
    sDirty = TRUE;
    return 0;
}

static u16 HostEraseFlashSector(u16 sectorNum)
{
    if (sectorNum >= HOST_FLASH_SECTOR_COUNT)
        return 0x80FF;
    memset(SectorPtr(sectorNum), 0xFF, HOST_FLASH_SECTOR_SIZE);
    sDirty = TRUE;
    return 0;
}

static u16 HostProgramFlashByte(u16 sectorNum, u32 offset, u8 data)
{
    if (sectorNum >= HOST_FLASH_SECTOR_COUNT || offset >= HOST_FLASH_SECTOR_SIZE)
        return 0x8000;
    SectorPtr(sectorNum)[offset] = data;
    sDirty = TRUE;
    return 0;
}

static u16 HostProgramFlashSector(u16 sectorNum, u8 *src)
{
    if (sectorNum >= HOST_FLASH_SECTOR_COUNT)
        return 0x80FF;
    memcpy(SectorPtr(sectorNum), src, HOST_FLASH_SECTOR_SIZE);
    sDirty = TRUE;
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

/* Returns 0 when a supported chip is found, which is always. A chip that
 * Host_SaveOpen didn't load starts out blank. */
u16 IdentifyFlash(void)
{
    if (!sFlashInitialized)
    {
        memset(sFlash, 0xFF, sizeof(sFlash));
        sFlashInitialized = TRUE;
    }
    return 0;
}

/* --------------------------------------------------------------------- */
/* Save file                                                             */
/* --------------------------------------------------------------------- */

bool Host_SaveOpen(const char *saveDir)
{
    FILE *file;
    long size;

    sSaveEnabled = FALSE;
    sDirty = FALSE;
    memset(sFlash, 0xFF, sizeof(sFlash));
    sFlashInitialized = TRUE;

    if (!HostFs_MakeDir(saveDir))
    {
        fprintf(stderr, "save: cannot create directory '%s': %s; saving disabled\n", saveDir, strerror(errno));
        return false;
    }
    if (snprintf(sSavePath, sizeof(sSavePath), "%s/%s", saveDir, HOST_SAVE_FILE_NAME) >= (int)sizeof(sSavePath))
    {
        fprintf(stderr, "save: path too long; saving disabled\n");
        return false;
    }

    file = fopen(sSavePath, "rb");
    if (file == NULL)
    {
        if (errno != ENOENT)
        {
            fprintf(stderr, "save: cannot read '%s': %s; saving disabled\n", sSavePath, strerror(errno));
            return false;
        }
        printf("save: %s (new)\n", sSavePath);
        sSaveEnabled = TRUE;
        return true;
    }

    fseek(file, 0, SEEK_END);
    size = ftell(file);
    rewind(file);
    if (size != (long)sizeof(sFlash) || fread(sFlash, 1, sizeof(sFlash), file) != sizeof(sFlash))
    {
        fclose(file);
        memset(sFlash, 0xFF, sizeof(sFlash));
        fprintf(stderr, "save: '%s' is not a %u-byte flash save (size %ld); leaving it untouched, saving disabled\n",
                sSavePath, (unsigned)sizeof(sFlash), size);
        return false;
    }
    fclose(file);
    printf("save: %s\n", sSavePath);
    sSaveEnabled = TRUE;
    return true;
}

bool Host_SaveFlush(void)
{
    char error[sizeof(sSavePath) + 256];

    if (!sSaveEnabled || !sDirty)
        return true;

    if (!HostFs_WriteFileAtomic(sSavePath, sFlash, sizeof(sFlash), error, sizeof(error)))
    {
        fprintf(stderr, "save: %s\n", error);
        return false;
    }
    sDirty = FALSE;
    return true;
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
