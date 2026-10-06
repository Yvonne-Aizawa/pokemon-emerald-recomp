/*
 * platform/tools/save_layout_probe.c
 *
 * Compiled with debug info (the save-layout-probe object, never linked) so
 * platform/tools/save_layout.py can read the save blocks' layout from it.
 * Same headers as platform/src/host_save_layout.c.
 */

#include "global.h"
#include "pokemon_storage_system.h"

struct HostSaveLayoutProbe
{
    struct SaveBlock2 saveBlock2;
    struct SaveBlock1 saveBlock1;
    struct PokemonStorage pokemonStorage;
    struct SaveBlock3 saveBlock3;
};

struct HostSaveLayoutProbe gHostSaveLayoutProbe;
