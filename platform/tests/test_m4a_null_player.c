/*
 * platform/tests/test_m4a_null_player.c
 *
 * The sound engine must accept a NULL player (platform/patches/m4a.c.patch):
 * gMPlay_PokemonCry is NULL until the first cry plays, and e.g. the Pokedex
 * info screen stops "the current cry" before any has played. On the GBA the
 * engine reads BIOS junk through NULL and does nothing.
 */

#include <stdio.h>

#include "global.h"
#include "m4a.h"
#include "sound.h"

extern struct MusicPlayerInfo *gMPlay_PokemonCry;

int main(void)
{
    gMPlay_PokemonCry = NULL;

    StopCryAndClearCrySongs();
    StopCry();
    m4aMPlayContinue(NULL);
    m4aMPlayFadeOut(NULL, 4);
    m4aMPlayFadeOutTemporarily(NULL, 4);
    m4aMPlayFadeIn(NULL, 4);
    m4aMPlayTempoControl(NULL, 0x100);
    m4aMPlayVolumeControl(NULL, TRACKS_ALL, 0x100);
    m4aMPlayImmInit(NULL);
    if (IsCryPlaying())
    {
        printf("FAIL: no cry is playing\n");
        return 1;
    }
    printf("ok  : cry and player controls accept a NULL player\n");

    printf("m4a_null_player: all tests passed\n");
    return 0;
}
