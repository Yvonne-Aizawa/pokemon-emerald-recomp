/* Read-only checkpoint inspection. Uses this build's upstream save layout. */
#include <stdio.h>
#include "global.h"
#include "agb_flash.h"
#include "load_save.h"
#include "save.h"
#include "pokemon.h"
#include "item.h"
#include "money.h"
#include "constants/vars.h"
#include "platform/host_save.h"

struct SaveSymbol { unsigned id; const char *name; };
#include "save_symbols.inc"

static void PrintSymbols(const struct SaveSymbol *symbols, unsigned count, unsigned id)
{
    unsigned i;
    bool first = true;
    printf("[");
    for (i = 0; i < count; i++)
        if (symbols[i].id == id)
        {
            printf("%s\"%s\"", first ? "" : ",", symbols[i].name);
            first = false;
        }
    printf("]");
}

int Host_InspectSave(const char *path)
{
    struct SaveSector sector;
    unsigned i, j;
    bool first;
    u8 status;

    if (!Host_SaveOpenReadOnly(path))
    {
        fprintf(stderr, "inspect: cannot read a 128 KiB flash save: %s\n", path);
        return 1;
    }
    /* Guard the loader's location-table indexing on malformed input. */
    for (i = 0; i < NUM_SAVE_SLOTS * NUM_SECTORS_PER_SLOT; i++)
    {
        ReadFlash(i, 0, (u8 *)&sector, sizeof(sector));
        if (sector.signature == SECTOR_SIGNATURE && sector.id >= NUM_SECTORS_PER_SLOT)
        {
            fprintf(stderr, "inspect: invalid sector id in sector %u\n", i);
            return 1;
        }
    }
    SetSaveBlocksPointers(0);
    CheckForFlashMemory();
    status = LoadGameSave(SAVE_NORMAL);
    if (status != SAVE_STATUS_OK)
    {
        fprintf(stderr, "inspect: save loader status %u (empty/corrupt saves refused)\n", status);
        return 1;
    }
    if (gSaveBlock1Ptr->playerPartyCount > PARTY_SIZE)
    {
        fprintf(stderr, "inspect: invalid party count\n");
        return 1;
    }
    printf("{\"save_counter\":%lu,\"loader_status\":%u,", (unsigned long)gSaveCounter, status);
    /* Raw game text bytes preserve names without lossy character conversion. */
    printf("\"player_name_bytes\":[");
    for (i = 0; i < sizeof(gSaveBlock2Ptr->playerName); i++)
        printf("%s%u", i ? "," : "", gSaveBlock2Ptr->playerName[i]);
    printf("],\"gender\":%u,\"map\":{\"group\":%d,\"number\":%d,\"x\":%d,\"y\":%d},",
           gSaveBlock2Ptr->playerGender, gSaveBlock1Ptr->location.mapGroup,
           gSaveBlock1Ptr->location.mapNum, gSaveBlock1Ptr->pos.x, gSaveBlock1Ptr->pos.y);
    printf("\"money\":%lu,\"party\":[", (unsigned long)GetMoney(&gSaveBlock1Ptr->money));
    for (i = 0; i < gSaveBlock1Ptr->playerPartyCount; i++)
    {
        struct Pokemon *mon = &gSaveBlock1Ptr->playerParty[i];
        unsigned species = GetMonData(mon, MON_DATA_SPECIES);
        unsigned bad = GetMonData(mon, MON_DATA_SANITY_IS_BAD_EGG);
        printf("%s{\"species\":%u,\"level\":%lu,\"hp\":%lu,\"bad_egg\":%s}",
               i ? "," : "", species, (unsigned long)GetMonData(mon, MON_DATA_LEVEL),
               (unsigned long)GetMonData(mon, MON_DATA_HP), bad ? "true" : "false");
    }
    printf("],\"bag\":[");
    first = true;
    for (i = 0; i < POCKETS_COUNT; i++)
        for (j = 0; j < gBagPockets[i].capacity; j++)
        {
            unsigned item = GetBagItemId(i, j);
            if (!item) continue;
            printf("%s{\"pocket\":%u,\"slot\":%u,\"item\":%u,\"quantity\":%u}",
                   first ? "" : ",", i, j, item, GetBagItemQuantity(i, j));
            first = false;
        }
    printf("],\"flags\":{");
    first = true;
    for (i = 0; i < FLAGS_COUNT; i++)
        if (gSaveBlock1Ptr->flags[i / 8] & (1 << (i % 8)))
        {
            printf("%s\"%u\":", first ? "" : ",", i);
            PrintSymbols(sFlagSymbols, ARRAY_COUNT(sFlagSymbols), i);
            first = false;
        }
    printf("},\"vars\":{");
    first = true;
    for (i = 0; i < VARS_COUNT; i++)
        if (gSaveBlock1Ptr->vars[i])
        {
            printf("%s\"%u\":{\"value\":%u,\"names\":", first ? "" : ",", i + VARS_START, gSaveBlock1Ptr->vars[i]);
            PrintSymbols(sVarSymbols, ARRAY_COUNT(sVarSymbols), i + VARS_START);
            printf("}");
            first = false;
        }
    printf("}}\n");
    return 0;
}
