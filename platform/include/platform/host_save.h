/*
 * platform/include/platform/host_save.h
 *
 * The cartridge's flash save chip, backed by a file.
 *
 * The file is a raw image of the 128 KiB (1 Mbit) flash -- the same format
 * GBA emulators use for .sav files.
 */

#ifndef PLATFORM_HOST_SAVE_H
#define PLATFORM_HOST_SAVE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HOST_SAVE_FILE_NAME "pkmemerald.sav"

/* Load `saveDir`/pkmemerald.sav into the emulated flash, creating `saveDir`
 * if needed. A missing file means a blank (erased) chip. Returns false if
 * saving had to be disabled (the file is unreadable or has the wrong size;
 * it is then left untouched). Call before the game boots. */
bool Host_SaveOpen(const char *saveDir);

/* Write the flash image back if the game changed it since the last flush:
 * to a temporary file, synced, then renamed over the save, so an
 * interrupted write never corrupts it. Cheap when nothing changed; call
 * once per frame and at exit. Returns false if writing failed. */
bool Host_SaveFlush(void);

/* Save the game as the start menu's SAVE does, and write the file (for
 * generating test saves: host_main.c --make-save). Only when the player is
 * free in the overworld; returns false otherwise or if saving failed. */
bool Host_SaveGameNow(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_HOST_SAVE_H */
