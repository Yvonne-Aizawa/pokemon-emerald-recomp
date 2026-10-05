/*
 * platform/src/host_hal.c
 *
 * Host-side implementations for the GBA HAL stubs.
 *
 * Provides:
 *   1. The backing memory regions (g_host_ewram, g_host_vram, g_host_oam,
 *      g_host_palette, g_host_mmio) that the macros in
 *      platform/include/gba/defines.h point at.
 *   2. Host implementations of the GBA BIOS functions declared in
 *      gba/syscall.h (CpuSet, CpuFastSet, LZ77UnCompWram/Vram, RLUnComp*,
 *      BgAffineSet, ObjAffineSet, Sqrt, ArcTan2, Div, VBlankIntrWait,
 *      SoftReset, RegisterRamReset, MultiBoot).
 *   3. Host implementations of the AGB / mGBA / NoCash debug print
 *      functions declared in gba/isagbprint.h.
 *
 * Phase 2: this file exists to make the static lib `pkmemerald-core` link.
 *         Most of the implementations are minimal but correct; the
 *         complex ones (LZ77, RL, BgAffineSet) are written from scratch
 *         in pure C99 and will be exercised in Phase 3+.
 *
 * Phase 5: the print functions get connected to SDL_main-loop stderr.
 *         Phase 7:  the heap layout is exposed here.
 *         Phase 9:  REG_* writes here are wired to the renderer.
 */

#include "gba/gba.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* --------------------------------------------------------------------- */
/* 1. Backing memory regions                                           */
/* --------------------------------------------------------------------- */

uint8_t  g_host_ewram[EWRAM_SIZE];   /* 256 KiB */
uint8_t  g_host_iwram[IWRAM_SIZE];   /*  32 KiB */
uint8_t  g_host_vram[VRAM_SIZE];     /*  96 KiB */
uint8_t  g_host_oam[OAM_SIZE];       /*   1 KiB */
uint8_t  g_host_palette[PLTT_SIZE];  /*   1 KiB */
volatile uint16_t g_host_mmio[0x400];/*   2 KiB, addresses 0x04000000..0x040007FF */

void *g_host_sound_info = NULL;      /* Phase 13 sets this. */
uint16_t g_host_intr_check;          /* INTR_CHECK  */
void *g_host_intr_vector;            /* INTR_VECTOR */

/* --------------------------------------------------------------------- */
/* 2. GBA BIOS syscall replacements                                    */
/* --------------------------------------------------------------------- */

void SoftReset(u32 resetFlags)
{
    (void)resetFlags;
    /* Real GBA restarts. We just exit. */
    exit(0);
}

void RegisterRamReset(u32 resetFlags)
{
    /* Map the GBA's resetFlags bits onto the host arrays. */
    if (resetFlags & RESET_PALETTE)
        memset(g_host_palette, 0, sizeof(g_host_palette));
    if (resetFlags & RESET_VRAM)
        memset(g_host_vram, 0, sizeof(g_host_vram));
    if (resetFlags & RESET_OAM)
        memset(g_host_oam, 0, sizeof(g_host_oam));
    if (resetFlags & RESET_EWRAM)
        memset(g_host_ewram, 0, sizeof(g_host_ewram));
    if (resetFlags & RESET_IWRAM)
        memset(g_host_iwram, 0, sizeof(g_host_iwram));
    if (resetFlags & RESET_SIO_REGS)
        memset((void *)g_host_mmio + 0x80, 0, 0x40);  /* SIOCNT region */
    if (resetFlags & RESET_SOUND_REGS)
        memset((void *)g_host_mmio + 0x60, 0, 0x30); /* sound region */
    if (resetFlags & RESET_REGS)
        memset((void *)g_host_mmio, 0, sizeof(g_host_mmio));
}

void VBlankIntrWait(void)
{
    /* The host main loop drives frames at 60 Hz; this just yields. */
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 16000000L };  /* ~16 ms */
    nanosleep(&ts, NULL);
}

u16 Sqrt(u32 num)
{
    /* GBA's BIOS Sqrt takes a u32, returns a u16, fixed-point-ish.
     * Use libm. */
    if (num == 0) return 0;
    return (u16)sqrt((double)num);
}

u16 ArcTan2(s16 x, s16 y)
{
    /* GBA returns a u16 in [0, 0xFFFF] covering [0, 2pi). Use atan2. */
    if (x == 0 && y == 0) return 0;
    double a = atan2((double)y, (double)x);
    if (a < 0) a += 2.0 * M_PI;
    /* Scale to 16 bits. */
    return (u16)((a / (2.0 * M_PI)) * 65536.0);
}

s32 Div(s32 num, s32 denom)
{
    /* GBA software division: returns the quotient in low 32 bits,
     * remainder in high 32 bits of the pair. The C wrapper returns just
     * the quotient. */
    if (denom == 0) return 0;  /* GBA returns 0x80080000 on div-by-zero */
    return num / denom;
}

/* CpuSet control-word bits. Already defined in gba/syscall.h:
 *   CPU_SET_SRC_FIXED    0x01000000
 *   CPU_SET_32BIT        0x04000000
 *   CPU_FAST_SET_SRC_FIXED 0x01000000
 */

