/*
 * platform/src/host_save_json.c
 *
 * The JSON save file (see platform/host_save_json.h).
 *
 * Mirrors save.c's slot format: each slot is 14 sectors, sector id N holds
 * chunk N of SaveBlock2/SaveBlock1/PokemonStorage (sSaveSlotLayout) plus
 * chunk N of SaveBlock3, and a footer (id, checksum, signature, counter).
 * Which slot the game loads follows GetSaveValidStatus. Nothing here
 * touches the game's live save blocks: everything works on copies.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "gba/flash_internal.h"
#include "save.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "money.h"
#include "constants/coins.h"
#include "constants/vars.h"

#include "platform/host_json.h"
#include "platform/host_save_json.h"

struct SaveSymbol { unsigned id; const char *name; };
#define SAVE_SYMBOLS_CHARMAP
#include "save_symbols.inc"

#define FLASH_SIZE      FLASH_ROM_SIZE_1M
#define SLOT_SECTORS    NUM_SECTORS_PER_SLOT
#define ALL_SECTOR_IDS  ((1u << SLOT_SECTORS) - 1)
#define NUM_FLAG_BITS   (sizeof(((struct SaveBlock1 *)0)->flags) * 8)

STATIC_ASSERT(sizeof(struct SaveSector) == SECTOR_SIZE, HostSaveSectorSize);

/* The save blocks of one slot, as the game would load them. */
struct SaveImage
{
    struct SaveBlock2 sb2;
    struct SaveBlock1 sb1;
    struct PokemonStorage storage;
    struct SaveBlock3 sb3;
    u32 counter;
};

static const struct
{
    const char *name;
    size_t offset;
    size_t size;
} sBlocks[] = {
    { "save_block_2",    offsetof(struct SaveImage, sb2),     sizeof(struct SaveBlock2) },
    { "save_block_1",    offsetof(struct SaveImage, sb1),     sizeof(struct SaveBlock1) },
    { "pokemon_storage", offsetof(struct SaveImage, storage), sizeof(struct PokemonStorage) },
    { "save_block_3",    offsetof(struct SaveImage, sb3),     sizeof(struct SaveBlock3) },
};

/* Sectors outside the save slots, kept as whole-sector images. */
static const struct
{
    const char *name;
    u16 sector;
} sSpecialSectors[] = {
    { "hall_of_fame_1",  SECTOR_ID_HOF_1 },
    { "hall_of_fame_2",  SECTOR_ID_HOF_2 },
    { "trainer_hill",    SECTOR_ID_TRAINER_HILL },
    { "recorded_battle", SECTOR_ID_RECORDED_BATTLE },
};

/* --------------------------------------------------------------------- */
/* Flash slots (save.c)                                                  */
/* --------------------------------------------------------------------- */

/* Where in a SaveImage the part of a save block that sector `id` holds
 * starts, and its size (SAVEBLOCK_CHUNK). */
static size_t SectorChunk(u16 id, u16 *size)
{
    size_t block, total, offset;

    if (id == SECTOR_ID_SAVEBLOCK2)
    {
        block = offsetof(struct SaveImage, sb2);
        total = sizeof(struct SaveBlock2);
        offset = 0;
    }
    else if (id <= SECTOR_ID_SAVEBLOCK1_END)
    {
        block = offsetof(struct SaveImage, sb1);
        total = sizeof(struct SaveBlock1);
        offset = (size_t)(id - SECTOR_ID_SAVEBLOCK1_START) * SECTOR_DATA_SIZE;
    }
    else
    {
        block = offsetof(struct SaveImage, storage);
        total = sizeof(struct PokemonStorage);
        offset = (size_t)(id - SECTOR_ID_PKMN_STORAGE_START) * SECTOR_DATA_SIZE;
    }
    *size = total > offset ? (u16)min(total - offset, (size_t)SECTOR_DATA_SIZE) : 0;
    return block + offset;
}

/* SaveBlock3Size */
static size_t SaveBlock3ChunkSize(u16 id)
{
    size_t begin = (size_t)id * SAVE_BLOCK_3_CHUNK_SIZE;
    size_t end = min(begin + SAVE_BLOCK_3_CHUNK_SIZE, sizeof(struct SaveBlock3));
    return end > begin ? end - begin : 0;
}

/* CalculateChecksum */
static u16 Checksum(const u8 *data, u16 size)
{
    u32 sum = 0;
    u16 i;

    for (i = 0; i < size / 4; i++)
    {
        u32 word;
        memcpy(&word, data + i * 4, 4);
        sum += word;
    }
    return (u16)((sum >> 16) + sum);
}

