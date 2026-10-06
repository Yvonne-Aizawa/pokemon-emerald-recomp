/*
 * platform/include/platform/host_save_json.h
 *
 * The JSON save file (host_save_json.c): converts between the emulated
 * 128 KiB flash chip and its JSON form. host_flash.c does the file I/O.
 *
 * The file holds the save the game's loader would pick from the flash --
 * the newest complete save slot -- as every field of its save blocks by name
 * (host_save_layout.h), plus readable fields (player, money, flags,
 * vars...) in place of the raw ones. Reading the file rebuilds a flash chip
 * holding that one slot. When the slots aren't in a state the
 * loader takes cleanly (an interrupted or damaged save), the file holds the
 * raw flash image instead, so the game sees exactly what it wrote.
 */

#ifndef PLATFORM_HOST_SAVE_JSON_H
#define PLATFORM_HOST_SAVE_JSON_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HOST_SAVE_JSON_FORMAT  "pkmemerald-save"
#define HOST_SAVE_JSON_VERSION 2  /* 1: save blocks as base64 (still read) */

/* The JSON text for a flash image (FLASH_ROM_SIZE_1M bytes): malloc'd and
 * NUL-terminated, length in *length. NULL if out of memory. */
char *HostSaveJson_FromFlash(const unsigned char *flash, size_t *length);

/* Rebuild a flash image from JSON text. On failure `flash` is unchanged and
 * `error` says why (file problems the player can fix: names, ranges...). */
bool HostSaveJson_ToFlash(const char *text, size_t length, unsigned char *flash, char *error, size_t errorSize);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_HOST_SAVE_JSON_H */