void (CpuSet)(const void *src, void *dest, u32 control)  /* parenthesized: CpuSet is also a checking macro under MODERN */
{
    u32 count   = control & 0x001FFFFFu;   /* count in transfer units */
    u32 fixed   = control & CPU_SET_SRC_FIXED;
    u32 is32    = control & CPU_SET_32BIT;

    if (is32) {
        u32 *d = (u32 *)dest;
        if (fixed) {
            u32 v = *(const u32 *)src;
            for (u32 i = 0; i < count; i++) d[i] = v;
        } else {
            const u32 *s = (const u32 *)src;
            for (u32 i = 0; i < count; i++) d[i] = s[i];
        }
    } else {
        u16 *d = (u16 *)dest;
        if (fixed) {
            u16 v = *(const u16 *)src;
            for (u32 i = 0; i < count; i++) d[i] = v;
        } else {
            const u16 *s = (const u16 *)src;
            for (u32 i = 0; i < count; i++) d[i] = s[i];
        }
    }
}

void CpuFastSet(const void *src, void *dest, u32 control)
{
    /* CpuFastSet is always 32-bit, count is in 32-bit words (8 bytes per
     * unit in the encoded form: count * 8 bytes = count << 3 bits? No,
     * count is the same: units of 8 bytes).
     * Actually per pret docs: count is the number of 32-bit words to
     * transfer. */
    u32 count = control & 0x001FFFFFu;
    u32 fixed = control & CPU_FAST_SET_SRC_FIXED;

    /* memcpy rather than u32 stores: the host ABI doesn't give every struct
     * the 4-byte alignment that apcs-gnu does on the GBA (see syscall.h). */
    u8 *d = (u8 *)dest;
    if (fixed) {
        u32 v;
        memcpy(&v, src, sizeof(v));
        for (u32 i = 0; i < count; i++) memcpy(d + i * 4, &v, 4);
    } else {
        memmove(d, src, (size_t)count * 4);
    }
}

/* BgAffineSet: build a 2x3 affine matrix from a source spec.
 * GBA fixed-point: pa/pb/pc/pd are s16 in 8.8 fixed-point.
 * dx/dy are s32 in 16.8 fixed-point (24.8 actually — see pret/gba-tech). */
void BgAffineSet(struct BgAffineSrcData *src, struct BgAffineDstData *dest, s32 count)
{
    for (s32 i = 0; i < count; i++) {
        s32 sx    = src[i].sx;
        s32 sy    = src[i].sy;
        s16 angle = src[i].alpha;
        double rad = (angle * 2.0 * M_PI) / 65536.0;
        double c = cos(rad);
        double s = sin(rad);
        dest[i].pa = (s16)((c * sx) * 256.0);
        dest[i].pb = (s16)((s * sx) * 256.0);
        dest[i].pc = (s16)((-s * sy) * 256.0);
        dest[i].pd = (s16)((c * sy) * 256.0);
        dest[i].dx = (s32)(src[i].scrX * 256.0 + (double)src[i].texX
                          - ((double)dest[i].pa * src[i].scrX
                           + (double)dest[i].pb * src[i].scrY));
        dest[i].dy = (s32)(src[i].scrY * 256.0 + (double)src[i].texY
                          - ((double)dest[i].pc * src[i].scrX
                           + (double)dest[i].pd * src[i].scrY));
    }
}

void ObjAffineSet(struct ObjAffineSrcData *src, void *dest, s32 count, s32 offset)
{
    /* The destination is `count` OamData-like affine entries laid out
     * consecutively. offset is in bytes between entries. */
    u8 *d = (u8 *)dest;
    for (s32 i = 0; i < count; i++) {
        s16 sx    = src[i].xScale;
        s16 sy    = src[i].yScale;
        u16 angle = src[i].rotation;
        double rad = (angle * 2.0 * M_PI) / 65536.0;
        double c = cos(rad);
        double s = sin(rad);
        s16 *entry = (s16 *)(d + i * offset);
        entry[0] = (s16)(c * sx);             /* pa */
        entry[1] = (s16)(s * sx);             /* pb */
        entry[2] = (s16)(-s * sy);            /* pc */
        entry[3] = (s16)(c * sy);             /* pd */
    }
}

/* LZ77 (type 0x10) decompression, GBA flavour. The 4-byte header packs
 * the type in the low nibble and the uncompressed length (in bytes) in
 * the upper 24 bits. */
