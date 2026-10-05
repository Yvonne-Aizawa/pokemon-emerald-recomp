/*
 * platform/src/platform.c
 *
 * PC platform layer placeholder.
 *
 * Phase 1: this file exists so the CMake target has at least one translation
 * unit to build. Real platform/SDL2 implementations arrive in Phase 5.
 */

#include "platform/platform.h"

#include <stddef.h>

int Platform_Init(void)
{
    return 0;
}

void Platform_Shutdown(void)
{
}

void Platform_FrameBegin(void)
{
}

void Platform_FrameEnd(void)
{
}

void Platform_PollEvents(void)
{
}

uint32_t Platform_GetTicks(void)
{
    return 0;
}

void Platform_SleepMs(uint32_t ms)
{
    (void)ms;
}
