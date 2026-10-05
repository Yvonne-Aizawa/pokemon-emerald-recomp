/*
 * platform/src/irq_timer_posix.c
 *
 * POSIX implementation of irq_timer.h: an interval timer (SIGALRM).
 * A Windows build will need its own (e.g. a timer thread that suspends the
 * game thread), see Phase 17.
 */

#define _POSIX_C_SOURCE 200809L

#include "platform/irq_timer.h"
#include "platform/main_loop.h"

#include <signal.h>
#include <stddef.h>
#include <string.h>
#include <sys/time.h>

static void (*volatile sRaise)(void);

static void OnAlarm(int sig)
{
    (void)sig;
    if (sRaise != NULL)
        sRaise();
}

static void SetTimer(long usec)
{
    struct itimerval timer;

    memset(&timer, 0, sizeof(timer));
    timer.it_value.tv_usec = usec;
    timer.it_interval.tv_usec = usec;
    setitimer(ITIMER_REAL, &timer, NULL);
}

void IrqTimer_Arm(void (*raise)(void))
{
    static int sInstalled;

    if (!sInstalled)
    {
        struct sigaction sa;

        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = OnAlarm;
        sigemptyset(&sa.sa_mask);
        /* System calls the game makes (none, normally) resume afterwards. */
        sa.sa_flags = SA_RESTART;
        sigaction(SIGALRM, &sa, NULL);
        sInstalled = 1;
    }
    sRaise = raise;
    SetTimer(HOST_FRAME_NS / 1000);
}

void IrqTimer_Disarm(void)
{
    SetTimer(0);
    sRaise = NULL;
}
