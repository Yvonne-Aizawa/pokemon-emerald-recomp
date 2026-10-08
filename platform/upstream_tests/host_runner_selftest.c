/*
 * platform/upstream_tests/host_runner_selftest.c
 *
 * A test that must fail, built into pkmemerald-tests: the upstream-detects-
 * failure test (CMakeLists.txt) checks that the host runner reports it as a
 * failure, with upstream's message, and exits 1. Also the runner's other
 * self-checks.
 */

#include <stdlib.h>  /* before global.h, whose macros clash with it */

#include "global.h"
#include "test/test.h"

TEST("Host runner self-test: a failing EXPECT_EQ fails")
{
    EXPECT_EQ(1, 2);
}

// ASan and UBSan end the process with exit(1) after a report, not a signal:
// upstream-runner-exit checks that the runner still counts that as CRASH.
TEST("Host runner: exit(1) counts as CRASH")
{
    KNOWN_CRASHING;
    exit(1);
}

// Regression checks for host crashes found by the broad sanitizer battle scan.
#include "recorded_battle.h"
#include "battle_anim.h"
#include "sprite.h"
#include "task.h"

TEST("Host regression: clearing recorded actions stops at an empty buffer")
{
    u8 buffer[3];
    RecordedBattle_Init(0);
    RecordedBattle_ClearBattlerAction(0, 1);
    RecordedBattle_SetBattlerAction(0, 42);
    RecordedBattle_ClearBattlerAction(0, 255);
    RecordedBattle_ClearBattlerAction(0, 1);
    RecordedBattle_SetBattlerAction(0, 17);
    EXPECT_EQ(RecordedBattle_BufferNewBattlerData(buffer), 3);
    EXPECT_EQ(buffer[0], 0);
    EXPECT_EQ(buffer[1], 1);
    EXPECT_EQ(buffer[2], 17);
}

void AnimTask_AnimateGustTornadoPalette(u8 taskId);

TEST("Host regression: Gust palette task finishes when its palette is absent")
{
    ResetTasks();
    FreeAllSpritePalettes();
    gBattleAnimArgs[0] = 0;
    gBattleAnimArgs[1] = 2;
    gAnimVisualTaskCount = 1;
    u8 taskId = CreateTask(AnimTask_AnimateGustTornadoPalette, 0);
    AnimTask_AnimateGustTornadoPalette(taskId);
    gTasks[taskId].func(taskId);
    gTasks[taskId].func(taskId);
    EXPECT_EQ(gTasks[taskId].isActive, FALSE);
    EXPECT_EQ(gAnimVisualTaskCount, 0);
}
