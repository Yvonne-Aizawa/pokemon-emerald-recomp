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
 */

/* libc first: global.h defines function-like macros (abs, ...) that would
 * break libc's own declarations. */
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "libgcnmultiboot.h"
#include "main.h"

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
