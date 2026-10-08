/*
 * platform/src/host_save_layout.c
 *
 * The save-block field table (see platform/host_save_layout.h), generated
 * into host_save_layout.inc along with checks that it matches this build's
 * structs. Same headers as platform/tools/save_layout_probe.c.
 */

#include <stddef.h>

#include "global.h"
#include "pokemon_storage_system.h"

#include "platform/host_save_layout.h"

/* PC port: select the generated table for this host's pointer width. */
#if __SIZEOF_POINTER__ == 8
#include "host_save_layout_64.inc"
#else
#include "host_save_layout.inc"
#endif
