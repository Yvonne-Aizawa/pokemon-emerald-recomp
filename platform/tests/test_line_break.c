/*
 * platform/tests/test_line_break.c
 *
 * BuildNewString must ignore trailing lines that hold no words
 * (platform/patches/line_break.c.patch): BreakSubStringAutomatic can lay a
 * string out on fewer lines than it allocated, leaving lines with
 * numWords == 0 and an unset words pointer. Reading through it crashed while
 * a battle message was printed, and the last real line wrote a line break
 * over the string's EOS.
 */

#include <stdio.h>
#include <string.h>

#include "global.h"
#include "constants/characters.h"
#include "line_break.h"

//  "AB CD": two words of two characters each
static u8 sStr[6];
static struct StringWord sWords[2];

static void Reset(void)
{
    sStr[0] = CHAR_A; sStr[1] = CHAR_B; sStr[2] = CHAR_SPACE;
    sStr[3] = CHAR_C; sStr[4] = CHAR_D; sStr[5] = EOS;
    sWords[0] = (struct StringWord){ .startIndex = 0, .length = 2, .width = 12 };
    sWords[1] = (struct StringWord){ .startIndex = 3, .length = 2, .width = 12 };
}

static int Check(const char *what, const u8 *expected)
{
    if (memcmp(sStr, expected, sizeof(sStr)) != 0)
    {
        printf("FAIL: %s: got %02x %02x %02x %02x %02x %02x\n", what,
               sStr[0], sStr[1], sStr[2], sStr[3], sStr[4], sStr[5]);
        return 1;
    }
    printf("ok  : %s\n", what);
    return 0;
}

int main(void)
{
    int failed = 0;

    //  Both words on line 0; lines 1 and 2 were allocated but never filled.
    Reset();
    struct StringLine oneUsed[3] = {
        { .words = &sWords[0], .numWords = 2, .spaceWidth = 6 },
        { .words = NULL, .numWords = 0, .spaceWidth = 6 },
        { .words = NULL, .numWords = 0, .spaceWidth = 6 },
    };
    BuildNewString(oneUsed, 3, 2, sStr, SHOW_SCROLL_PROMPT);
    const u8 unchanged[] = { CHAR_A, CHAR_B, CHAR_SPACE, CHAR_C, CHAR_D, EOS };
    failed |= Check("unused trailing lines are skipped, EOS kept", unchanged);

    //  One word per line, plus an unused third line.
    Reset();
    struct StringLine twoUsed[3] = {
        { .words = &sWords[0], .numWords = 1, .spaceWidth = 6 },
        { .words = &sWords[1], .numWords = 1, .spaceWidth = 6 },
        { .words = NULL, .numWords = 0, .spaceWidth = 6 },
    };
    BuildNewString(twoUsed, 3, 2, sStr, SHOW_SCROLL_PROMPT);
    const u8 broken[] = { CHAR_A, CHAR_B, CHAR_NEWLINE, CHAR_C, CHAR_D, EOS };
    failed |= Check("two used lines break with a newline, no scroll prompt", broken);

    if (failed)
        return 1;
    printf("line_break: all tests passed\n");
    return 0;
}
