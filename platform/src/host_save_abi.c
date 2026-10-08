/* Translate persisted pointer fields and alignment using both checked schemas. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "global.h"
#include "save.h"
#include "pokemon_storage_system.h"
#include "platform/host_save_abi.h"

#if __SIZEOF_POINTER__ == 8
#include "save_abi.inc"

static size_t ChunkSize(unsigned id, bool native)
{
    size_t size, offset;
    if (id == SECTOR_ID_SAVEBLOCK2)
        return sizeof(struct SaveBlock2);
    if (id <= SECTOR_ID_SAVEBLOCK1_END)
    {
        size = native ? sizeof(struct SaveBlock1) : RAW_BLOCK_SIZE;
        offset = (id - SECTOR_ID_SAVEBLOCK1_START) * SECTOR_DATA_SIZE;
    }
    else
    {
        size = sizeof(struct PokemonStorage);
        offset = (id - SECTOR_ID_PKMN_STORAGE_START) * SECTOR_DATA_SIZE;
    }
    return size > offset ? min(size - offset, (size_t)SECTOR_DATA_SIZE) : 0;
}

static u16 Sum(const u8 *data, size_t size)
{
    u32 sum = 0, word;
    size_t i;
    for (i = 0; i + 4 <= size; i += 4)
    {
        memcpy(&word, data + i, 4);
        sum += word;
    }
    return (u16)(sum + (sum >> 16));
}
#endif

bool HostSave_ConvertFlashAbi(unsigned char *flash, bool toNative, char *error, size_t errorSize)
{
#if __SIZEOF_POINTER__ == 8
    u8 *copy = malloc(SECTORS_COUNT * SECTOR_SIZE);
    u8 *block = calloc(1, sizeof(struct SaveBlock1));
    u8 *converted = calloc(1, sizeof(struct SaveBlock1));
    unsigned slot, i;
    if (!copy || !block || !converted)
    {
        free(copy); free(block); free(converted);
        snprintf(error, errorSize, "out of memory converting raw save layout");
        return false;
    }
    memcpy(copy, flash, SECTORS_COUNT * SECTOR_SIZE);
    for (slot = 0; slot < NUM_SAVE_SLOTS; slot++)
    {
        struct SaveSector sectors[NUM_SECTORS_PER_SLOT];
        unsigned ids = 0;
        u32 counter = 0;
        bool valid = true;
        memset(block, 0, sizeof(struct SaveBlock1));
        memset(converted, 0, sizeof(struct SaveBlock1));
        for (i = 0; i < NUM_SECTORS_PER_SLOT; i++)
        {
            struct SaveSector *s = &sectors[i];
            memcpy(s, copy + (slot * NUM_SECTORS_PER_SLOT + i) * SECTOR_SIZE, SECTOR_SIZE);
            if (!i) counter = s->counter;
            if (s->signature != SECTOR_SIGNATURE || s->id >= NUM_SECTORS_PER_SLOT
                || s->counter != counter || (ids & (1u << s->id))
                || s->checksum != Sum(s->data, ChunkSize(s->id, !toNative)))
            {
                valid = false;
                break;
            }
            ids |= 1u << s->id;
            if (s->id >= SECTOR_ID_SAVEBLOCK1_START && s->id <= SECTOR_ID_SAVEBLOCK1_END)
                memcpy(block + (s->id - SECTOR_ID_SAVEBLOCK1_START) * SECTOR_DATA_SIZE,
                       s->data, ChunkSize(s->id, !toNative));
        }
        /* Leave damaged/incomplete slots byte-for-byte intact. */
        if (!valid) continue;
        for (i = 0; i < ARRAY_COUNT(sSaveAbiRanges); i++)
        {
            size_t rawSize = sSaveAbiRanges[i].rawSize;
            size_t nativeSize = sSaveAbiRanges[i].nativeSize;
            const u8 *src = block + (toNative ? sSaveAbiRanges[i].raw : sSaveAbiRanges[i].native);
            u8 *dst = converted + (toNative ? sSaveAbiRanges[i].native : sSaveAbiRanges[i].raw);
            size_t j;
            if (!toNative)
                for (j = rawSize; j < nativeSize; j++)
                    if (src[j] != 0)
                    {
                        snprintf(error, errorSize, "save field does not fit the raw save format");
                        free(copy); free(block); free(converted);
                        return false;
                    }
            memcpy(dst, src, min(rawSize, nativeSize));
        }
        for (i = 0; i < NUM_SECTORS_PER_SLOT; i++)
        {
            struct SaveSector *s = &sectors[i];
            if (s->id < SECTOR_ID_SAVEBLOCK1_START || s->id > SECTOR_ID_SAVEBLOCK1_END) continue;
            memset(s->data, 0, sizeof(s->data));
            memcpy(s->data, converted + (s->id - SECTOR_ID_SAVEBLOCK1_START) * SECTOR_DATA_SIZE,
                   ChunkSize(s->id, toNative));
            s->checksum = Sum(s->data, ChunkSize(s->id, toNative));
            memcpy(copy + (slot * NUM_SECTORS_PER_SLOT + i) * SECTOR_SIZE, s, SECTOR_SIZE);
        }
    }
    memcpy(flash, copy, SECTORS_COUNT * SECTOR_SIZE);
    free(copy); free(block); free(converted);
#else
    (void)flash; (void)toNative; (void)error; (void)errorSize;
#endif
    return true;
}
