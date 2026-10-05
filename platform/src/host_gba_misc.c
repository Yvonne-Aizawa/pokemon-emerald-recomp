/*
 * platform/src/host_gba_misc.c
 *
 * Host versions of the remaining symbols that upstream defines in ARM
 * assembly or the linker script:
 *
 *   src/crt0.s             ReInitializeEWRAM (+ the EWRAM reset it pairs with)
 *   src/rom_header.s       RomHeaderGameCode, RomHeaderSoftwareVersion
 *   src/libgcnmultiboot.s  GameCubeMultiBoot_* (as if no GameCube is attached)
 *   ld_script_modern.ld    __rom_end, __iwram_end (debug-only size readouts)
 *   BIOS                   BitUnPack (SWI 0x10)
 *
 * plus the host's answers to "where is GBA RAM / ROM?" (see below).
 */

/* libc first: global.h defines function-like macros (abs, ...) that would
 * break libc's own declarations. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "libgcnmultiboot.h"
#include "main.h"

#include "platform/crash_handler.h"
#include "platform/host_game.h"

/* --------------------------------------------------------------------- */
/* EWRAM reset                                                          */
/*                                                                       */
/* EWRAM_DATA / EWRAM_INIT variables live in the "ewram_data" and        */
/* "ewram_init" sections (gba/defines.h). On hardware, a RESET_EWRAM      */
/* clears all of EWRAM and ReInitializeEWRAM then copies EWRAM_INIT's     */
/* initial values back from ROM. We keep that copy ourselves, taken      */
/* before main() runs. The symbols are weak so programs that link no      */
/* EWRAM variables still link.                                           */
/* --------------------------------------------------------------------- */

extern char __start_ewram_data[] __attribute__((weak));
extern char __stop_ewram_data[] __attribute__((weak));
extern char __start_ewram_init[] __attribute__((weak));
extern char __stop_ewram_init[] __attribute__((weak));

static unsigned char *sEwramInitImage;

static size_t SectionSize(const char *start, const char *stop)
{
    return (start != NULL && stop != NULL) ? (size_t)(stop - start) : 0;
}

__attribute__((constructor)) static void SnapshotEwramInit(void)
{
    size_t size = SectionSize(__start_ewram_init, __stop_ewram_init);

    if (size != 0 && (sEwramInitImage = malloc(size)) != NULL)
        memcpy(sEwramInitImage, __start_ewram_init, size);
}

/* Called by RegisterRamReset(RESET_EWRAM) in host_hal.c. */
void Host_ResetEwram(void)
{
    size_t size;

    if ((size = SectionSize(__start_ewram_data, __stop_ewram_data)) != 0)
        memset(__start_ewram_data, 0, size);
    if ((size = SectionSize(__start_ewram_init, __stop_ewram_init)) != 0)
        memset(__start_ewram_init, 0, size);
}

void ReInitializeEWRAM(void)
{
    size_t size = SectionSize(__start_ewram_init, __stop_ewram_init);

    if (size != 0 && sEwramInitImage != NULL)
        memcpy(__start_ewram_init, sEwramInitImage, size);
}

/* --------------------------------------------------------------------- */
/* Memory map                                                             */
/*                                                                        */
/* Game code sometimes tells ROM from RAM by address:                    */
/*  - bg.c's IsTileMapOutsideWram: is a tilemap buffer writable RAM?     */
/*    (patched to call Host_IsGbaRamPointer)                             */
/*  - script commands/specials "tagged" by adding ROM_SIZE, relying on   */
/*    the GBA's ROM mirror at 0x0A000000 to still be callable (patched   */
/*    in script.c/scrcmd.c to strip the tag), and recognised by          */
/*    (address & 0xE000000) == 0xA000000.                                */
/* The executable is non-PIE at 0x08048000, so code lands inside the     */
/* GBA's ROM window like on hardware; CheckMemoryMap makes sure it stays  */
/* there.                                                                 */
/* --------------------------------------------------------------------- */

extern char __executable_start[];
extern char etext[];
extern char __data_start[];  /* glibc: start of .data */
extern char _end[];
extern char __start_script_data[] __attribute__((weak));
extern char __stop_script_data[] __attribute__((weak));

/* The game's RAM: every writable variable -- .data, the EWRAM sections,
 * .bss -- except the event/battle scripts, which upstream keeps in a
 * writable-flagged section but which live in ROM on hardware. */
