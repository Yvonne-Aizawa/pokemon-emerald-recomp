/*
 * platform/src/irq_timer_posix.c
 *
 * POSIX implementation of irq_timer.h: an interval timer (SIGALRM).
 *
 * SIGALRM goes to the whole process, so any thread may receive it -- e.g.
 * SDL's audio thread. Game interrupts must only ever run on the game's
 * thread (they preempt game code, never run beside it), so a thread that
 * catches it passes it on.
 * A Windows build will need its own (e.g. a timer thread that suspends the
 * game thread), see Phase 17.
 */

#define _POSIX_C_SOURCE 200809L

#include "platform/irq_timer.h"
#include "platform/main_loop.h"

#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <string.h>
#include <sys/time.h>

static void (*volatile sRaise)(void);
static pthread_t sGameThread;

static void OnAlarm(int sig)
{
    if (!pthread_equal(pthread_self(), sGameThread))
    {
        pthread_kill(sGameThread, sig);
        return;
    }
    if (sRaise != NULL)
        sRaise();
}

static void SetTimer(unsigned long usec)
{
    struct itimerval timer;

    memset(&timer, 0, sizeof(timer));
    timer.it_value.tv_sec = usec / 1000000;
    timer.it_value.tv_usec = usec % 1000000;
    timer.it_interval = timer.it_value;
    setitimer(ITIMER_REAL, &timer, NULL);
}

void IrqTimer_Arm(void (*raise)(void), unsigned long intervalNs)
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
    sGameThread = pthread_self();
    sRaise = raise;
    SetTimer(intervalNs / 1000);
}

void IrqTimer_Disarm(void)
{
    SetTimer(0);
    sRaise = NULL;
}
