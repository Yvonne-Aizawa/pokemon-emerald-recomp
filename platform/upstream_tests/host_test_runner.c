/*
 * platform/upstream_tests/host_test_runner.c
 *
 * Runs upstream's test suite (reference/test/; PLAN.md, Phase 19b) on the
 * host. Upstream runs it in mGBA: a test ROM whose runner (test/test_runner.c,
 * CB2_TestRunner) goes through the tests inside the game's main loop, prints
 * results through mGBA's debug registers for mgba-rom-test-hydra to collect,
 * and soft-resets when a test crashes. Here (with test/test_runner.c.patch):
 *
 *  - A child process runs the game's frame loop with CB2_TestRunner as the
 *    main callback. The runner's JumpToAgbMainLoop is a siglongjmp back to
 *    that loop.
 *  - The runner's output lines are printed and counted as Hydra does.
 *  - A test that crashes the child: the parent starts a new one, and the
 *    runner, finding that test in the persistent state (shared memory here),
 *    reports it as CRASH and carries on -- as after a soft reset on the GBA.
 *    A child that stops making progress is killed and handled the same way.
 *  - Timer 2, which drives the per-test timeout, ticks every 60 frames.
 *
 * Usage: pkmemerald-tests [PATTERN]
 *   PATTERN as upstream's `make check TESTS=...`: a test file
 *   ("test/fpmath.c"), a test name prefix, or "*infix". Default: all.
 * Exit status: 0 if every test passed (or failed as expected), else 1;
 * 2 if the runner itself failed.
 * PKM_TESTS_NO_FORK=1 runs the tests in this process, for a debugger (a
 * crash then ends the run).
 *
 * Linux only for now (fork, mmap); see PLAN.md 19b-5 for Windows.
 */

#define _GNU_SOURCE

#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "gba/gba.h"

#include "platform/host_game.h"
#include "platform/host_input.h"
#include "platform/host_render.h"
#include "platform/host_save.h"
#include "platform/irq_timer.h"
#include "platform/main_loop.h"
#include "platform/platform.h"

/* host_test_args.c */
extern char gTestRunnerArgv[256];

/* The game (reference/src/main.c). */
extern void (*gIntrTable[])(void);

/* test/test_runner.c.patch */
void HostTest_SetPersistentState(void *state);

#define TIMER2_INTERRUPT 7          /* gIntrTable index */
#define FRAMES_PER_SECOND 60        /* the runner's timeout is in seconds */
#define STALL_INTERVAL_NS 100000000ul
#define PROGRESS_TIMEOUT_S 60       /* a child with no new frame for this long is stuck */
#define MAX_RESTARTS 1000

/* Shared between the parent and its children (they come and go). */
struct Shared
{
    uint32_t persistent;            /* struct PersistentTestRunnerState */
    volatile uint32_t heartbeat;    /* frames run by the current child */
    char name[256];                 /* the current test (":N") */
    char location[256];             /* its file:line (":L") */
    char output[16384];             /* lines since the last result */
    uint32_t outputLength;
    bool outputTruncated;
    uint32_t results, passes, fails, knownFails, knownFailsPassing, expectedFails,
             expectedFailsPassing, assumptionFails, todos;
    char failures[32768];           /* "name (file:line)" lines, for the summary */
    uint32_t failuresLength;
};

static struct Shared *sShared;
static sigjmp_buf sMainLoop;

/* --------------------------------------------------------------------- */
/* Output, as mgba-rom-test-hydra prints it                               */
/* --------------------------------------------------------------------- */

char gHostTestDebugString[0x100];

static void Append(char *buffer, uint32_t *length, size_t size, const char *text, bool *truncated)
{
    size_t n = strlen(text);

    if (*length + n + 2 > size)
    {
        if (truncated != NULL)
            *truncated = true;
        return;
    }
    memcpy(buffer + *length, text, n);
    *length += (uint32_t)n;
    buffer[(*length)++] = '\n';
    buffer[*length] = '\0';
}

static void AddFailure(void)
{
    char line[600];

    snprintf(line, sizeof(line), "  %s (%s)", sShared->name, sShared->location);
    Append(sShared->failures, &sShared->failuresLength, sizeof(sShared->failures), line, NULL);
}