bool32 Host_IsGbaRamPointer(const void *ptr)
{
    const char *p = ptr;

    if (p < __data_start || p >= _end)
        return FALSE;
    if (__start_script_data != NULL && p >= __start_script_data && p < __stop_script_data)
        return FALSE;
    return TRUE;
}

__attribute__((constructor)) static void CheckMemoryMap(void)
{
    if ((uintptr_t)__executable_start < ROM_START || (uintptr_t)etext > ROM_START + 0x2000000)
    {
        fprintf(stderr, "pkmemerald: code at %p-%p is outside the GBA ROM window "
                        "0x08000000-0x0A000000; tagged script pointers would break. "
                        "Link non-PIE at the default i386 address.\n",
                (void *)__executable_start, (void *)etext);
        abort();
    }
}

/* --------------------------------------------------------------------- */
/* ROM header                                                            */
/* --------------------------------------------------------------------- */

/* gbafix fills these in on hardware builds: "BPEE" is English Emerald. */
const u8 RomHeaderGameCode[GAME_CODE_LENGTH] = { 'B', 'P', 'E', 'E' };
const u8 RomHeaderSoftwareVersion = 0;

/* --------------------------------------------------------------------- */
/* GameCube multiboot: no GameCube is ever attached                      */
/* --------------------------------------------------------------------- */

void GameCubeMultiBoot_Init(struct GcmbStruct *pStruct)
{
    memset(pStruct, 0, sizeof(*pStruct));
}

void GameCubeMultiBoot_Main(struct GcmbStruct *pStruct) { (void)pStruct; }
void GameCubeMultiBoot_HandleSerialInterrupt(struct GcmbStruct *pStruct) { (void)pStruct; }
void GameCubeMultiBoot_ExecuteProgram(struct GcmbStruct *pStruct) { (void)pStruct; }
void GameCubeMultiBoot_Quit(void) { }

/* --------------------------------------------------------------------- */
/* Linker-script symbols                                                 */
/*                                                                       */
/* Only used for debug readouts (debug.c's ROM size, assertf.c's free    */
/* stack), whose numbers are meaningless on the host anyway.             */
/* --------------------------------------------------------------------- */

u8 __rom_end[1];
char __iwram_end[1];

/* --------------------------------------------------------------------- */
/* BitUnPack (BIOS SWI 0x10)                                             */
/*                                                                       */
/* Widens srcWidth-bit units into destWidth-bit units, adding dataOffset */
/* to every non-zero unit (or every unit, with offsetZeros). Units are   */
/* packed from the least significant bit; output is written in 32-bit    */
/* little-endian words.                                                  */
/* --------------------------------------------------------------------- */

struct HostBitUnPackArgs
{
    u16 compressedSize;
    u8 compressedBits;
    u8 decompressedBits;
    u32 dataOffset:31;
    u32 offsetZeros:1;
};

void BitUnPack(const void *src, void *dest, const struct HostBitUnPackArgs *args)
{
    const u8 *in = src;
    u8 *out = dest;
    u32 srcWidth = args->compressedBits;
    u32 destWidth = args->decompressedBits;
    u32 srcMask = (srcWidth >= 32) ? 0xFFFFFFFFu : (1u << srcWidth) - 1;
    u32 word = 0;
    u32 wordBits = 0;
    u32 i, bit;

    for (i = 0; i < args->compressedSize; i++)
    {
        for (bit = 0; bit < 8; bit += srcWidth)
        {
            u32 unit = (in[i] >> bit) & srcMask;

            if (unit != 0 || args->offsetZeros)
                unit += args->dataOffset;
            word |= unit << wordBits;
            wordBits += destWidth;
            if (wordBits >= 32)
            {
                memcpy(out, &word, sizeof(word));
                out += sizeof(word);
                word = 0;
                wordBits = 0;
            }
        }
    }
}

/* --------------------------------------------------------------------- */
/* Crash reports: which screen/state the game was in                     */
/* --------------------------------------------------------------------- */

void Host_RegisterCrashWatches(void)
{
    Crash_WatchFunctionPointer("gMain.callback1", (void *const *)&gMain.callback1);
    Crash_WatchFunctionPointer("gMain.callback2", (void *const *)&gMain.callback2);
    Crash_WatchFunctionPointer("gMain.vblankCallback", (void *const *)&gMain.vblankCallback);
    Crash_WatchFunctionPointer("gMain.hblankCallback", (void *const *)&gMain.hblankCallback);
}
