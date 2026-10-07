/*
 * platform/tests/test_mon_anim_vshake.c
 *
 * The vertical shake animations must reach their end without dividing by zero
 * (platform/patches/pokemon_animation.c.patch). sVerticalShakeData ends with
 * the row {-1, 0}, and VerticalShakeTwice / VerticalShakeLowTwice divided by
 * that 0 before checking for the end marker: SIGFPE on x86, harmless on the
 * GBA. It crashed in the overworld when a Pokémon's front sprite animated.
 */

#include <stdio.h>
#include <string.h>

#include "global.h"
#include "sprite.h"
#include "pokemon_animation.h"

static const struct {
    enum AnimFunctionIDs id;
    const char *name;
} sAnims[] = {
    { ANIM_V_SHAKE_TWICE,          "ANIM_V_SHAKE_TWICE" },
    { ANIM_V_SHAKE_TWICE_SLOW,     "ANIM_V_SHAKE_TWICE_SLOW" },
    { ANIM_V_SHAKE_LOW_TWICE,      "ANIM_V_SHAKE_LOW_TWICE" },
    { ANIM_V_SHAKE_LOW_TWICE_SLOW, "ANIM_V_SHAKE_LOW_TWICE_SLOW" },
    { ANIM_V_SHAKE_LOW_TWICE_FAST, "ANIM_V_SHAKE_LOW_TWICE_FAST" },
};

int main(void)
{
    int failed = 0;

    for (size_t i = 0; i < ARRAY_COUNT(sAnims); i++)
    {
        struct Sprite sprite;
        int frame;

        memset(&sprite, 0, sizeof(sprite));
        sprite.animEnded = TRUE;
        StartMonSummaryAnimation(&sprite, sAnims[i].id);
        for (frame = 0; frame < 5000 && sprite.callback != SpriteCallbackDummy; frame++)
            sprite.callback(&sprite);

        if (sprite.callback != SpriteCallbackDummy || sprite.y2 != 0)
        {
            printf("FAIL: %s: not finished after %d frames (y2 %d)\n",
                   sAnims[i].name, frame, sprite.y2);
            failed++;
        }
        else
        {
            printf("ok  : %s ends after %d frames\n", sAnims[i].name, frame);
        }
    }
    return failed != 0;
}