void LZ77UnCompWram(const u32 *src, void *dest)
{
    /* Same algorithm as LZ77UnCompVram — the difference on real GBA is
     * just that Vram uses 16-bit stores (for VRAM access timing). On
     * host, memcpy is fine for both. */
    const u8 *s = (const u8 *)src;
    u32 header = s[0] | (s[1] << 8) | (s[2] << 16) | (s[3] << 24);
    u32 uncompressed_size = header >> 8;

    u8 *d = (u8 *)dest;
    u8 *const d_end = d + uncompressed_size;
    u32 pos = 4;
    while (d < d_end) {
        u8 flags = s[pos++];
        for (u8 mask = 0x80; mask != 0 && d < d_end; mask >>= 1) {
            if (flags & mask) {
                /* Back-reference: 2 bytes, big-endian */
                u16 b = (s[pos] << 8) | s[pos + 1];
                pos += 2;
                u32 length  = (b >> 12) + 3;     /* 3..18 */
                u32 offset  = (b & 0x0FFF) + 1;  /* 1..4096 */
                const u8 *src_b = d - offset;
                for (u32 k = 0; k < length && d < d_end; k++) *d++ = *src_b++;
            } else {
                *d++ = s[pos++];
            }
        }
    }
}

void LZ77UnCompVram(const u32 *src, void *dest)
{
    LZ77UnCompWram(src, dest);
}

/* Run-length (type 0x30) decompression. Header low nibble = 0x30,
 * upper 24 bits = uncompressed size. Body is a sequence of (char, len)
 * pairs. len of 0 means 256. Stops when dest is full. */
void RLUnCompWram(const u32 *src, void *dest)
{
    const u8 *s = (const u8 *)src;
    u32 header = s[0] | (s[1] << 8) | (s[2] << 16) | (s[3] << 24);
    u32 uncompressed_size = header >> 8;

    u8 *d = (u8 *)dest;
    u8 *const d_end = d + uncompressed_size;
    u32 pos = 4;
    while (d < d_end) {
        u8 c = s[pos++];
        u8 n = s[pos++];
        pos += 2;  /* padding to 32-bit boundary per pair */
        u32 count = (n == 0) ? 256 : n;
        for (u32 k = 0; k < count && d < d_end; k++) {
            *d++ = c;
        }
    }
}

void RLUnCompVram(const u32 *src, void *dest)
{
    RLUnCompWram(src, dest);
}

int MultiBoot(struct MultiBootParam *mp)
{
    (void)mp;
    /* No host equivalent. Return a benign error. */
    return -1;
}

/* --------------------------------------------------------------------- */
/* 3. Debug print implementations                                       */
/* --------------------------------------------------------------------- */

#ifndef NDEBUG

bool32 MgbaOpen(void)
{
    /* The original detects mGBA's magic string at 0x04FFFA00 and writes
     * to its debug RAM. On host, we just say "opened" and let MgbaPrintf
     * go to stderr. */
    return 1;
}

void MgbaClose(void) { }

void MgbaPrintf(s32 level, const char *pBuf, ...)
{
    static const char *names[] = { "FATAL", "ERROR", "WARN", "INFO", "DEBUG" };
    int idx = (level >= 0 && level <= 4) ? level : 4;
    fprintf(stderr, "[mGBA %s] ", names[idx]);
    va_list ap;
    va_start(ap, pBuf);
    vfprintf(stderr, pBuf, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void MgbaAssert(const char *pFile, s32 nLine, const char *pExpression, bool32 nStopProgram)
{
    fprintf(stderr, "Assertion failed: %s\n  at %s:%ld\n  stop=%d\n",
            pExpression, pFile, (long)nLine, (int)nStopProgram);
    if (nStopProgram) abort();
}

void NoCashGBAPrintf(const char *pBuf, ...)
{
    va_list ap;
    va_start(ap, pBuf);
    vfprintf(stderr, pBuf, ap);
    va_end(ap);
}

void NoCashGBAAssert(const char *pFile, s32 nLine, const char *pExpression, bool32 nStopProgram)
{
    MgbaAssert(pFile, nLine, pExpression, nStopProgram);
}

void AGBPrintf(const char *pBuf, ...)
{
    va_list ap;
    va_start(ap, pBuf);
    vfprintf(stderr, pBuf, ap);
    va_end(ap);
}

void AGBAssert(const char *pFile, int nLine, const char *pExpression, int nStopProgram)
{
    fprintf(stderr, "AGB assert: %s at %s:%d (stop=%d)\n",
            pExpression, pFile, nLine, nStopProgram);
    if (nStopProgram) abort();
}

void AGBPrintInit(void) { }

#else  /* NDEBUG (release builds) — all no-ops */

void MgbaPrintf(s32 level, const char *pBuf, ...) { (void)level; (void)pBuf; }
void MgbaAssert(const char *pFile, s32 nLine, const char *pExpression, bool32 nStopProgram)
{ (void)pFile; (void)nLine; (void)pExpression; (void)nStopProgram; }
void NoCashGBAPrintf(const char *pBuf, ...) { (void)pBuf; }
void NoCashGBAAssert(const char *pFile, s32 nLine, const char *pExpression, bool32 nStopProgram)
{ (void)pFile; (void)nLine; (void)pExpression; (void)nStopProgram; }
void AGBPrintf(const char *pBuf, ...) { (void)pBuf; }
void AGBAssert(const char *pFile, int nLine, const char *pExpression, int nStopProgram)
{ (void)pFile; (void)nLine; (void)pExpression; (void)nStopProgram; }
void AGBPrintInit(void) { }

#endif /* NDEBUG */

/* Required by the printf format string attributes on debug-only paths. */
