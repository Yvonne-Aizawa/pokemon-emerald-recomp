/*
 * platform/tests/test_crash_handler.c
 *
 * A child process installs the crash handler and dereferences NULL inside a
 * static function. The child must still die from SIGSEGV, and the report
 * must name the signal, the static function, the frame counter and a
 * watched function pointer. The report goes to a new file named after the
 * local date and time, and a later crash doesn't replace it.
 */

#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
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

/* Runs a child that crashes with the handler writing to `dir`; returns its
 * wait status. */
static int CrashChild(const char *dir)
{
    int status;
    pid_t child = fork();

    if (child == 0)
    {
        /* Keep the test output clean: the report also goes to stderr. */
        if (freopen("/dev/null", "w", stderr) == NULL)
            _exit(2);
        Crash_Install(dir);
        Crash_SetFrameCounter(FakeFrameCounter);
        sWatchedCallback = SomeGameCallback;
        Crash_WatchFunctionPointer("callback", (void *const *)&sWatchedCallback);
        CrashOnPurpose();
        _exit(0);
    }
    waitpid(child, &status, 0);
    return status;
}

/* Number of files in `dir`; the alphabetically first one's name goes to
 * `first` (`size` bytes). */
static int ListReports(const char *dir, char *first, size_t size)
{
    DIR *d = opendir(dir);
    struct dirent *entry;
    int count = 0;

    first[0] = '\0';
    if (d == NULL)
        return 0;
    while ((entry = readdir(d)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;
        if (count++ == 0 || strcmp(entry->d_name, first) < 0)
            snprintf(first, size, "%s", entry->d_name);
    }
    closedir(d);
    return count;
}

static void RemoveReports(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *entry;
    char path[512];

    if (d == NULL)
        return;
    while ((entry = readdir(d)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        unlink(path);
    }
    closedir(d);
    rmdir(dir);
}

int main(void)
{
    char dir[] = "/tmp/pkmemerald-test-crash-XXXXXX";
    static char report[32 * 1024];
    char name[256], path[512], expectedDate[32];
    time_t before, after;
    struct tm local;
    int status;
    FILE *f;
    size_t len;

    if (mkdtemp(dir) == NULL)
        return 1;

    before = time(NULL);
    status = CrashChild(dir);
    after = time(NULL);
    Check(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV, "the process still dies from SIGSEGV");

    Check(ListReports(dir, name, sizeof(name)) == 1, "one crash writes one report file");
    len = strlen(name);
    Check(len == 19 + strlen(CRASH_REPORT_SUFFIX) && strcmp(name + 19, CRASH_REPORT_SUFFIX) == 0
       && name[4] == '-' && name[7] == '-' && name[10] == '_' && name[13] == '-' && name[16] == '-',
          "report file is named YYYY-MM-DD_HH-MM-SS" CRASH_REPORT_SUFFIX);
    /* The crash happened between `before` and `after`, in local time. */
    localtime_r(&before, &local);
    strftime(expectedDate, sizeof(expectedDate), "%Y-%m-%d_%H-%M-%S", &local);
    Check(strncmp(name, expectedDate, 19) >= 0, "report time is not before the crash");
    localtime_r(&after, &local);
    strftime(expectedDate, sizeof(expectedDate), "%Y-%m-%d_%H-%M-%S", &local);
    Check(strncmp(name, expectedDate, 19) <= 0, "report time is not after the crash");

    snprintf(path, sizeof(path), "%s/%s", dir, name);
    f = fopen(path, "r");
    len = f != NULL ? fread(report, 1, sizeof(report) - 1, f) : 0;
    if (f != NULL)
        fclose(f);
    report[len] = '\0';

    /* File names have one-second resolution. */
    sleep(1);
    CrashChild(dir);
    Check(ListReports(dir, name, sizeof(name)) == 2, "a later crash keeps the earlier report");
    RemoveReports(dir);

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
