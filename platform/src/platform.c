/*
 * platform/src/platform.c
 *
 * Headless POSIX implementation of platform.h.
 *
 * Phase 4: no window, input or audio -- just time, sleeping and a quit flag
 * (set by SIGINT/SIGTERM) so the main loop can run. Phase 5 replaces this
 * with an SDL2 backend.
 */

#define _POSIX_C_SOURCE 200809L

#include "platform/platform.h"

#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <time.h>

static struct PlatformConfig sConfig;
static uint64_t sStartNs;
static volatile sig_atomic_t sQuitRequested;

static uint64_t MonotonicNs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

static void OnQuitSignal(int sig)
{
    (void)sig;
    sQuitRequested = 1;
}

int Platform_Init(const struct PlatformConfig *config)
{
    struct sigaction sa = {0};

    sConfig = *config;
    sStartNs = MonotonicNs();
    sQuitRequested = 0;

    sa.sa_handler = OnQuitSignal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    return 0;
}

void Platform_Shutdown(void)
{
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
}

const char *Platform_GetDataDir(void)
{
    return sConfig.dataDir;
}

const char *Platform_GetSaveDir(void)
{
    return sConfig.saveDir;
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

bool Platform_QuitRequested(void)
{
    return sQuitRequested != 0;
}

void Platform_RequestQuit(void)
{
    sQuitRequested = 1;
}

uint32_t Platform_GetTicks(void)
{
    return (uint32_t)(Platform_GetTimeNs() / 1000000u);
}

uint64_t Platform_GetTimeNs(void)
{
    return MonotonicNs() - sStartNs;
}

void Platform_SleepMs(uint32_t ms)
{
    Platform_SleepNs((uint64_t)ms * 1000000u);
}

void Platform_SleepNs(uint64_t ns)
{
    struct timespec ts = {
        .tv_sec = (time_t)(ns / 1000000000u),
        .tv_nsec = (long)(ns % 1000000000u),
    };

    while (nanosleep(&ts, &ts) == -1 && errno == EINTR && !sQuitRequested)
        ;
}
