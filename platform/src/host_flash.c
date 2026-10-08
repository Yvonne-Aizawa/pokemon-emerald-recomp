/*
 * platform/src/host_flash.c
 *
 * Stand-in for the GBA's 1 Mbit flash save chip (reference/src/agb_flash*.c,
 * excluded from the host build).
 *
 * The chip is emulated in memory -- 32 sectors of 4 KiB, reading 0xFF when
 * erased like real flash -- and persisted to a file (see platform/host_save.h):
 * every erase/program marks it dirty, and Host_SaveFlush writes it out
 * atomically, as JSON (host_save_json.c). A crash mid-save then looks like a
 * power cut mid-save on hardware, which the game's two-slot save scheme
 * (save.c) recovers from: the file then holds the raw chip.
 */

/* libc first: global.h defines function-like macros that clash with it. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "gba/flash_internal.h"
#include "agb_flash.h"

#include "platform/host_fs.h"
#include "platform/host_save.h"
#include "platform/host_save_json.h"
#include "platform/host_save_abi.h"

#define HOST_FLASH_SECTOR_SIZE  0x1000u
#define HOST_FLASH_SECTOR_COUNT (FLASH_ROM_SIZE_1M / HOST_FLASH_SECTOR_SIZE)
/* Far above any real save (about 200 KiB), so a stray huge file isn't read. */
#define MAX_SAVE_FILE_SIZE      (16L * 1024 * 1024)

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

/* The whole file in a malloc'd buffer (NUL-terminated). NULL with errno set
 * (ENOENT: no file; EFBIG: too large). */
static char *ReadWholeFile(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    char *data;
    long length;

    if (file == NULL)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        errno = EIO;
        return NULL;
    }
    if (length > MAX_SAVE_FILE_SIZE)
    {
        fclose(file);
        errno = EFBIG;
        return NULL;
    }
    data = malloc((size_t)length + 1);
    if (data == NULL || fread(data, 1, (size_t)length, file) != (size_t)length)
    {
        free(data);
        fclose(file);
        errno = EIO;
        return NULL;
    }
    fclose(file);
    data[length] = '\0';
    *size = (size_t)length;
    return data;
}

/* A JSON save, or a raw flash image (the .sav format before JSON), into
 * sFlash, which is left erased on failure. */
static bool LoadSaveData(const char *data, size_t size, char *error, size_t errorSize, struct HostSaveJsonLoad *load)
{
    size_t i = 0;

    memset(sFlash, 0xFF, sizeof(sFlash));
    memset(load, 0, sizeof(*load));
    while (i < size && (data[i] == ' ' || data[i] == '\t' || data[i] == '\n' || data[i] == '\r'))
        i++;
    if (i < size && data[i] == '{')
    {
        if (HostSaveJson_ToFlash(data, size, sFlash, error, errorSize, load))
            return true;
        /* A raw image can start with '{' too; it never parses as JSON. */
        if (size != sizeof(sFlash))
            return false;
    }
    if (size == sizeof(sFlash))
    {
        memcpy(sFlash, data, size);
        return HostSave_ConvertFlashAbi(sFlash, true, error, errorSize);
    }
    snprintf(error, errorSize, "neither a JSON save nor a %u-byte flash image (size %lu)",
             (unsigned)sizeof(sFlash), (unsigned long)size);
    return false;
}

/* A save from another game version (field layout) is kept as it was, once,
 * before the game can overwrite it: pkmemerald.<layout>.json.bak. */
static bool BackUpOtherLayout(const char *saveDir, const char *data, size_t size, const struct HostSaveJsonLoad *load)
{
    char path[sizeof(sSavePath)], error[sizeof(sSavePath) + 256];
    const char *name = load->fingerprint;
    FILE *existing;
    size_t i;

    /* The name comes from the file: use it only if it's the hex it should be. */
    for (i = 0; name[i] != '\0'; i++)
        if (!((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'a' && name[i] <= 'f')))
            break;
    if (i == 0 || name[i] != '\0')
        name = "other";
    if (snprintf(path, sizeof(path), "%s/pkmemerald.%s.json.bak", saveDir, name) >= (int)sizeof(path))
    {
        fprintf(stderr, "save: path too long for a backup; saving disabled\n");
        return false;
    }
    printf("save: this save is from another game version; %u thing(s) it has were dropped (see above)\n",
           load->dropped);
    existing = fopen(path, "rb");
    if (existing != NULL)
    {
        fclose(existing);
        printf("save: the earlier backup %s is kept\n", path);
        return true;
    }
    if (!HostFs_WriteFileAtomic(path, data, size, error, sizeof(error)))
    {
        fprintf(stderr, "save: cannot back up the save before it changes (%s); saving disabled\n", error);
        return false;
    }
    printf("save: backed up the save as it was to %s\n", path);
    return true;
}

