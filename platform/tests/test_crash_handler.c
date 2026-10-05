/*
 * platform/tests/test_crash_handler.c
 *
 * A child process installs the crash handler and dereferences NULL inside a
 * static function. The child must still die from SIGSEGV, and the report
 * must name the signal, the static function, the frame counter and a
 * watched function pointer.
 */

#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "platform/crash_handler.h"

static int sFailures;
static void (*sWatchedCallback)(void);

static void Check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        sFailures++;
}

static uint32_t FakeFrameCounter(void)
{
    return 4242;
}

static void SomeGameCallback(void)
{
}

/* noinline + volatile: keep a real frame and a real NULL store. */
static __attribute__((noinline)) void CrashOnPurpose(void)
{
    volatile int *volatile ptr = NULL;
    *ptr = 1;
}

int main(void)
{
    char path[] = "/tmp/pkmemerald-test-crash-XXXXXX";
    static char report[32 * 1024];
    int fd, status;
    pid_t child;
    FILE *f;
    size_t len;

    fd = mkstemp(path);
    if (fd < 0)
        return 1;
    close(fd);

    child = fork();
    if (child == 0)
    {
        /* Keep the test output clean: the report also goes to stderr. */
        if (freopen("/dev/null", "w", stderr) == NULL)
            _exit(2);
        Crash_Install(path);
        Crash_SetFrameCounter(FakeFrameCounter);
        sWatchedCallback = SomeGameCallback;
        Crash_WatchFunctionPointer("callback", (void *const *)&sWatchedCallback);
        CrashOnPurpose();
        _exit(0);
    }
    waitpid(child, &status, 0);
    Check(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV, "the process still dies from SIGSEGV");

    f = fopen(path, "r");
    len = f != NULL ? fread(report, 1, sizeof(report) - 1, f) : 0;
    if (f != NULL)
        fclose(f);
    report[len] = '\0';
    unlink(path);

    Check(strstr(report, "SIGSEGV") != NULL, "report names the signal");
    Check(strstr(report, "crashed at: ") != NULL && strstr(report, "CrashOnPurpose") != NULL,
          "report names the crashing static function");
    Check(strstr(report, "frame: 4242") != NULL, "report includes the frame counter");
    Check(strstr(report, "callback: ") != NULL && strstr(report, "SomeGameCallback") != NULL,
          "report symbolizes watched function pointers");
    Check(strstr(report, "main") != NULL, "call stack reaches main");

    if (sFailures)
        printf("--- report ---\n%s", report);
    printf(sFailures ? "crash_handler: %d FAILED\n" : "crash_handler: all tests passed\n", sFailures);
    return sFailures != 0;
}
