/*
 * platform/upstream_tests/host_test_args.c
 *
 * The test runner's settings. On the GBA they are constants in the test ROM
 * (test/test_runner_args.c, src/test_runner_stub.c) that the Makefile
 * overwrites with patchelf before running it; here host_test_runner.c sets
 * them from its command line. Both upstream files are left out of the test
 * build.
 *
 * Kept apart from the game's headers, which declare these `const`.
 */

#include <stdint.h>

uint8_t gTestRunnerEnabled = 1;
uint8_t gTestRunnerHeadless = 1;    /* timeouts on */
uint8_t gTestRunnerSkipIsFail = 0;
uint8_t gTestRunnerN = 1;           /* number of processes */
uint8_t gTestRunnerI = 0;           /* this process's index */
char gTestRunnerArgv[256];          /* the filter pattern */