static void ReadSector(const u8 *flash, u16 sector, struct SaveSector *out)
{
    memcpy(out, flash + (size_t)sector * SECTOR_SIZE, SECTOR_SIZE);
}

/* A sector whose signature and checksum hold. Ids past the table count as
 * bad: the game would read a size from beyond its table for them. */
static bool SectorValid(const struct SaveSector *s)
{
    u16 size;

    if (s->signature != SECTOR_SIGNATURE || s->id >= SLOT_SECTORS)
        return false;
    SectorChunk(s->id, &size);
    return s->checksum == Checksum(s->data, size);
}

enum { SLOT_EMPTY, SLOT_OK, SLOT_ERROR };

static int SlotStatus(const u8 *flash, int slot, u32 *counter)
{
    struct SaveSector s;
    bool signature = false;
    u32 ids = 0;
    int i;

    for (i = 0; i < SLOT_SECTORS; i++)
    {
        ReadSector(flash, (u16)(slot * SLOT_SECTORS + i), &s);
        if (s.signature != SECTOR_SIGNATURE)
            continue;
        signature = true;
        if (SectorValid(&s))
        {
            *counter = s.counter;
            ids |= 1u << s.id;
        }
    }
    if (!signature)
        return SLOT_EMPTY;
    return ids == ALL_SECTOR_IDS ? SLOT_OK : SLOT_ERROR;
}

enum { FLASH_EMPTY, FLASH_SLOT, FLASH_RAW };

/* What LoadGameSave would make of the flash: no save, a clean load of one
 * slot (*slot, *counter), or anything else (kept raw). */
static int ClassifyFlash(const u8 *flash, int *slot, u32 *counter)
{
    u32 counter1 = 0, counter2 = 0;
    int status1 = SlotStatus(flash, 0, &counter1);
    int status2 = SlotStatus(flash, 1, &counter2);

    if (status1 == SLOT_EMPTY && status2 == SLOT_EMPTY)
        return FLASH_EMPTY;
    if (status1 == SLOT_OK && status2 == SLOT_OK)
    {
        if ((counter1 == 0xFFFFFFFF && counter2 == 0) || (counter1 == 0 && counter2 == 0xFFFFFFFF))
            *counter = counter1 + 1 < counter2 + 1 ? counter2 : counter1;
        else
            *counter = counter1 < counter2 ? counter2 : counter1;
    }
    else if (status1 == SLOT_OK && status2 == SLOT_EMPTY)
        *counter = counter1;
    else if (status2 == SLOT_OK && status1 == SLOT_EMPTY)
        *counter = counter2;
    else
        return FLASH_RAW;  /* the game would report an error */

    /* The game loads slot counter % 2 (CopySaveSlotData). */
    *slot = (int)(*counter % NUM_SAVE_SLOTS);
    if ((*slot == 0 ? status1 : status2) != SLOT_OK)
        return FLASH_RAW;
    return FLASH_SLOT;
}

static void UnpackSlot(const u8 *flash, int slot, struct SaveImage *image)
{
    struct SaveSector s;
    int i;

    for (i = 0; i < SLOT_SECTORS; i++)
    {
        u16 size;
        size_t chunk;

        ReadSector(flash, (u16)(slot * SLOT_SECTORS + i), &s);
        if (!SectorValid(&s))
            continue;
        chunk = SectorChunk(s.id, &size);
        memcpy((u8 *)image + chunk, s.data, size);
        memcpy((u8 *)&image->sb3 + (size_t)s.id * SAVE_BLOCK_3_CHUNK_SIZE, s.saveBlock3Chunk, SaveBlock3ChunkSize(s.id));
    }
}

/* HandleWriteSector, for a whole slot: sector N of slot counter % 2 holds id N. */
static void PackSlot(struct SaveImage *image, u8 *flash)
{
    struct SaveSector s;
    u16 id;
    int slot = (int)(image->counter % NUM_SAVE_SLOTS);

    for (id = 0; id < SLOT_SECTORS; id++)
    {
        u16 size;
        const u8 *chunk = (const u8 *)image + SectorChunk(id, &size);

        memset(&s, 0, sizeof(s));
        s.id = id;
        s.signature = SECTOR_SIGNATURE;
        s.counter = image->counter;
        memcpy(s.data, chunk, size);
        memcpy(s.saveBlock3Chunk, (u8 *)&image->sb3 + (size_t)id * SAVE_BLOCK_3_CHUNK_SIZE, SaveBlock3ChunkSize(id));
        s.checksum = Checksum(chunk, size);
        memcpy(flash + (size_t)(slot * SLOT_SECTORS + id) * SECTOR_SIZE, &s, SECTOR_SIZE);
    }
}

