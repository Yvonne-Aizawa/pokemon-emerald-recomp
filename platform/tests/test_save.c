/*
 * platform/tests/test_save.c
 *
 * The file-backed flash save (host_flash.c): a blank start, writes reaching
 * disk only through Host_SaveFlush, reloading, and refusing to touch a file
 * that isn't a flash save.
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
    char path[256], tmpPath[256];
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

    /* A write reaches disk on flush, atomically. */
    Check(ProgramFlashSectorAndVerify(3, sector) == 0, "program a sector");
    Check(FileSize(path) == -1, "writes stay in memory until flushed");
    Check(Host_SaveFlush() && FileSize(path) == FLASH_ROM_SIZE_1M, "flush writes the 128 KiB image");
    Check(access(tmpPath, F_OK) != 0, "no temporary file is left behind");

    /* Reload. */
    snprintf(tmpPath, sizeof(tmpPath), "%s/saves", dir);
    Check(Host_SaveOpen(tmpPath), "reopen the existing save");
    ReadFlash(3, 0, readBack, sizeof(readBack));
    Check(memcmp(readBack, sector, sizeof(sector)) == 0, "sector contents survive a reload");
    ReadFlash(4, 0, readBack, 16);
    Check(readBack[0] == 0xFF, "untouched sectors stay erased");

    /* A file of the wrong size is never overwritten. */
    f = fopen(path, "wb");
    fputs("not a save", f);
    fclose(f);
    Check(!Host_SaveOpen(tmpPath), "a wrong-size file disables saving");
    ProgramFlashSectorAndVerify(0, sector);
    Host_SaveFlush();
    Check(FileSize(path) == 10, "...and is left untouched");

    snprintf(tmpPath, sizeof(tmpPath), "rm -rf '%s'", dir);
    if (system(tmpPath) != 0)
        fprintf(stderr, "could not remove %s\n", dir);

    printf(sFailures ? "save: %d FAILED\n" : "save: all tests passed\n", sFailures);
    return sFailures != 0;
}