bool Host_SaveOpen(const char *saveDir)
{
    char legacyPath[sizeof(sSavePath)];
    char error[512];
    const char *path = sSavePath;
    struct HostSaveJsonLoad load;
    char *data;
    size_t size;

    sSaveEnabled = FALSE;
    sDirty = FALSE;
    memset(sFlash, 0xFF, sizeof(sFlash));
    sFlashInitialized = TRUE;

    if (!HostFs_MakeDir(saveDir))
    {
        fprintf(stderr, "save: cannot create directory '%s': %s; saving disabled\n", saveDir, strerror(errno));
        return false;
    }
    if (snprintf(sSavePath, sizeof(sSavePath), "%s/%s", saveDir, HOST_SAVE_FILE_NAME) >= (int)sizeof(sSavePath)
     || snprintf(legacyPath, sizeof(legacyPath), "%s/%s", saveDir, HOST_LEGACY_SAVE_FILE_NAME) >= (int)sizeof(legacyPath))
    {
        fprintf(stderr, "save: path too long; saving disabled\n");
        return false;
    }

    data = ReadWholeFile(sSavePath, &size);
    if (data == NULL && errno == ENOENT)
    {
        /* A save from before the JSON format: converted on the first flush;
         * the old file is left as it is. */
        path = legacyPath;
        data = ReadWholeFile(legacyPath, &size);
        if (data == NULL && errno == ENOENT)
        {
            printf("save: %s (new)\n", sSavePath);
            sSaveEnabled = TRUE;
            return true;
        }
    }
    if (data == NULL)
    {
        fprintf(stderr, "save: cannot read '%s': %s; saving disabled\n", path, strerror(errno));
        return false;
    }
    if (!LoadSaveData(data, size, error, sizeof(error), &load))
    {
        free(data);
        fprintf(stderr, "save: '%s': %s; leaving it untouched, saving disabled\n", path, error);
        return false;
    }
    if (load.otherLayout && !BackUpOtherLayout(saveDir, data, size, &load))
    {
        free(data);
        memset(sFlash, 0xFF, sizeof(sFlash));
        return false;
    }
    free(data);
    if (path == legacyPath)
    {
        printf("save: %s (converting %s, which is kept)\n", sSavePath, HOST_LEGACY_SAVE_FILE_NAME);
        sDirty = TRUE;
    }
    else
    {
        printf("save: %s\n", sSavePath);
    }
    sSaveEnabled = TRUE;
    return true;
}

bool Host_SaveOpenReadOnly(const char *path)
{
    struct HostSaveJsonLoad load;
    char error[512];
    char *data;
    size_t size;
    bool ok;

    sSaveEnabled = FALSE;
    sDirty = FALSE;
    sFlashInitialized = TRUE;
    memset(sFlash, 0xFF, sizeof(sFlash));
    data = ReadWholeFile(path, &size);
    if (data == NULL)
        return false;
    ok = LoadSaveData(data, size, error, sizeof(error), &load);
    if (!ok)
        fprintf(stderr, "save: '%s': %s\n", path, error);
    free(data);
    return ok;
}

/* The flash as JSON, or as a raw image if `raw`. */
static bool WriteSave(const char *path, bool raw, char *error, size_t errorSize)
{
    char *text;
    size_t length;
    bool ok;

    if (raw)
    {
        unsigned char *copy = malloc(sizeof(sFlash));
        if (copy == NULL)
        {
            snprintf(error, errorSize, "out of memory exporting raw save");
            return false;
        }
        memcpy(copy, sFlash, sizeof(sFlash));
        ok = HostSave_ConvertFlashAbi(copy, false, error, errorSize)
            && HostFs_WriteFileAtomic(path, copy, sizeof(sFlash), error, errorSize);
        free(copy);
        return ok;
    }
    text = HostSaveJson_FromFlash(sFlash, &length);
    if (text == NULL)
    {
        snprintf(error, errorSize, "out of memory writing '%s'", path);
        return false;
    }
    ok = HostFs_WriteFileAtomic(path, text, length, error, errorSize);
    free(text);
    return ok;
}

bool Host_SaveFlush(void)
{
    char error[sizeof(sSavePath) + 256];

    if (!sSaveEnabled || !sDirty)
        return true;

    if (!WriteSave(sSavePath, false, error, sizeof(error)))
    {
        fprintf(stderr, "save: %s\n", error);
        return false;
    }
    sDirty = FALSE;
    return true;
}

int Host_ConvertSave(const char *input, const char *output)
{
    char error[sizeof(sSavePath) + 256];
    size_t length = strlen(output);
    bool raw = length >= 4 && strcmp(output + length - 4, ".sav") == 0;

    if (!Host_SaveOpenReadOnly(input))
    {
        fprintf(stderr, "convert: cannot read a save from '%s'\n", input);
        return 1;
    }
    if (!WriteSave(output, raw, error, sizeof(error)))
    {
        fprintf(stderr, "convert: %s\n", error);
        return 1;
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
