/*
 * platform/upstream_tests/host_known_failures.c
 *
 * Upstream tests that fail on the host for a reason that isn't a port bug:
 * they check something that differs between the GBA and a PC by design.
 * test/test_runner.c.patch reports them as KNOWN_FAILING (which doesn't fail
 * the run) with the reason here; one that passes is reported as
 * KNOWN_FAILING_PASS, so the list can't go stale. Port bugs are fixed, not
 * listed (PLAN.md, Phase 19b).
 */

#include <stddef.h>
#include <string.h>

/* Benchmarks: EXPECT_FASTER times two versions of some ARM code with the
 * GBA's hardware timers and expects the optimised one to be faster on the
 * GBA's CPU. On the host the code is x86 and the timers count frames, not
 * cycles, so both usually measure 0. */
#define BENCHMARK "an ARM benchmark (EXPECT_FASTER), timed with GBA timers"

/* Save blocks: on the GBA (-mabi=apcs-gnu) every struct's size is rounded up
 * to 4 bytes; on x86 it isn't, so the save blocks are smaller on the host
 * (e.g. SaveBlock1: 15496 bytes, 15568 on the GBA). See PLAN.md, Phase 19b
 * (save layout). */
#define SAVE_LAYOUT "the host's save block layout differs from the GBA's (struct size rounding)"

static const struct { const char *name, *reason; } sKnownFailures[] = {
    { "BuildOamBuffer faster on already-sorted max sprites", BENCHMARK },
    { "BuildOamBuffer faster with max sprites (equal y/subpriority)", BENCHMARK },
    { "BuildOamBuffer faster with max sprites (random y/subpriority)", BENCHMARK },
    { "BuildOamBuffer faster with mix of sprites", BENCHMARK },
    { "BuildOamBuffer faster with no sprites", BENCHMARK },
    { "Optimised GetMonData", BENCHMARK },
    { "Optimised SetMonData", BENCHMARK },
    { "RandomUniform mul-based faster than mod-based (compile-time)", BENCHMARK },
    { "RandomUniform mul-based faster than mod-based (run-time)", BENCHMARK },
    { "SaveBlock1 is backwards compatible", SAVE_LAYOUT },
    { "SaveBlock2 is backwards compatible", SAVE_LAYOUT },
    { "SaveBlock3 is backwards compatible", SAVE_LAYOUT },
};

const char *HostTest_KnownFailure(const char *name)
{
    size_t i;

    for (i = 0; i < sizeof(sKnownFailures) / sizeof(sKnownFailures[0]); i++)
        if (strcmp(name, sKnownFailures[i].name) == 0)
            return sKnownFailures[i].reason;
    return NULL;
}
