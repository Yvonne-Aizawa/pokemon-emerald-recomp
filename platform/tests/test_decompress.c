/*
 * platform/tests/test_decompress.c
 *
 * Functional test for the host decompressor implementations.
 *
 * Compresses a known input using the GBA LZ77 / RL format, decompresses
 * it with the host implementation, and checks the output matches.
 *
 * Not part of Phase 2's "Done when" but worth having so Phase 3 doesn't
 * discover broken decompressors the hard way.
 */

/* Include gba/gba.h FIRST so the host type aliases (u8, u16, ...) are
 * defined before libc's <string.h> (transitively) pulls in
 * <strings.h>, which on this codebase collides with the local
 * refrence/include/strings.h shadow. */
#include "gba/gba.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int failures = 0;

#define CHECK_EQ(a, b, msg) do {                                          \
    if ((a) != (b)) {                                                     \
        fprintf(stderr, "FAIL %s: %s = 0x%x, expected 0x%x\n",            \
                msg, #a, (unsigned)(a), (unsigned)(b));                   \
        failures++;                                                       \
    }                                                                     \
} while (0)

/* LZ77-compress a buffer of size `size` into `out`. Returns total
 * compressed size (header + body). Format: 4-byte header, then a
 * stream of flag bytes (1 bit per element, MSB first) where 0=literal
 * (1 byte) and 1=back-reference (2 bytes: 12-bit offset+1, 4-bit len+3). */
static uint32_t lz77_compress(const uint8_t *in, uint32_t size, uint8_t *out)
{
    uint32_t header = 0x10u | (size << 8);
    out[0] = header & 0xFF;
    out[1] = (header >> 8) & 0xFF;
    out[2] = (header >> 16) & 0xFF;
    out[3] = (header >> 24) & 0xFF;

    uint32_t opos = 4;
    uint32_t ipos = 0;
    while (ipos < size) {
        uint8_t flags = 0;
        uint32_t flag_pos = opos++;
        uint8_t mask = 0x80;
        for (int bit = 0; bit < 8 && ipos < size; bit++, mask >>= 1) {
            /* Find best back-reference in the lookback window. */
            uint32_t best_off = 0, best_len = 0;
            uint32_t max_off = (ipos < 0x1000) ? ipos : 0x1000;
            for (uint32_t off = 1; off <= max_off; off++) {
                uint32_t k = 0;
                while (k < 18 && ipos + k < size && in[ipos + k] == in[ipos - off + k])
                    k++;
                if (k >= 3 && k > best_len) {
                    best_off = off;
                    best_len = k;
                    if (k == 18) break;
                }
            }
            if (best_len >= 3) {
                uint16_t b = (uint16_t)(((best_len - 3) << 12) | ((best_off - 1) & 0x0FFF));
                out[opos++] = b >> 8;
                out[opos++] = b & 0xFF;
                flags |= mask;
                ipos += best_len;
            } else {
                out[opos++] = in[ipos++];
            }
        }
        out[flag_pos] = flags;
    }
    return opos;
}

static int test_lz77_roundtrip(void)
{
    /* A string with lots of repetition. */
    const char *src = "Hello world! Hello world! Hello world! "
                      "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
                      "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
                      "The quick brown fox jumps over the lazy dog. "
                      "The quick brown fox jumps over the lazy dog. ";
    uint32_t size = (uint32_t)strlen(src);
    uint8_t compressed[1024];
    uint8_t decompressed[1024] = {0};

    uint32_t csize = lz77_compress((const uint8_t *)src, size, compressed);
    fprintf(stderr, "LZ77: %u bytes input -> %u bytes compressed (%u%%)\n",
            size, csize, csize * 100 / size);

    LZ77UnCompWram((const u32 *)compressed, decompressed);
    if (memcmp(decompressed, src, size) != 0) {
        fprintf(stderr, "LZ77: round-trip MISMATCH\n");
        failures++;
        return 0;
    }
    fprintf(stderr, "LZ77: round-trip OK\n");
    return 1;
}

static int test_rl_roundtrip(void)
{
    /* Run-length test: a long run of one character plus a few others.
     * Expected layout:
     *   bytes 0-249  : 250 'A's
     *   bytes 250-259: 10 'B's
     *   bytes 260-269: 10 'C's
     *   bytes 270-279: 10 'D's
     *   bytes 280-289: 10 'E's
     *   bytes 290-299: 10 'F's
     */
    uint8_t src[300];
    memset(src, 0, sizeof(src));
    memset(src, 'A', 250);
    for (int i = 0; i < 10; i++) src[250 + i] = 'B';
    for (int i = 0; i < 10; i++) src[260 + i] = 'C';
    for (int i = 0; i < 10; i++) src[270 + i] = 'D';
    for (int i = 0; i < 10; i++) src[280 + i] = 'E';
    for (int i = 0; i < 10; i++) src[290 + i] = 'F';

    uint32_t size = 300;

    /* Manually build an RL stream. */
    uint8_t compressed[64];
    uint32_t header = 0x30u | (size << 8);
    compressed[0] = header & 0xFF;
    compressed[1] = (header >> 8) & 0xFF;
    compressed[2] = (header >> 16) & 0xFF;
    compressed[3] = (header >> 24) & 0xFF;
    /* Run of 250 'A's: needs two pairs (max 256 each). */
    compressed[4] = 'A'; compressed[5] = 250; compressed[6] = 0; compressed[7] = 0;
    compressed[8] = 'B'; compressed[9] = 10;  compressed[10] = 0; compressed[11] = 0;
    compressed[12] = 'C'; compressed[13] = 10; compressed[14] = 0; compressed[15] = 0;
    compressed[16] = 'D'; compressed[17] = 10; compressed[18] = 0; compressed[19] = 0;
    compressed[20] = 'E'; compressed[21] = 10; compressed[22] = 0; compressed[23] = 0;
    compressed[24] = 'F'; compressed[25] = 10; compressed[26] = 0; compressed[27] = 0;

    uint8_t decompressed[400] = {0};
    RLUnCompWram((const u32 *)compressed, decompressed);

    /* Find first mismatch. */
    int mismatch_at = -1;
    for (uint32_t i = 0; i < size; i++) {
        if (decompressed[i] != src[i]) { mismatch_at = (int)i; break; }
    }
    if (mismatch_at >= 0) {
        fprintf(stderr, "RL: round-trip MISMATCH at byte %d\n", mismatch_at);
        fprintf(stderr, "  expected: ...%02x %02x %02x %02x...\n",
                src[mismatch_at], src[mismatch_at+1], src[mismatch_at+2], src[mismatch_at+3]);
        fprintf(stderr, "  got:      ...%02x %02x %02x %02x...\n",
                decompressed[mismatch_at], decompressed[mismatch_at+1],
                decompressed[mismatch_at+2], decompressed[mismatch_at+3]);
        failures++;
        return 0;
    }
    fprintf(stderr, "RL: round-trip OK\n");
    return 1;
}

int main(void)
{
    test_lz77_roundtrip();
    test_rl_roundtrip();
    if (failures == 0) {
        fprintf(stderr, "decompress: all tests passed\n");
        return 0;
    }
    fprintf(stderr, "decompress: %d failure(s)\n", failures);
    return 1;
}
