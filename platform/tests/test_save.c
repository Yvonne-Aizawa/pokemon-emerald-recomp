/*
 * platform/tests/test_save.c
 *
 * The file-backed flash save (host_flash.c): a blank start, writes reaching
 * disk only through Host_SaveFlush, reloading, refusing to touch a file that
 * isn't a save, and converting an old raw pkmemerald.sav.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <direct.h>
#include <process.h>
#endif

#include "global.h"
#include "gba/flash_internal.h"
#include "agb_flash.h"
#include "save.h"

#include "platform/host_save.h"

static int sFailures;

static void Check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        sFailures++;
}

static long FileSize(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

#ifdef _WIN32
/* MinGW has no mkdtemp. (Process IDs repeat, under Wine especially, so
 * count up until a name is free.) */
static char *mkdtemp(char *template)
{
    const char *temp = getenv("TEMP");
    char path[256];
    int i;

    for (i = 0; i < 1000; i++)
    {
        snprintf(path, sizeof(path), "%s\\pkmemerald-test-save-%d-%d", temp != NULL ? temp : ".", _getpid(), i);
        if (_mkdir(path) == 0)
        {
            strcpy(template, path);
            return template;
        }
    }
    return NULL;
}
#endif

int main(void)
{
    char dir[256] = "/tmp/pkmemerald-test-save-XXXXXX";
    /* Room for the directory (256) plus the longest suffix. */
    char path[512], tmpPath[512 + 8], legacyPath[512];
    const char *badJson = "{\"format\": \"pkmemerald-save\", \"version\": 1, \"game\": 5}";
    static u8 sector[0x1000], readBack[0x1000];
    FILE *f;
    int i;

    if (mkdtemp(dir) == NULL)
    {
        perror("mkdtemp");
        return 1;
    }
    snprintf(path, sizeof(path), "%s/saves/%s", dir, HOST_SAVE_FILE_NAME);
    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);
    for (i = 0; i < (int)sizeof(sector); i++)
        sector[i] = (u8)(i * 7);

    /* New save: blank chip, nothing written until the game writes. */
    snprintf(tmpPath, sizeof(tmpPath), "%s/saves", dir);
    Check(Host_SaveOpen(tmpPath), "open creates the save directory");
    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);
    ReadFlash(5, 0, readBack, 16);
    Check(readBack[0] == 0xFF && readBack[15] == 0xFF, "a new chip reads as erased (0xFF)");
    Check(Host_SaveFlush() && FileSize(path) == -1, "no file is written before the game saves");

    /* A write reaches disk on flush, atomically. (The Trainer Hill sector is
     * kept as it is; save slot contents are checked by test_json_save_format.) */
    Check(ProgramFlashSectorAndVerify(SECTOR_ID_TRAINER_HILL, sector) == 0, "program a sector");
    Check(FileSize(path) == -1, "writes stay in memory until flushed");
    Check(Host_SaveFlush() && FileSize(path) > 0, "flush writes the save");
    f = fopen(path, "rb");
    Check(f != NULL && fgetc(f) == '{', "...as JSON");
    if (f != NULL)
        fclose(f);
    Check(access(tmpPath, F_OK) != 0, "no temporary file is left behind");

    /* Reload. */
    snprintf(tmpPath, sizeof(tmpPath), "%s/saves", dir);
    Check(Host_SaveOpen(tmpPath), "reopen the existing save");
    ReadFlash(SECTOR_ID_TRAINER_HILL, 0, readBack, sizeof(readBack));
    Check(memcmp(readBack, sector, sizeof(sector)) == 0, "sector contents survive a reload");
    ReadFlash(SECTOR_ID_RECORDED_BATTLE, 0, readBack, 16);
    Check(readBack[0] == 0xFF, "untouched sectors stay erased");

    /* Inspection detaches persistence, even if emulated flash is changed. */
    Check(Host_SaveOpenReadOnly(path), "load a read-only save");
    ReadFlash(SECTOR_ID_TRAINER_HILL, 0, readBack, sizeof(readBack));
    Check(memcmp(readBack, sector, sizeof(sector)) == 0, "read-only load preserves flash contents");
    memset(readBack, 0, sizeof(readBack));
    ProgramFlashSectorAndVerify(SECTOR_ID_TRAINER_HILL, readBack);
    Check(Host_SaveFlush(), "read-only flush is harmless");
    Check(Host_SaveOpenReadOnly(path), "reload untouched file after emulated write");
    ReadFlash(SECTOR_ID_TRAINER_HILL, 0, readBack, sizeof(readBack));
    Check(memcmp(readBack, sector, sizeof(sector)) == 0, "read-only writes never reach disk");

    /* A file that isn't a save is never overwritten. */
    f = fopen(path, "wb");
    fputs("not a save", f);
    fclose(f);
    Check(!Host_SaveOpen(tmpPath), "a file that isn't a save disables saving");
    ProgramFlashSectorAndVerify(0, sector);
    Host_SaveFlush();
    Check(FileSize(path) == 10, "...and is left untouched");
    f = fopen(path, "wb");
    fputs(badJson, f);
    fclose(f);
    Check(!Host_SaveOpen(tmpPath), "an invalid JSON save disables saving");
    ProgramFlashSectorAndVerify(0, sector);
    Host_SaveFlush();
    Check(FileSize(path) == (long)strlen(badJson), "...and is left untouched");

    /* An old raw image (pkmemerald.sav) is converted, and kept. */
    remove(path);
    snprintf(legacyPath, sizeof(legacyPath), "%s/saves/%s", dir, HOST_LEGACY_SAVE_FILE_NAME);
    f = fopen(legacyPath, "wb");
    for (i = 0; i < FLASH_ROM_SIZE_1M; i++)
        fputc(i / 0x1000 == SECTOR_ID_TRAINER_HILL ? sector[i % 0x1000] : 0xFF, f);
    fclose(f);
    Check(Host_SaveOpen(tmpPath), "open an old raw save");
    ReadFlash(SECTOR_ID_TRAINER_HILL, 0, readBack, sizeof(readBack));
    Check(memcmp(readBack, sector, sizeof(sector)) == 0, "...with its contents");
    Check(Host_SaveFlush() && FileSize(path) > 0, "the first flush writes the JSON save");
    Check(FileSize(legacyPath) == FLASH_ROM_SIZE_1M, "...and keeps the old file");
    Check(Host_SaveOpen(tmpPath), "reopen: the JSON save is used");
    ReadFlash(SECTOR_ID_TRAINER_HILL, 0, readBack, sizeof(readBack));
    Check(memcmp(readBack, sector, sizeof(sector)) == 0, "...with the same contents");

    snprintf(tmpPath, sizeof(tmpPath), "rm -rf '%s'", dir);
    if (system(tmpPath) != 0)
        fprintf(stderr, "could not remove %s\n", dir);

    printf(sFailures ? "save: %d FAILED\n" : "save: all tests passed\n", sFailures);
    return sFailures != 0;
}
