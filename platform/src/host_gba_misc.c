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
#if PKM_ASAN
#include <sanitizer/asan_interface.h>
#endif

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
/*                                                                       */
/* Windows (PE) has no __start_/__stop_ symbols: the variables are in     */
/* .data$<section>_m, and the linker sorts .data$* by name, so empty      */
/* markers in _a and _z sections bracket them.                           */
/* --------------------------------------------------------------------- */

#ifdef _WIN32
/* The markers are empty objects, so to the compiler a memset from one of
 * them writes out of bounds (undefined: it may drop the call). Their
 * addresses are passed through an empty asm, which hides where the pointer
 * came from. */
static char *SectionBound(char *marker)
{
    __asm__("" : "+r"(marker));
    return marker;
}
#define SECTION_MARKERS(name)                                                              \
    __attribute__((used, section(".data$" #name "_a"))) static char sStartMarker_##name[0]; \
    __attribute__((used, section(".data$" #name "_z"))) static char sStopMarker_##name[0];
SECTION_MARKERS(ewram_data)
SECTION_MARKERS(ewram_init)
#define __start_ewram_data SectionBound(sStartMarker_ewram_data)
#define __stop_ewram_data  SectionBound(sStopMarker_ewram_data)
#define __start_ewram_init SectionBound(sStartMarker_ewram_init)
#define __stop_ewram_init  SectionBound(sStopMarker_ewram_init)
#else
extern char __start_ewram_data[] __attribute__((weak));
extern char __stop_ewram_data[] __attribute__((weak));
extern char __start_ewram_init[] __attribute__((weak));
extern char __stop_ewram_init[] __attribute__((weak));
#endif

static unsigned char *sEwramInitImage;

/* Integer arithmetic: the bounds are distinct objects to the compiler. */
static size_t SectionSize(const char *start, const char *stop)
{
    return (start != NULL && stop != NULL) ? (size_t)((uintptr_t)stop - (uintptr_t)start) : 0;
}

__attribute__((constructor)) static void SnapshotEwramInit(void)
{
    size_t size = SectionSize(__start_ewram_init, __stop_ewram_init);

    if (size != 0 && (sEwramInitImage = malloc(size)) != NULL)
        memcpy(sEwramInitImage, __start_ewram_init, size);
}

#if PKM_ASAN
/* PKM_ASAN_HEAP=1 (environment): the game's heap marks its unused memory
 * off-limits to AddressSanitizer (malloc.c.patch). Off by default: it mostly
 * reports upstream reading freed heap memory, which behaves the same on the
 * GBA and the PC. */
bool32 Host_AsanHeapChecks(void)
{
    static int sEnabled = -1;

    if (sEnabled < 0)
    {
        const char *value = getenv("PKM_ASAN_HEAP");

        sEnabled = value != NULL && value[0] != '\0' && strcmp(value, "0") != 0;
    }
    return sEnabled;
}
#endif

/* Called by RegisterRamReset(RESET_EWRAM) in host_hal.c. */
void Host_ResetEwram(void)
{
    size_t size;

    if ((size = SectionSize(__start_ewram_data, __stop_ewram_data)) != 0)
    {
#if PKM_ASAN
        /* The game's heap (gHeap) lives here, with its unused memory marked
         * off-limits (malloc.c.patch); the reset clears all of it, and the
         * game sets the heap up again afterwards. */
        __asan_unpoison_memory_region(__start_ewram_data, size);
#endif
        memset(__start_ewram_data, 0, size);
    }
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
/* The executable is non-PIE at 0x08048000 (Linux) or has the fixed base   */
/* 0x08000000 (Windows, see CMakeLists.txt), so code lands inside the     */
/* GBA's ROM window like on hardware; CheckMemoryMap makes sure it stays  */
/* there.                                                                 */
/* --------------------------------------------------------------------- */

#ifdef _WIN32
/* MinGW's linker script symbols, named directly (asm labels skip the     */
/* i386 C name prefix `_`). .data holds the EWRAM sections too; upstream  */
/* scripts are in their own "script_data" output section, outside both.  */
extern char __executable_start[] __asm__("__image_base__");
extern char etext[] __asm__("etext");
extern char sDataStart[] __asm__("__data_start__");
extern char sDataEnd[] __asm__("__data_end__");
extern char sBssStart[] __asm__("__bss_start__");
extern char sBssEnd[] __asm__("__bss_end__");

bool32 Host_IsGbaRamPointer(const void *ptr)
{
    uintptr_t p = (uintptr_t)ptr;

    return (p >= (uintptr_t)sDataStart && p < (uintptr_t)sDataEnd)
        || (p >= (uintptr_t)sBssStart && p < (uintptr_t)sBssEnd);
}
#else
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
#endif

__attribute__((constructor)) static void CheckMemoryMap(void)
{
    if ((uintptr_t)__executable_start < ROM_START || (uintptr_t)etext > ROM_START + 0x2000000)
    {
        fprintf(stderr, "pkmemerald: code at %p-%p is outside the GBA ROM window "
                        "0x08000000-0x0A000000; tagged script pointers would break. "
                        "Link at a fixed address in that window (see CMakeLists.txt).\n",
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
