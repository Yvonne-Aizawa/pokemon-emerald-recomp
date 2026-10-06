/*
 * platform/include/platform/host_save.h
 *
 * The cartridge's flash save chip, backed by a file.
 *
 * The file is JSON (platform/host_save_json.h): the game's save blocks, with
 * the player, money, flags and vars readable and editable. Older saves were a
 * raw image of the 128 KiB (1 Mbit) flash, pkmemerald.sav; one is still read
 * when there is no JSON save, and converted.
 */

#ifndef PLATFORM_HOST_SAVE_H
#define PLATFORM_HOST_SAVE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HOST_SAVE_FILE_NAME        "pkmemerald.json"
#define HOST_LEGACY_SAVE_FILE_NAME "pkmemerald.sav"

/* Load `saveDir`/pkmemerald.json into the emulated flash, creating `saveDir`
 * if needed; without one, an old pkmemerald.sav there is loaded and
 * converted on the first flush (and kept). No file means a blank (erased)
 * chip. Returns false if saving had to be disabled (the file is unreadable
 * or invalid -- a JSON error is printed; it is then left untouched). Call
 * before the game boots. */
bool Host_SaveOpen(const char *saveDir);

/* Write the save back if the game changed the flash since the last flush:
 * to a temporary file, synced, then renamed over the save, so an
 * interrupted write never corrupts it. Cheap when nothing changed; call
 * once per frame and at exit. Returns false if writing failed. */
bool Host_SaveFlush(void);

/* Load a save (JSON or a raw flash image) with persistence disabled. Never
 * creates directories or attaches the file for writing. */
bool Host_SaveOpenReadOnly(const char *path);

/* Convert a save (JSON or raw) to `output`: a raw flash image if its name
 * ends in .sav, JSON otherwise. Returns an exit status. */
int Host_ConvertSave(const char *input, const char *output);

/* Print a JSON checkpoint report using the game's save loader. */
int Host_InspectSave(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_HOST_SAVE_H */
