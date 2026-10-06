/*
 * platform/include/platform/host_config.h
 *
 * Player settings: pkmemerald.ini in the save directory.
 *
 * An INI file, written with comments and the defaults the first time the
 * game starts, and never rewritten after that, so the player's edits and
 * comments stay. Settings are read in this order, later ones winning:
 * defaults, the file, command-line flags. Mistakes (unknown setting, bad
 * value) are reported and the default is used instead; they never stop the
 * game from starting.
 *
 *   [video]
 *   scale = 3              initial window size, multiple of 240x160 (1-16)
 *   fullscreen = no        borderless fullscreen (F11 / Alt+Enter toggle)
 *   [sound]
 *   sound = yes
 *   smooth_sound = no      interpolated/low-passed instead of exact GBA output
 *   [keyboard]
 *   a = Z                  keys for each GBA button: SDL key names, comma-
 *   ...                    separated (key names are checked by the input
 *                          code, which knows them)
 */

#ifndef PLATFORM_HOST_CONFIG_H
#define PLATFORM_HOST_CONFIG_H

#include <stdbool.h>

#include "platform/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HOST_CONFIG_FILE "pkmemerald.ini"
#define HOST_CONFIG_MAX_SCALE 16
#define HOST_CONFIG_KEYS_LENGTH 128

struct HostConfig
{
    int scale;
    bool fullscreen;
    bool sound;
    bool smoothSound;
    /* Indexed by button bit (PLATFORM_BUTTON_A is bit 0, ...). */
    char keys[PLATFORM_BUTTON_COUNT][HOST_CONFIG_KEYS_LENGTH];
};

/* The [keyboard] setting name of each button, indexed by button bit. */
extern const char *const gHostConfigButtonNames[PLATFORM_BUTTON_COUNT];

void HostConfig_SetDefaults(struct HostConfig *config);

/* Apply settings from INI `text`. Problems are reported on stderr as
 * "config: SOURCE:LINE: ..." and leave that setting unchanged. Returns the
 * number of problems. */
int HostConfig_Parse(struct HostConfig *config, const char *text, const char *sourceName);

/* The commented default file. Parsing it yields the defaults. */
const char *HostConfig_DefaultText(void);

/* Set defaults, then apply the file at `path`; if there is none, create it
 * with the defaults. Reports what it did on stdout ("config: PATH"). Returns
 * false only if an existing file could not be read (defaults are used). */
bool HostConfig_Load(struct HostConfig *config, const char *path);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_HOST_CONFIG_H */
