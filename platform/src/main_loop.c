/*
 * platform/src/main_loop.c
 *
 * Fixed-rate host main loop; see platform/main_loop.h.
 */

#include "platform/main_loop.h"
#include "platform/platform.h"

#include <stddef.h>

/* If we fall further behind than this (debugger pause, slow frame), drop the
 * backlog instead of running frames back-to-back to catch up. */
#define MAX_FRAME_LAG 4

static HostFrameCallback sFrameCallback;
static uint32_t sFrameCount;

void Host_SetFrameCallback(HostFrameCallback callback)
{
    sFrameCallback = callback;
}

void Host_RunFrame(void)
{
    if (sFrameCallback != NULL)
        sFrameCallback();
}

uint32_t Host_GetFrameCount(void)
{
    return sFrameCount;
}

uint32_t Host_RunMainLoop(uint32_t maxFrames)
{
    uint32_t framesRun = 0;
    uint64_t deadline = Platform_GetTimeNs() + HOST_FRAME_NS;

    while (maxFrames == 0 || framesRun < maxFrames)
    {
        uint64_t now;

        Platform_PollEvents();
        if (Platform_QuitRequested())
            break;

        Platform_FrameBegin();
        Host_RunFrame();
        Platform_FrameEnd();

        sFrameCount++;
        framesRun++;

        now = Platform_GetTimeNs();
        if (now < deadline)
            Platform_SleepNs(deadline - now);
        else if (now - deadline > (uint64_t)MAX_FRAME_LAG * HOST_FRAME_NS)
            deadline = now;
        deadline += HOST_FRAME_NS;
    }

    return framesRun;
}
