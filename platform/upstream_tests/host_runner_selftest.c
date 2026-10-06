/*
 * platform/upstream_tests/host_runner_selftest.c
 *
 * A test that must fail, built into pkmemerald-tests: the upstream-detects-
 * failure test (CMakeLists.txt) checks that the host runner reports it as a
 * failure, with upstream's message, and exits 1.
 */

#include "global.h"
#include "test/test.h"

TEST("Host runner self-test: a failing EXPECT_EQ fails")
{
    EXPECT_EQ(1, 2);
}