static void HandleLine(const char *line)
{
    if (line[0] == ':' && line[1] != '\0')
    {
        uint32_t *counter = NULL;
        bool listed = false;

        switch (line[1])
        {
        case 'N':
            snprintf(sShared->name, sizeof(sShared->name), "%s", line + 2);
            return;
        case 'L':
            snprintf(sShared->location, sizeof(sShared->location), "%s", line + 2);
            return;
        case 'P': counter = &sShared->passes; break;
        case 'E': counter = &sShared->expectedFails; break;
        case 'K': counter = &sShared->knownFails; break;
        case 'T': counter = &sShared->todos; break;
        case 'U': counter = &sShared->knownFailsPassing; listed = true; break;
        case 'V': counter = &sShared->expectedFailsPassing; listed = true; break;
        case 'A': counter = &sShared->assumptionFails; listed = true; break;
        case 'F': counter = &sShared->fails; listed = true; break;
        }
        if (counter != NULL)
        {
            (*counter)++;
            sShared->results++;
            if (listed)
                AddFailure();
            printf("%s: %s\n", sShared->name, line + 2);
            if (sShared->outputLength != 0)
                fputs(sShared->output, stdout);
            if (sShared->outputTruncated)
                printf("[Further test output was truncated.]\n");
            sShared->outputLength = 0;
            sShared->output[0] = '\0';
            sShared->outputTruncated = false;
            snprintf(sShared->name, sizeof(sShared->name), "WAITING...");
            return;
        }
    }
    Append(sShared->output, &sShared->outputLength, sizeof(sShared->output), line, &sShared->outputTruncated);
}

/* mGBA clears its debug string after printing it, and the runner relies on
 * that: it doesn't terminate a line that ends without '\n'. */
void HostTest_DebugFlush(void)
{
    gHostTestDebugString[sizeof(gHostTestDebugString) - 1] = '\0';
    HandleLine(gHostTestDebugString);
    memset(gHostTestDebugString, 0, sizeof(gHostTestDebugString));
}

void HostTest_Exit(uint8_t exitCode)
{
    fflush(stdout);
    _exit(exitCode);
}

/* --------------------------------------------------------------------- */
/* The child: the game's frame loop                                       */
/* --------------------------------------------------------------------- */

void HostTest_JumpToMainLoop(void)
{
    IrqTimer_Disarm();
    siglongjmp(sMainLoop, 1);
}

static void RunTestFrame(void)
{
    static uint32_t sFrames;

    Host_SetKeypad(0);
    IrqTimer_Arm(HostMain_RaiseVBlankInterrupts, STALL_INTERVAL_NS);
    HostMain_RunFrame();
    IrqTimer_Disarm();
    Host_RenderFrame(Platform_GetFramebuffer());
    sShared->heartbeat++;

    /* Timer 2: the runner's one-second tick (timeout), in game time. */
    if (++sFrames % FRAMES_PER_SECOND == 0 && (REG_TM2CNT_H & TIMER_ENABLE)
     && REG_IME && (REG_IE & INTR_FLAG_TIMER2) && gIntrTable[TIMER2_INTERRUPT] != NULL)
        gIntrTable[TIMER2_INTERRUPT]();
}

/* Game code waiting for V-blank inside a frame: end the frame there. */
static void WaitForVBlankInsideFrame(void)
{
    IrqTimer_Disarm();
    HostMain_RaiseVBlankInterrupts();
    Host_RenderFrame(Platform_GetFramebuffer());
    sShared->heartbeat++;
    IrqTimer_Arm(HostMain_RaiseVBlankInterrupts, STALL_INTERVAL_NS);
}

static void RunChild(const char *saveDir)
{
    struct PlatformConfig config = { .dataDir = "assets", .saveDir = saveDir, .scale = 1 };

    setvbuf(stdout, NULL, _IOLBF, 0);
    if (Platform_Init(&config) != 0)
    {
        fprintf(stderr, "pkmemerald-tests: platform init failed\n");
        _exit(2);
    }
    Host_SaveOpen(saveDir);
    HostTest_SetPersistentState(&sShared->persistent);
    gHostVBlankIntrWaitHandler = WaitForVBlankInsideFrame;
    Host_SetPacing(false);
    Host_SetFrameCallback(RunTestFrame);

    AgbMain();  /* the main callback is CB2_TestRunner (main.c.patch) */
    sigsetjmp(sMainLoop, 1);
    Host_RunMainLoop(0);
    fprintf(stderr, "pkmemerald-tests: the main loop ended before the runner did\n");
    _exit(2);
}

