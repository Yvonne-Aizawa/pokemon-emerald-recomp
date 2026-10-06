/*
 * platform/tests/test_dma_free.c
 *
 * A heap block freed while a DMA3 copy from it is still queued (as the game
 * does, e.g. CopyWindowToVram then RemoveWindow before the V-blank): the copy
 * must still deliver the bytes the block held when it was freed, even if the
 * memory is reused before the copy runs (dma3_manager.c.patch,
 * malloc.c.patch).
 */

#include <stdio.h>
#include <string.h>

#include "global.h"
#include "dma3.h"
/* By path: "malloc.h" would find libc's first (see CMakeLists.txt). */
#include "../../reference/include/malloc.h"

static int sFailures;

static void Check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        sFailures++;
}

static u8 sDest[256];

int main(void)
{
    u8 expected[256];
    u8 *block, *reuse;
    int i;

    InitHeap(gHeap, HEAP_SIZE);
    ClearDma3Requests();

    block = Alloc(sizeof(expected));
    for (i = 0; i < (int)sizeof(expected); i++)
        expected[i] = block[i] = (u8)(i * 7 + 3);

    Check(RequestDma3Copy(block, sDest, sizeof(expected), 0) >= 0, "copy queued");
    Free(block);

    /* The freed memory is reused before the V-blank runs the copy. */
    reuse = Alloc(sizeof(expected));
    Check(reuse == block, "the freed block is reused (the case that matters)");
    memset(reuse, 0xEE, sizeof(expected));

    ProcessDma3Requests();
    Check(memcmp(sDest, expected, sizeof(expected)) == 0, "the copy delivers the bytes as they were when freed");

    /* A copy whose source is still allocated is unaffected. */
    memset(sDest, 0, sizeof(sDest));
    Check(RequestDma3Copy(reuse, sDest, sizeof(expected), 0) >= 0, "second copy queued");
    ProcessDma3Requests();
    Check(sDest[0] == 0xEE && sDest[255] == 0xEE, "a copy from a live block reads the block itself");
    Free(reuse);

    printf(sFailures ? "dma_free: %d FAILED\n" : "dma_free: all tests passed\n", sFailures);
    return sFailures != 0;
}
