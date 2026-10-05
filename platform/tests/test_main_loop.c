/*
 * platform/tests/test_main_loop.c
 *
 * Host main loop smoke test: the VBlank callback runs once per frame, the
 * frame limit and quit flag stop the loop, and frames are paced at the GBA
 * refresh rate.
 */

#include "platform/main_loop.h"
#include "platform/platform.h"

#include <stdio.h>

#define FRAMES 30

static unsigned sCalls;
static unsigned sQuitAfter;

static void CountVBlank(void)
{
    sCalls++;
    if (sQuitAfter != 0 && sCalls == sQuitAfter)
        Platform_RequestQuit();
}

static int Check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    return ok ? 0 : 1;
}

int main(void)
{
    struct PlatformConfig config = { .dataDir = "assets", .saveDir = "saves" };
    int failures = 0;
    uint64_t start, elapsed, expected;
    uint32_t ran;

    if (Platform_Init(&config) != 0)
        return Check(0, "Platform_Init");

    /* Frame limit + pacing. */
    Host_SetVBlankCallback(CountVBlank);
    start = Platform_GetTimeNs();
    ran = Host_RunMainLoop(FRAMES);
    elapsed = Platform_GetTimeNs() - start;
    expected = (uint64_t)FRAMES * HOST_FRAME_NS;

    failures += Check(ran == FRAMES, "loop stops at the frame limit");
    failures += Check(sCalls == FRAMES, "VBlank callback runs once per frame");
    failures += Check(Host_GetFrameCount() == FRAMES, "frame counter matches");
    /* Generous upper bound: CI machines can be slow to wake up. */
    failures += Check(elapsed >= expected - HOST_FRAME_NS && elapsed < expected * 2,
                      "frames are paced at ~59.73 Hz");
    printf("      %d frames took %.1f ms (expected %.1f ms)\n",
           FRAMES, elapsed / 1e6, expected / 1e6);

    /* Quit request ends an unbounded loop. */
    sCalls = 0;
    sQuitAfter = 5;
    ran = Host_RunMainLoop(0);
    failures += Check(ran == 5 && Platform_QuitRequested(), "quit request stops the loop");

    Platform_Shutdown();
    printf(failures ? "main_loop: %d FAILED\n" : "main_loop: all tests passed\n", failures);
    return failures != 0;
}