/* --------------------------------------------------------------------- */
/* The parent: restarts crashed children, prints the summary              */
/* --------------------------------------------------------------------- */

/* Waits for the child; kills it if it stops making progress. Returns its
 * wait status. */
static int WaitForChild(pid_t pid)
{
    uint32_t lastHeartbeat = sShared->heartbeat;
    time_t lastProgress = time(NULL);
    int status;

    for (;;)
    {
        struct timespec pause = { 0, 50 * 1000 * 1000 };

        if (waitpid(pid, &status, WNOHANG) == pid)
            return status;
        if (sShared->heartbeat != lastHeartbeat)
        {
            lastHeartbeat = sShared->heartbeat;
            lastProgress = time(NULL);
        }
        else if (time(NULL) - lastProgress > PROGRESS_TIMEOUT_S)
        {
            printf("%s: no progress for %d s; stopping it\n", sShared->name, PROGRESS_TIMEOUT_S);
            kill(pid, SIGKILL);
        }
        nanosleep(&pause, NULL);
    }
}

static void PrintSummary(void)
{
    printf("\n%u tests: %u passed, %u failed, %u known failing, %u expected failures,"
           " %u assumptions failed, %u to do\n",
           sShared->results, sShared->passes, sShared->fails, sShared->knownFails,
           sShared->expectedFails, sShared->assumptionFails, sShared->todos);
    if (sShared->knownFailsPassing + sShared->expectedFailsPassing != 0)
        printf("%u known failing and %u expected-to-fail tests now pass\n",
               sShared->knownFailsPassing, sShared->expectedFailsPassing);
    if (sShared->failuresLength != 0)
        printf("Not passing:\n%s", sShared->failures);
}

int main(int argc, char **argv)
{
    char saveDir[] = "/tmp/pkmemerald-tests-XXXXXX";
    int restarts;

    if (argc > 2 || (argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)))
    {
        fprintf(argc > 2 ? stderr : stdout,
                "usage: %s [PATTERN]\n"
                "  PATTERN: a test file (test/fpmath.c), a test name prefix, or *infix\n", argv[0]);
        return argc > 2 ? 2 : 0;
    }
    if (argc == 2)
        snprintf(gTestRunnerArgv, sizeof(gTestRunnerArgv), "%s", argv[1]);

    setenv("SDL_VIDEODRIVER", "dummy", 0);
    setenv("SDL_AUDIODRIVER", "dummy", 0);
    sShared = mmap(NULL, sizeof(*sShared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (sShared == MAP_FAILED || mkdtemp(saveDir) == NULL)
    {
        perror("pkmemerald-tests");
        return 2;
    }
    memset(sShared, 0, sizeof(*sShared));
    snprintf(sShared->name, sizeof(sShared->name), "WAITING...");
    fflush(stdout);
    if (getenv("PKM_TESTS_NO_FORK") != NULL)
        RunChild(saveDir);

    for (restarts = 0; restarts <= MAX_RESTARTS; restarts++)
    {
        pid_t pid = fork();
        int status;

        if (pid < 0)
        {
            perror("pkmemerald-tests: fork");
            return 2;
        }
        if (pid == 0)
            RunChild(saveDir);

        status = WaitForChild(pid);
        if (WIFEXITED(status))
        {
            PrintSummary();
            rmdir(saveDir);
            return WEXITSTATUS(status);
        }
        /* Crashed (or stuck): the next child's runner reports it as CRASH --
         * unless no test had started, which restarting can't fix. */
        if (sShared->persistent == 0)
        {
            fprintf(stderr, "pkmemerald-tests: the runner crashed before any test started"
                            " (signal %d); PKM_TESTS_NO_FORK=1 runs it in one process for a debugger\n",
                    WIFSIGNALED(status) ? WTERMSIG(status) : 0);
            rmdir(saveDir);
            return 2;
        }
        printf("%s: the test process ended with signal %d; restarting after it\n",
               sShared->name, WIFSIGNALED(status) ? WTERMSIG(status) : 0);
        fflush(stdout);
    }
    fprintf(stderr, "pkmemerald-tests: too many restarts\n");
    return 2;
}
