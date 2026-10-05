/*
 * platform/tests/test_m4a_stub.c
 *
 * The silent sound engine (host_m4a.c) must accept a NULL player like the
 * real one: gMPlay_PokemonCry is NULL until the first cry plays, and e.g. the
 * Pokedex info screen stops "the current cry" before any has played.
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
    printf("ok  : cry and player controls accept a NULL player\n");

    printf("m4a_stub: all tests passed\n");
    return 0;
}
