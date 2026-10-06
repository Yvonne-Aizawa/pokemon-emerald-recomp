/*
 * platform/tests/test_heap_asan.c
 *
 * With AddressSanitizer (PKM_SANITIZE=address...) and PKM_ASAN_HEAP=1, the
 * game's own heap marks its unused memory off-limits (malloc.c.patch), so
 * overruns and use-after-free inside gHeap are reported. Checks which memory
 * is marked; in other builds there is nothing to check.
 */

#include <stdio.h>
#include <stdlib.h>
#if PKM_ASAN
#include <sanitizer/asan_interface.h>
#endif

#include "global.h"
/* By path: "malloc.h" would find libc's first (the game's headers are
 * searched after the system's; see CMakeLists.txt). */
#include "../../reference/include/malloc.h"

static int sFailures;

#if PKM_ASAN
static void Check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        sFailures++;
}
#endif

int main(void)
{
#if PKM_ASAN
    u8 *a, *b, *c;

    setenv("PKM_ASAN_HEAP", "1", 1);  /* before the first heap operation reads it */
    InitHeap(gHeap, HEAP_SIZE);
    Check(__asan_address_is_poisoned(gHeap + HEAP_SIZE / 2), "a fresh heap is off-limits");

    a = Alloc(40);
    b = Alloc(100);
    Check(!__asan_region_is_poisoned(a, 40) && !__asan_region_is_poisoned(b, 100), "allocations are usable");
    Check(__asan_address_is_poisoned(b + 100 + 64), "memory past the last allocation is off-limits");

    Free(a);
    Check(__asan_address_is_poisoned(a + 8), "freed memory is off-limits");
    Check(!__asan_region_is_poisoned(b, 100), "freeing one block leaves the next usable");

    Free(b);
    Check(__asan_address_is_poisoned(b + 8), "freed and merged memory is off-limits");

    c = AllocZeroed(200);
    Check(!__asan_region_is_poisoned(c, 200) && c[199] == 0, "memory is usable again once reallocated");
    Free(c);

    printf(sFailures ? "heap_asan: %d FAILED\n" : "heap_asan: all tests passed\n", sFailures);
#else
    printf("heap_asan: not an AddressSanitizer build, nothing to check\n");
#endif
    return sFailures != 0;
}