static bool Erased(const u8 *data, size_t size)
{
    size_t i;

    for (i = 0; i < size; i++)
        if (data[i] != 0xFF)
            return false;
    return true;
}

/* --------------------------------------------------------------------- */
/* Base64                                                                */
/* --------------------------------------------------------------------- */

static const char sBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void WriteBase64(struct HostJsonWriter *w, const u8 *data, size_t size)
{
    size_t length = (size + 2) / 3 * 4, i, n = 0;
    char *text = malloc(length + 1);

    if (text == NULL)
    {
        w->failed = true;
        return;
    }
    for (i = 0; i < size; i += 3)
    {
        u32 v = (u32)data[i] << 16;
        if (i + 1 < size) v |= (u32)data[i + 1] << 8;
        if (i + 2 < size) v |= data[i + 2];
        text[n++] = sBase64[(v >> 18) & 63];
        text[n++] = sBase64[(v >> 12) & 63];
        text[n++] = i + 1 < size ? sBase64[(v >> 6) & 63] : '=';
        text[n++] = i + 2 < size ? sBase64[v & 63] : '=';
    }
    HostJsonW_String(w, text, length);
    free(text);
}

/* Exactly `size` bytes, or false. */
static bool ReadBase64(const struct HostJsonValue *v, u8 *out, size_t size)
{
    size_t i, n = 0;

    if (v == NULL || v->type != HOST_JSON_STRING || v->length != (size + 2) / 3 * 4)
        return false;
    for (i = 0; i < v->length; i += 4)
    {
        u32 value = 0;
        int j;
        for (j = 0; j < 4; j++)
        {
            char c = v->string[i + j];
            const char *p = c != '\0' ? strchr(sBase64, c) : NULL;
            if (c == '=' && i + 4 == v->length && j >= 2)
                p = sBase64;  /* padding; checked by the size below */
            else if (p == NULL)
                return false;
            value = (value << 6) | (u32)(p - sBase64);
        }
        for (j = 0; j < 3 && n < size; j++)
            out[n++] = (u8)(value >> (16 - 8 * j));
    }
    return n == size;
}

/* --------------------------------------------------------------------- */
/* Game text and names                                                   */
/* --------------------------------------------------------------------- */

/* Game text up to the 0xFF terminator, as UTF-8 via upstream's charmap;
 * bytes without a character become {XX}, so every byte string survives. */
static void WriteGameText(struct HostJsonWriter *w, const u8 *text, size_t maxLength)
{
    char out[256];
    size_t i, n = 0;

    for (i = 0; i < maxLength && text[i] != 0xFF && n + 8 < sizeof(out); i++)
    {
        size_t j;
        for (j = 0; j < ARRAY_COUNT(sCharSymbols); j++)
            if (sCharSymbols[j].id == text[i])
                break;
        if (j < ARRAY_COUNT(sCharSymbols))
            n += (size_t)snprintf(out + n, sizeof(out) - n, "%s", sCharSymbols[j].name);
        else
            n += (size_t)snprintf(out + n, sizeof(out) - n, "{%02X}", text[i]);
    }
    HostJsonW_String(w, out, n);
}

static int HexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static size_t Utf8Length(unsigned char lead)
{
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    return 4;
}

/* The reverse of WriteGameText; returns the byte count or -1 (bad text,
 * message in `error`). */
static int ParseGameText(const struct HostJsonValue *v, u8 *out, size_t maxLength, char *error, size_t errorSize)
{
    size_t i = 0, n = 0;

    if (v == NULL || v->type != HOST_JSON_STRING)
    {
        snprintf(error, errorSize, "expected a string");
        return -1;
    }
    while (i < v->length)
    {
        int byte = -1;

        if (v->string[i] == '{')
        {
            if (i + 4 <= v->length && HexValue(v->string[i + 1]) >= 0 && HexValue(v->string[i + 2]) >= 0
             && v->string[i + 3] == '}')
            {
                byte = HexValue(v->string[i + 1]) * 16 + HexValue(v->string[i + 2]);
                i += 4;
            }
        }
        else
        {
            size_t length = Utf8Length((unsigned char)v->string[i]), j;
            for (j = 0; j < ARRAY_COUNT(sCharSymbols); j++)
                if (strlen(sCharSymbols[j].name) == length && i + length <= v->length
                 && memcmp(sCharSymbols[j].name, v->string + i, length) == 0)
                {
                    byte = (int)sCharSymbols[j].id;
                    i += length;
                    break;
                }
        }
        if (byte < 0 || byte == 0xFF)
        {
            snprintf(error, errorSize, "\"%s\": the game has no character for \"%.*s\" (use {XX} for a raw byte)",
                     v->string, (int)Utf8Length((unsigned char)v->string[i]), v->string + i);
            return -1;
        }
        if (n == maxLength)
        {
            snprintf(error, errorSize, "\"%s\" is longer than %u characters", v->string, (unsigned)maxLength);
            return -1;
        }
        out[n++] = (u8)byte;
    }
    return (int)n;
}

