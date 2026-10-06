/*
 * platform/include/platform/host_test_ram.h
 *
 * Memory for upstream's battle test framework (PLAN.md, Phase 19b), mapped by
 * the host test runner (platform/upstream_tests/host_test_runner.c) where the
 * GBA's EWRAM is. The framework keeps addresses of the tests' locals and
 * results in 27-bit fields (struct QueuedHPEvent and friends in
 * include/test/battle.h), which GBA RAM addresses fit and the host's own data
 * and stack don't. test/test_runner_battle.c.patch puts the runner's state and
 * the test functions' stack here.
 */
#ifndef PLATFORM_HOST_TEST_RAM_H
#define PLATFORM_HOST_TEST_RAM_H

#define HOST_TEST_RAM 0x02000000
#define HOST_TEST_RAM_SIZE 0x100000

#endif
