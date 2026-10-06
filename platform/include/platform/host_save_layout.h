/*
 * platform/include/platform/host_save_layout.h
 *
 * Every field of the game's save blocks, for the JSON save file
 * (host_save_json.c): the table in platform/src/host_save_layout.inc is
 * generated from the build's debug info by platform/tools/save_layout.py.
 */

#ifndef PLATFORM_HOST_SAVE_LAYOUT_H
#define PLATFORM_HOST_SAVE_LAYOUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum HostSaveFieldKind
{
    HOST_SAVE_UINT,    /* unsigned integer, enum or pointer */
    HOST_SAVE_SINT,    /* signed integer */
    HOST_SAVE_STRUCT,  /* gHostSaveTypes[type] */
    HOST_SAVE_BYTES,   /* a union: its bytes */
};

struct HostSaveField
{
    const char *name;
    uint16_t offset;     /* in the parent type: the first byte, for a bitfield */
    uint16_t size;       /* one element; bytes spanned, for a bitfield */
    uint8_t kind;        /* enum HostSaveFieldKind */
    uint8_t bitOffset;   /* bitfields: from bit 0 of the byte at `offset` */
    uint8_t bitSize;     /* 0: not a bitfield */
    uint8_t dimCount;    /* array dimensions, outermost first */
    uint16_t dims[3];
    uint16_t type;
};

struct HostSaveType
{
    const char *name;
    uint16_t size;
    uint16_t firstField;
    uint16_t fieldCount;
};

/* The roots come first, in this order. */
enum
{
    HOST_SAVE_TYPE_SAVE_BLOCK_2,
    HOST_SAVE_TYPE_SAVE_BLOCK_1,
    HOST_SAVE_TYPE_POKEMON_STORAGE,
    HOST_SAVE_TYPE_SAVE_BLOCK_3,
};

extern const struct HostSaveType gHostSaveTypes[];
extern const struct HostSaveField gHostSaveFields[];
extern const unsigned gHostSaveTypeCount;

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_HOST_SAVE_LAYOUT_H */