/* Range markers share ids with real flags and vars; name them last. */
static bool IsRangeName(const char *name)
{
    static const char *const suffixes[] = { "_START", "_END", "_COUNT" };
    size_t length = strlen(name), i;

    for (i = 0; i < ARRAY_COUNT(suffixes); i++)
    {
        size_t n = strlen(suffixes[i]);
        if (length > n && strcmp(name + length - n, suffixes[i]) == 0)
            return true;
    }
    return false;
}

static const char *SymbolName(const struct SaveSymbol *symbols, size_t count, unsigned id)
{
    const char *found = NULL;
    size_t i;

    for (i = 0; i < count; i++)
        if (symbols[i].id == id)
        {
            if (!IsRangeName(symbols[i].name))
                return symbols[i].name;
            if (found == NULL)
                found = symbols[i].name;
        }
    return found;
}

/* A flag or var: a name from the table, or a number (an integer or a
 * "0x..." string). */
static bool SymbolId(const struct SaveSymbol *symbols, size_t count, const char *text, size_t length, unsigned *id)
{
    size_t i;
    char *end;

    if (length > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    {
        unsigned long value = strtoul(text + 2, &end, 16);
        if (end == text + length)
        {
            *id = (unsigned)value;
            return true;
        }
        return false;
    }
    for (i = 0; i < count; i++)
        if (strlen(symbols[i].name) == length && memcmp(symbols[i].name, text, length) == 0)
        {
            *id = symbols[i].id;
            return true;
        }
    return false;
}

/* --------------------------------------------------------------------- */
/* Writing                                                               */
/* --------------------------------------------------------------------- */

static u32 TrainerId(const struct SaveBlock2 *sb2)
{
    return (u32)sb2->playerTrainerId[0] | ((u32)sb2->playerTrainerId[1] << 8)
         | ((u32)sb2->playerTrainerId[2] << 16) | ((u32)sb2->playerTrainerId[3] << 24);
}

static void WritePlayer(struct HostJsonWriter *w, const struct SaveImage *image)
{
    const struct SaveBlock2 *sb2 = &image->sb2;

    HostJsonW_Key(w, "player");
    HostJsonW_BeginObject(w);
    HostJsonW_Key(w, "name");
    WriteGameText(w, sb2->playerName, PLAYER_NAME_LENGTH);
    HostJsonW_Key(w, "gender");
    if (sb2->playerGender == MALE || sb2->playerGender == FEMALE)
        HostJsonW_CString(w, sb2->playerGender == MALE ? "male" : "female");
    else
        HostJsonW_Uint(w, sb2->playerGender);
    HostJsonW_Key(w, "trainer_id");
    HostJsonW_Uint(w, TrainerId(sb2));
    HostJsonW_Key(w, "play_time");
    HostJsonW_BeginObject(w);
    HostJsonW_Key(w, "hours");
    HostJsonW_Uint(w, sb2->playTimeHours);
    HostJsonW_Key(w, "minutes");
    HostJsonW_Uint(w, sb2->playTimeMinutes);
    HostJsonW_Key(w, "seconds");
    HostJsonW_Uint(w, sb2->playTimeSeconds);
    HostJsonW_Key(w, "frames");
    HostJsonW_Uint(w, sb2->playTimeVBlanks);
    HostJsonW_EndObject(w);
    HostJsonW_Key(w, "money");
    HostJsonW_Uint(w, image->sb1.money ^ sb2->encryptionKey);
    HostJsonW_Key(w, "coins");
    HostJsonW_Uint(w, (u16)(image->sb1.coins ^ sb2->encryptionKey));
    HostJsonW_EndObject(w);
}

static void WriteFlagsAndVars(struct HostJsonWriter *w, const struct SaveBlock1 *sb1)
{
    unsigned i;

    HostJsonW_Key(w, "flags");
    HostJsonW_BeginArray(w);
    for (i = 0; i < NUM_FLAG_BITS; i++)
        if (sb1->flags[i / 8] & (1 << (i % 8)))
        {
            const char *name = i < FLAGS_COUNT ? SymbolName(sFlagSymbols, ARRAY_COUNT(sFlagSymbols), i) : NULL;
            if (name != NULL)
                HostJsonW_CString(w, name);
            else
                HostJsonW_Uint(w, i);
        }
    HostJsonW_EndArray(w);

    HostJsonW_Key(w, "vars");
    HostJsonW_BeginObject(w);
    for (i = 0; i < VARS_COUNT; i++)
        if (sb1->vars[i] != 0)
        {
            const char *name = SymbolName(sVarSymbols, ARRAY_COUNT(sVarSymbols), i + VARS_START);
            char hex[16];
            if (name == NULL)
            {
                snprintf(hex, sizeof(hex), "0x%X", i + VARS_START);
                name = hex;
            }
            HostJsonW_Key(w, name);
            HostJsonW_Uint(w, sb1->vars[i]);
        }
    HostJsonW_EndObject(w);
}

static void WriteInfo(struct HostJsonWriter *w, const struct SaveImage *image)
{
    const struct SaveBlock1 *sb1 = &image->sb1;
    unsigned i;

    HostJsonW_Key(w, "info");
    HostJsonW_BeginObject(w);
    HostJsonW_Key(w, "note");
    HostJsonW_CString(w, "Read-only: rewritten on every save, ignored when loading.");
    HostJsonW_Key(w, "location");
    HostJsonW_BeginObject(w);
    HostJsonW_Key(w, "map_group");
    HostJsonW_Int(w, sb1->location.mapGroup);
    HostJsonW_Key(w, "map_num");
    HostJsonW_Int(w, sb1->location.mapNum);
    HostJsonW_Key(w, "x");
    HostJsonW_Int(w, sb1->pos.x);
    HostJsonW_Key(w, "y");
    HostJsonW_Int(w, sb1->pos.y);
    HostJsonW_EndObject(w);
    HostJsonW_Key(w, "party");
    HostJsonW_BeginArray(w);
    for (i = 0; i < min(sb1->playerPartyCount, PARTY_SIZE); i++)
    {
        /* GetMonData decrypts in place and re-encrypts: use a copy. */
        struct Pokemon mon = sb1->playerParty[i];
        u8 nickname[POKEMON_NAME_BUFFER_SIZE + 8];
        u32 species = GetMonData(&mon, MON_DATA_SPECIES);

        memset(nickname, 0xFF, sizeof(nickname));
        GetMonData(&mon, MON_DATA_NICKNAME, nickname);
        HostJsonW_BeginObject(w);
        HostJsonW_Key(w, "species");
        WriteGameText(w, GetSpeciesName(species), POKEMON_NAME_LENGTH);
        HostJsonW_Key(w, "nickname");
        WriteGameText(w, nickname, POKEMON_NAME_LENGTH);
        HostJsonW_Key(w, "level");
        HostJsonW_Uint(w, GetMonData(&mon, MON_DATA_LEVEL));
        HostJsonW_EndObject(w);
    }
    HostJsonW_EndArray(w);
    HostJsonW_EndObject(w);
}

static void WriteLayout(struct HostJsonWriter *w)
{
    size_t i;

    HostJsonW_Key(w, "layout");
    HostJsonW_BeginObject(w);
    for (i = 0; i < ARRAY_COUNT(sBlocks); i++)
    {
        HostJsonW_Key(w, sBlocks[i].name);
        HostJsonW_Uint(w, (u32)sBlocks[i].size);
    }
    HostJsonW_EndObject(w);
}

char *HostSaveJson_FromFlash(const unsigned char *flash, size_t *length)
{
    struct HostJsonWriter w;
    struct SaveImage *image = NULL;
    int slot = 0, kind;
    u32 counter = 0;
    size_t i;

    kind = ClassifyFlash(flash, &slot, &counter);
    if (kind == FLASH_SLOT)
    {
        image = calloc(1, sizeof(*image));
        if (image == NULL)
            return NULL;
        UnpackSlot(flash, slot, image);
        image->counter = counter;
    }

    HostJsonW_Init(&w);
    HostJsonW_BeginObject(&w);
    HostJsonW_Key(&w, "format");
    HostJsonW_CString(&w, HOST_SAVE_JSON_FORMAT);
    HostJsonW_Key(&w, "version");
    HostJsonW_Uint(&w, HOST_SAVE_JSON_VERSION);
    WriteLayout(&w);

    if (kind == FLASH_RAW)
    {
        HostJsonW_Key(&w, "note");
        HostJsonW_CString(&w, "The save slots are mid-write or damaged, so this holds the raw flash chip.");
        HostJsonW_Key(&w, "flash");
        WriteBase64(&w, flash, FLASH_SIZE);
    }
    else
    {
        if (image != NULL)
        {
            HostJsonW_Key(&w, "game");
            HostJsonW_BeginObject(&w);
            HostJsonW_Key(&w, "save_counter");
            HostJsonW_Uint(&w, image->counter);
            WritePlayer(&w, image);
            WriteFlagsAndVars(&w, &image->sb1);
            WriteInfo(&w, image);
            HostJsonW_Key(&w, "blocks");
            HostJsonW_BeginObject(&w);
            for (i = 0; i < ARRAY_COUNT(sBlocks); i++)
            {
                HostJsonW_Key(&w, sBlocks[i].name);
                WriteBase64(&w, (const u8 *)image + sBlocks[i].offset, sBlocks[i].size);
            }
            HostJsonW_EndObject(&w);
            HostJsonW_EndObject(&w);
        }
        HostJsonW_Key(&w, "sectors");
        HostJsonW_BeginObject(&w);
        for (i = 0; i < ARRAY_COUNT(sSpecialSectors); i++)
        {
            const u8 *sector = flash + (size_t)sSpecialSectors[i].sector * SECTOR_SIZE;
            if (Erased(sector, SECTOR_SIZE))
                continue;
            HostJsonW_Key(&w, sSpecialSectors[i].name);
            WriteBase64(&w, sector, SECTOR_SIZE);
        }
        HostJsonW_EndObject(&w);
    }
    HostJsonW_EndObject(&w);
    free(image);

    if (!HostJsonW_Finish(&w))
    {
        HostJsonW_Free(&w);
        return NULL;
    }
    *length = w.length;
    return w.data;
}

/* --------------------------------------------------------------------- */
/* Reading                                                               */
/* --------------------------------------------------------------------- */

struct Reader
{
    char *error;
    size_t errorSize;
};

static bool Error(struct Reader *r, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    vsnprintf(r->error, r->errorSize, format, args);
    va_end(args);
    return false;
}

/* `key` of `object` as a number in [0, max], if present and different from
 * `current`; *changed says whether to apply it. */
static bool ReadField(struct Reader *r, const struct HostJsonValue *object, const char *key,
                      u32 current, u32 max, u32 *out, bool *changed)
{
    const struct HostJsonValue *v = HostJson_Get(object, key);

    *changed = false;
    if (v == NULL)
        return true;
    if (v->type == HOST_JSON_NUMBER && v->number == (double)current)
        return true;
    if (!HostJson_GetUint(v, max, out))
        return Error(r, "\"%s\" must be a whole number from 0 to %lu", key, (unsigned long)max);
    *changed = true;
    return true;
}

static bool ApplyPlayer(struct Reader *r, const struct HostJsonValue *player, struct SaveImage *image)
{
    struct SaveBlock2 *sb2 = &image->sb2;
    struct SaveBlock1 *sb1 = &image->sb1;
    const struct HostJsonValue *v, *time;
    u32 value;
    bool changed;

    if (player == NULL)
        return true;
    if (player->type != HOST_JSON_OBJECT)
        return Error(r, "\"player\" must be an object");

    if ((v = HostJson_Get(player, "name")) != NULL)
    {
        u8 name[PLAYER_NAME_LENGTH];
        int length = ParseGameText(v, name, PLAYER_NAME_LENGTH, r->error, r->errorSize);
        int old;

        if (length < 0)
            return false;
        for (old = 0; old < PLAYER_NAME_LENGTH && sb2->playerName[old] != 0xFF; old++)
            ;
        if (length != old || memcmp(name, sb2->playerName, (size_t)length) != 0)
        {
            memset(sb2->playerName, 0xFF, sizeof(sb2->playerName));
            memcpy(sb2->playerName, name, (size_t)length);
        }
    }

    if ((v = HostJson_Get(player, "gender")) != NULL)
    {
        if (v->type == HOST_JSON_STRING && strcmp(v->string, "male") == 0)
            sb2->playerGender = MALE;
        else if (v->type == HOST_JSON_STRING && strcmp(v->string, "female") == 0)
            sb2->playerGender = FEMALE;
        else if (!(v->type == HOST_JSON_NUMBER && v->number == sb2->playerGender))
            return Error(r, "\"gender\" must be \"male\" or \"female\"");
    }

    if (!ReadField(r, player, "trainer_id", TrainerId(sb2), 0xFFFFFFFF, &value, &changed))
        return false;
    if (changed)
    {
        sb2->playerTrainerId[0] = (u8)value;
        sb2->playerTrainerId[1] = (u8)(value >> 8);
        sb2->playerTrainerId[2] = (u8)(value >> 16);
        sb2->playerTrainerId[3] = (u8)(value >> 24);
    }

    if ((time = HostJson_Get(player, "play_time")) != NULL)
    {
        if (time->type != HOST_JSON_OBJECT)
            return Error(r, "\"play_time\" must be an object");
        if (!ReadField(r, time, "hours", sb2->playTimeHours, 0xFFFF, &value, &changed))
            return false;
        if (changed) sb2->playTimeHours = (u16)value;
        if (!ReadField(r, time, "minutes", sb2->playTimeMinutes, 59, &value, &changed))
            return false;
        if (changed) sb2->playTimeMinutes = (u8)value;
        if (!ReadField(r, time, "seconds", sb2->playTimeSeconds, 59, &value, &changed))
            return false;
        if (changed) sb2->playTimeSeconds = (u8)value;
        if (!ReadField(r, time, "frames", sb2->playTimeVBlanks, 59, &value, &changed))
            return false;
        if (changed) sb2->playTimeVBlanks = (u8)value;
    }

    /* Money and coins are stored XORed with the save's encryption key. */
    if (!ReadField(r, player, "money", sb1->money ^ sb2->encryptionKey, MAX_MONEY, &value, &changed))
        return false;
    if (changed) sb1->money = value ^ sb2->encryptionKey;
    if (!ReadField(r, player, "coins", (u16)(sb1->coins ^ sb2->encryptionKey), MAX_COINS, &value, &changed))
        return false;
    if (changed) sb1->coins = (u16)(value ^ sb2->encryptionKey);
    return true;
}

static bool ApplyFlags(struct Reader *r, const struct HostJsonValue *flags, struct SaveBlock1 *sb1)
{
    size_t i;

    if (flags == NULL)
        return true;
    if (flags->type != HOST_JSON_ARRAY)
        return Error(r, "\"flags\" must be a list");
    memset(sb1->flags, 0, sizeof(sb1->flags));
    for (i = 0; i < flags->count; i++)
    {
        const struct HostJsonValue *v = &flags->items[i];
        unsigned id;
        u32 number;

        if (v->type == HOST_JSON_STRING)
        {
            if (!SymbolId(sFlagSymbols, ARRAY_COUNT(sFlagSymbols), v->string, v->length, &id))
                return Error(r, "unknown flag \"%s\"", v->string);
        }
        else if (HostJson_GetUint(v, 0xFFFFFFFF, &number))
            id = number;
        else
            return Error(r, "flags must be names or numbers");
        if (id >= NUM_FLAG_BITS)
            return Error(r, "flag %u is not saved (only 0-%u are)", id, (unsigned)NUM_FLAG_BITS - 1);
        sb1->flags[id / 8] |= 1 << (id % 8);
    }
    return true;
}

static bool ApplyVars(struct Reader *r, const struct HostJsonValue *vars, struct SaveBlock1 *sb1)
{
    size_t i;

    if (vars == NULL)
        return true;
    if (vars->type != HOST_JSON_OBJECT)
        return Error(r, "\"vars\" must be an object");
    memset(sb1->vars, 0, sizeof(sb1->vars));
    for (i = 0; i < vars->count; i++)
    {
        const struct HostJsonValue *v = &vars->items[i];
        unsigned id;
        u32 value;

        if (!SymbolId(sVarSymbols, ARRAY_COUNT(sVarSymbols), v->key, v->keyLength, &id))
            return Error(r, "unknown var \"%s\"", v->key);
        if (id < VARS_START || id >= VARS_START + VARS_COUNT)
            return Error(r, "var \"%s\" is not saved", v->key);
        if (!HostJson_GetUint(v, 0xFFFF, &value))
            return Error(r, "var \"%s\" must be a whole number from 0 to 65535", v->key);
        sb1->vars[id - VARS_START] = (u16)value;
    }
    return true;
}

static bool ReadGame(struct Reader *r, const struct HostJsonValue *root, const struct HostJsonValue *game, u8 *flash)
{
    const struct HostJsonValue *layout = HostJson_Get(root, "layout");
    const struct HostJsonValue *blocks = HostJson_Get(game, "blocks");
    struct SaveImage *image;
    size_t i;
    bool ok;

    if (game->type != HOST_JSON_OBJECT)
        return Error(r, "\"game\" must be an object");
    for (i = 0; i < ARRAY_COUNT(sBlocks); i++)
    {
        u32 size;
        if (!HostJson_GetUint(HostJson_Get(layout, sBlocks[i].name), 0xFFFFFFFF, &size))
            return Error(r, "\"layout\" is missing \"%s\"", sBlocks[i].name);
        if (size != sBlocks[i].size)
            return Error(r, "the save was made by a build with a different %s layout (%lu bytes, this build: %lu)",
                         sBlocks[i].name, (unsigned long)size, (unsigned long)sBlocks[i].size);
    }

    image = calloc(1, sizeof(*image));
    if (image == NULL)
        return Error(r, "out of memory");
    ok = HostJson_GetUint(HostJson_Get(game, "save_counter"), 0xFFFFFFFF, &image->counter)
      || Error(r, "\"save_counter\" is missing or not a whole number");
    for (i = 0; ok && i < ARRAY_COUNT(sBlocks); i++)
        if (!ReadBase64(HostJson_Get(blocks, sBlocks[i].name), (u8 *)image + sBlocks[i].offset, sBlocks[i].size))
            ok = Error(r, "\"blocks\".\"%s\" is missing or not %lu bytes of base64",
                       sBlocks[i].name, (unsigned long)sBlocks[i].size);
    ok = ok && ApplyPlayer(r, HostJson_Get(game, "player"), image)
            && ApplyFlags(r, HostJson_Get(game, "flags"), &image->sb1)
            && ApplyVars(r, HostJson_Get(game, "vars"), &image->sb1);
    if (ok)
        PackSlot(image, flash);
    free(image);
    return ok;
}

bool HostSaveJson_ToFlash(const char *text, size_t length, unsigned char *flash, char *error, size_t errorSize)
{
    struct Reader r = { error, errorSize };
    struct HostJsonValue *root;
    const struct HostJsonValue *format, *v;
    u8 *image;
    u32 version;
    bool ok = true;
    size_t i;

    root = HostJson_Parse(text, length, error, errorSize);
    if (root == NULL)
        return false;
    format = HostJson_Get(root, "format");
    if (format == NULL || format->type != HOST_JSON_STRING || strcmp(format->string, HOST_SAVE_JSON_FORMAT) != 0)
    {
        HostJson_Free(root);
        return Error(&r, "not a " HOST_SAVE_JSON_FORMAT " file");
    }
    if (!HostJson_GetUint(HostJson_Get(root, "version"), 0xFFFFFFFF, &version) || version != HOST_SAVE_JSON_VERSION)
    {
        HostJson_Free(root);
        return Error(&r, "unsupported version (this build reads version %d)", HOST_SAVE_JSON_VERSION);
    }

    image = malloc(FLASH_SIZE);
    if (image == NULL)
    {
        HostJson_Free(root);
        return Error(&r, "out of memory");
    }
    memset(image, 0xFF, FLASH_SIZE);

    if ((v = HostJson_Get(root, "flash")) != NULL)
    {
        if (!ReadBase64(v, image, FLASH_SIZE))
            ok = Error(&r, "\"flash\" is not %u bytes of base64", FLASH_SIZE);
    }
    else
    {
        const struct HostJsonValue *game = HostJson_Get(root, "game");
        const struct HostJsonValue *sectors = HostJson_Get(root, "sectors");

        if (game != NULL)
            ok = ReadGame(&r, root, game, image);
        for (i = 0; ok && i < ARRAY_COUNT(sSpecialSectors); i++)
        {
            v = HostJson_Get(sectors, sSpecialSectors[i].name);
            if (v != NULL && !ReadBase64(v, image + (size_t)sSpecialSectors[i].sector * SECTOR_SIZE, SECTOR_SIZE))
                ok = Error(&r, "\"sectors\".\"%s\" is not %u bytes of base64", sSpecialSectors[i].name, SECTOR_SIZE);
        }
    }
    if (ok)
        memcpy(flash, image, FLASH_SIZE);
    free(image);
    HostJson_Free(root);
    return ok;
}
