/*
 * platform/src/host_save_game.c
 *
 * --make-save (host_main.c): save the game as the start menu's SAVE does, for
 * generating test saves (PLAN.md, Phase 19a-2). Only from the overworld with
 * the player free to move: a save made mid-script or mid-battle would not
 * continue the way a player's save does.
 */

/* libc first: global.h defines function-like macros that clash with it. */
#include <stdio.h>

#include "global.h"
#include "fieldmap.h"
#include "main.h"
#include "new_game.h"
#include "overworld.h"
#include "save.h"
#include "script.h"
#include "constants/game_stat.h"

#include "platform/host_save.h"

bool Host_SaveGameNow(void)
{
    if (gMain.callback2 != CB2_Overworld || ArePlayerFieldControlsLocked() || ScriptContext_IsEnabled())
    {
        fprintf(stderr, "save: --make-save: the player isn't free in the overworld at the end of the run;"
                        " no save made\n");
        return false;
    }

    /* start_menu.c: InitSave, then RunSaveCallback. */
    SaveMapView();
    IncrementGameStat(GAME_STAT_SAVED_GAME);
    if (TrySavingData(gDifferentSaveFile ? SAVE_OVERWRITE_DIFFERENT_FILE : SAVE_NORMAL) != SAVE_STATUS_OK)
    {
        fprintf(stderr, "save: --make-save: saving failed\n");
        return false;
    }
    gDifferentSaveFile = FALSE;
    return Host_SaveFlush();
}
