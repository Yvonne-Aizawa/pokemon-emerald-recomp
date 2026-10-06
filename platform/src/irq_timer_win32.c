/*
 * platform/src/irq_timer_win32.c
 *
 * Windows implementation of irq_timer.h. Windows has no signals, so a timer
 * thread does what SIGALRM does on POSIX: every interval it suspends the
 * game thread and, if that thread is running the game's own code, redirects
 * it into IrqTrampoline, which saves every register (and the SSE/x87 state),
 * runs `raise`, restores everything and returns to where the game was.
 *
 * Unlike a signal, the interrupt is only injected while the game thread is
 * inside this executable's code (the busy-waits it exists for are game
 * code), never inside Windows, the C runtime or SDL: redirecting a thread
 * that is blocked in a system call is unsafe, and those aren't re-entrant
 * anyway. Otherwise it is retried shortly after.
 *
 * 32-bit x86 only, like the rest of the port.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "platform/irq_timer.h"

#include <stdint.h>

#if !defined(__i386__)
#error "irq_timer_win32.c: the trampoline is 32-bit x86 only"
#endif

/* When the game thread isn't in game code, try again after this long. */
#define RETRY_MS 1

static void (*volatile sRaise)(void);
static volatile LONG sArmed;
static volatile DWORD sIntervalMs;
/* Set when an interrupt is injected, cleared once it has run: one at a
 * time, like the hardware's IME while a handler runs. */
static volatile LONG sPending;
static HANDLE sGameThread, sTimerThread, sWake;

/* Where the game thread was; the trampoline returns there. Named for the
 * assembly below. */
volatile uintptr_t gIrqResumeAddress __asm__("irq_resume_address");

/* The game's code: the image from its base to the end of .text (MinGW's
 * linker script symbols). */
extern char sImageBase[] __asm__("__image_base__");
extern char sTextEnd[] __asm__("etext");

void IrqTimer_RunInterrupt(void) __asm__("irq_run_interrupt");
void IrqTimer_RunInterrupt(void)
{
    void (*raise)(void) = sRaise;

    if (raise != NULL)
        raise();
    InterlockedExchange(&sPending, 0);
}

/* Entered with the interrupted thread's registers; returns to
 * irq_resume_address. The C call gets a 16-byte aligned stack; fxsave keeps
 * the SSE registers the game's floating-point code uses. */
void IrqTrampoline(void) __asm__("irq_trampoline");
__asm__(
    ".text\n"
    ".globl irq_trampoline\n"
    "irq_trampoline:\n"
    "    pushl irq_resume_address\n"
    "    pushfl\n"
    "    pushal\n"
    "    movl %esp, %ebp\n"
    "    subl $512, %esp\n"
    "    andl $-16, %esp\n"
    "    fxsave (%esp)\n"
    "    cld\n"
    "    call irq_run_interrupt\n"
    "    fxrstor (%esp)\n"
    "    movl %ebp, %esp\n"
    "    popal\n"
    "    popfl\n"
    "    ret\n"
);

/* With the game thread suspended: true if the interrupt was injected. */
static BOOL Inject(void)
{
    CONTEXT context;

    context.ContextFlags = CONTEXT_CONTROL;
    if (!GetThreadContext(sGameThread, &context))
        return FALSE;
    if (context.Eip < (uintptr_t)sImageBase || context.Eip >= (uintptr_t)sTextEnd)
        return FALSE;
    gIrqResumeAddress = context.Eip;
    context.Eip = (uintptr_t)IrqTrampoline;
    if (!SetThreadContext(sGameThread, &context))
        return FALSE;
    InterlockedExchange(&sPending, 1);
    return TRUE;
}

static DWORD WINAPI TimerThread(void *unused)
{
    DWORD wait = INFINITE;

    (void)unused;
    for (;;)
    {
        /* Arm/disarm wakes us: the period restarts from there. */
        if (WaitForSingleObject(sWake, wait) == WAIT_OBJECT_0)
        {
            wait = sArmed ? sIntervalMs : INFINITE;
            continue;
        }
        if (!sArmed)
        {
            wait = INFINITE;
            continue;
        }
        wait = sIntervalMs;
        if (sPending)
        {
            wait = RETRY_MS;
            continue;
        }
        if (SuspendThread(sGameThread) == (DWORD)-1)
            continue;
        if (!sArmed || !Inject())
            wait = RETRY_MS;
        ResumeThread(sGameThread);
    }
    return 0;
}

void IrqTimer_Arm(void (*raise)(void), unsigned long intervalNs)
{
    if (sTimerThread == NULL)
    {
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &sGameThread,
                        THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, 0);
        sWake = CreateEventA(NULL, FALSE, FALSE, NULL);
        sTimerThread = CreateThread(NULL, 64 * 1024, TimerThread, NULL, 0, NULL);
        SetThreadPriority(sTimerThread, THREAD_PRIORITY_TIME_CRITICAL);
    }
    sIntervalMs = (DWORD)((intervalNs + 999999) / 1000000);
    sRaise = raise;
    InterlockedExchange(&sArmed, 1);
    SetEvent(sWake);
}

void IrqTimer_Disarm(void)
{
    InterlockedExchange(&sArmed, 0);
    sRaise = NULL;
    SetEvent(sWake);
}
