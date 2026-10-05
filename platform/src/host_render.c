/*
 * platform/src/host_render.c
 *
 * GBA display emulation; see platform/host_render.h.
 *
 * A scanline renderer following the GBA's PPU: each of the 160 visible lines
 * is drawn from the display registers as they are at that line, then that
 * line's H-blank happens (H-blank DMA, H-blank interrupt), so per-line
 * effects -- scroll waves, window shapes, battle transitions -- work as on
 * hardware.
 *
 * Implemented: video modes 0-5 (text, affine and bitmap backgrounds),
 * sprites (regular and affine, semi-transparent, OBJ window), layer priority,
 * mosaic, windows 0/1/OBJ/outside, and colour effects (alpha blending,
 * brighten, darken). Not emulated: the per-line sprite pixel budget (the
 * hardware drops sprites past ~1210 cycles per line; the game stays under it).
 *
 * Timing: Host_RenderFrame runs right after the game's V-blank interrupt, and
 * does what the GBA does between that interrupt and the next frame's logic:
 * the H-blanks of the V-blank lines 160-227, then lines 0-159.
 */

#include "platform/host_render.h"
#include "platform/platform.h"

#include "global.h"
#include "main.h"

#define WIDTH  PLATFORM_SCREEN_WIDTH
#define HEIGHT PLATFORM_SCREEN_HEIGHT
#define TOTAL_LINES 228

/* Layer ids, as numbered in the window and blend registers. */
#define LAYER_BG0 0
#define LAYER_OBJ 4
#define LAYER_BD  5

/* A line-buffer pixel: BGR555 colour, or TRANSPARENT. */
#define TRANSPARENT 0x8000

/* Window/blend control bits per pixel: BG0-3, OBJ, colour effect. */
#define WINDOW_ALL 0x3F
#define WINDOW_EFFECT 0x20

#define BLEND_NONE     0
#define BLEND_ALPHA    1
#define BLEND_BRIGHTEN 2
#define BLEND_DARKEN   3

void Host_DmaHBlank(void);  /* host_dma.c */

static u16 Reg(u32 offset)
{
    return *(vu16 *)(REG_BASE + offset);
}

static void SetReg(u32 offset, u16 value)
{
    *(vu16 *)(REG_BASE + offset) = value;
}

static u32 Reg32(u32 offset)
{
    return Reg(offset) | ((u32)Reg(offset + 2) << 16);
}

static const u8 *Vram(void)
{
    return (const u8 *)VRAM;
}

static u16 Vram16(u32 addr)
{
    return Vram()[addr] | (Vram()[addr + 1] << 8);
}

static u16 BgPalette(u32 index)
{
    return ((const u16 *)BG_PLTT)[index] & 0x7FFF;
}

static u16 ObjPalette(u32 index)
{
    return ((const u16 *)OBJ_PLTT)[index] & 0x7FFF;
}

/* --------------------------------------------------------------------- */
/* Affine reference points                                               */
/*                                                                        */
/* BG2/BG3's BGxX/BGxY are latched into internal registers at V-blank and  */
/* advanced by PB/PD after every line; writing the register mid-frame     */
/* reloads it. Mosaic reuses the reference of the mosaic block's first   */
/* line, so we keep each line's values.                                   */
/* --------------------------------------------------------------------- */

struct AffineRef
{
    s32 x, y;               /* internal reference point, 20.8 fixed */
    u32 latchedX, latchedY; /* register values last loaded */
    s32 lineX[HEIGHT], lineY[HEIGHT];
};

static struct AffineRef sAffine[2];  /* BG2, BG3 */

static s32 SignExtend28(u32 value)
{
    return (s32)(value << 4) >> 4;
}

static u32 AffineBase(int bg)
{
    return bg == 2 ? REG_OFFSET_BG2PA : REG_OFFSET_BG3PA;
}

static void LatchAffine(int bg)
{
    struct AffineRef *ref = &sAffine[bg - 2];
    u32 base = AffineBase(bg);

    ref->latchedX = Reg32(base + 8);
    ref->latchedY = Reg32(base + 12);
    ref->x = SignExtend28(ref->latchedX);
    ref->y = SignExtend28(ref->latchedY);
}

static void UpdateAffineForLine(int bg, int y)
{
    struct AffineRef *ref = &sAffine[bg - 2];
    u32 base = AffineBase(bg);

    if (Reg32(base + 8) != ref->latchedX || Reg32(base + 12) != ref->latchedY)
        LatchAffine(bg);
    ref->lineX[y] = ref->x;
    ref->lineY[y] = ref->y;
    /* Advance by PB, PD for the next line. */
    ref->x += (s16)Reg(base + 2);
    ref->y += (s16)Reg(base + 6);
}

/* --------------------------------------------------------------------- */
/* Background line drawing                                               */
/* --------------------------------------------------------------------- */

struct Mosaic
{
    int bgH, bgV;
};

static struct Mosaic GetMosaic(void)
{
    u16 m = Reg(REG_OFFSET_MOSAIC);
    struct Mosaic mosaic = { (m & 0xF) + 1, ((m >> 4) & 0xF) + 1 };
    return mosaic;
}

static void DrawTextBgLine(int bg, int y, u16 *out)
{
    u16 cnt = Reg(REG_OFFSET_BG0CNT + 2 * bg);
    u32 hofs = Reg(REG_OFFSET_BG0HOFS + 4 * bg) & 0x1FF;
    u32 vofs = Reg(REG_OFFSET_BG0HOFS + 4 * bg + 2) & 0x1FF;
    u32 charBase = ((cnt >> 2) & 3) * 0x4000;
    u32 screenBase = ((cnt >> 8) & 0x1F) * 0x800;
    bool32 is8bpp = cnt & BGCNT_256COLOR;
    u32 width = (cnt & 0x4000) ? 512 : 256;
    u32 height = (cnt & 0x8000) ? 512 : 256;
    struct Mosaic mosaic = GetMosaic();
    bool32 useMosaic = cnt & BGCNT_MOSAIC;
    u32 srcY = useMosaic ? y - y % mosaic.bgV : (u32)y;
    u32 yy = (srcY + vofs) & (height - 1);
    int x;

    for (x = 0; x < WIDTH; x++)
    {
        u32 srcX = useMosaic ? x - x % mosaic.bgH : (u32)x;
        u32 xx = (srcX + hofs) & (width - 1);
        u32 block = (xx >> 8) + (yy >> 8) * (width >> 8);
        u32 entryAddr = screenBase + block * 0x800 + ((yy & 0xFF) >> 3) * 64 + ((xx & 0xFF) >> 3) * 2;
        u16 entry, tile;
        u32 px, py, addr, index;

        out[x] = TRANSPARENT;
        if (entryAddr + 1 >= BG_VRAM_SIZE)
            continue;
        entry = Vram16(entryAddr);
        tile = entry & 0x3FF;
        px = xx & 7;
        py = yy & 7;
        if (entry & 0x400)
            px = 7 - px;
        if (entry & 0x800)
            py = 7 - py;

        if (is8bpp)
        {
            addr = charBase + tile * 64 + py * 8 + px;
            if (addr >= BG_VRAM_SIZE)
                continue;
            index = Vram()[addr];
        }
        else
        {
            addr = charBase + tile * 32 + py * 4 + px / 2;
            if (addr >= BG_VRAM_SIZE)
                continue;
            index = (Vram()[addr] >> ((px & 1) * 4)) & 0xF;
            if (index != 0)
                index += (entry >> 12) * 16;
        }
        if (index != 0)
            out[x] = BgPalette(index);
    }
}

/* Affine sampling shared by affine tile BGs and the bitmap modes: the
 * texture coordinate of pixel x on line y, in whole pixels. */
static void AffineCoords(int bg, int y, int x, s32 *tx, s32 *ty)
{
    u16 cnt = Reg(REG_OFFSET_BG0CNT + 2 * bg);
    u32 base = AffineBase(bg);
    struct Mosaic mosaic = GetMosaic();
    int line = y, col = x;

    if (cnt & BGCNT_MOSAIC)
    {
        line = y - y % mosaic.bgV;
        col = x - x % mosaic.bgH;
    }
    *tx = (sAffine[bg - 2].lineX[line] + (s16)Reg(base) * col) >> 8;      /* PA */
    *ty = (sAffine[bg - 2].lineY[line] + (s16)Reg(base + 4) * col) >> 8;  /* PC */
}

static void DrawAffineBgLine(int bg, int y, u16 *out)
{
    u16 cnt = Reg(REG_OFFSET_BG0CNT + 2 * bg);
    u32 charBase = ((cnt >> 2) & 3) * 0x4000;
    u32 screenBase = ((cnt >> 8) & 0x1F) * 0x800;
    s32 size = 128 << (cnt >> 14);
    bool32 wrap = cnt & BGCNT_WRAP;
    int x;

    for (x = 0; x < WIDTH; x++)
    {
        s32 tx, ty;
        u32 mapAddr, addr, index;

        out[x] = TRANSPARENT;
        AffineCoords(bg, y, x, &tx, &ty);
        if (wrap)
        {
            tx &= size - 1;
            ty &= size - 1;
        }
        else if (tx < 0 || ty < 0 || tx >= size || ty >= size)
        {
            continue;
        }
        mapAddr = screenBase + (ty >> 3) * (size >> 3) + (tx >> 3);
        if (mapAddr >= BG_VRAM_SIZE)
            continue;
        addr = charBase + Vram()[mapAddr] * 64 + (ty & 7) * 8 + (tx & 7);
        if (addr >= BG_VRAM_SIZE)
            continue;
        index = Vram()[addr];
        if (index != 0)
            out[x] = BgPalette(index);
    }
}

/* Modes 3-5: BG2 is a bitmap, still positioned by the affine registers. */
static void DrawBitmapBgLine(int mode, int y, u16 *out)
{
    u32 page = (Reg(REG_OFFSET_DISPCNT) & 0x10) ? 0xA000 : 0;
    s32 w = (mode == 5) ? 160 : WIDTH;
    s32 h = (mode == 5) ? 128 : HEIGHT;
    int x;

    for (x = 0; x < WIDTH; x++)
    {
        s32 tx, ty;

        out[x] = TRANSPARENT;
        AffineCoords(2, y, x, &tx, &ty);
        if (tx < 0 || ty < 0 || tx >= w || ty >= h)
            continue;
        if (mode == 4)
        {
            u32 index = Vram()[page + ty * WIDTH + tx];
            if (index != 0)
                out[x] = BgPalette(index);
        }
        else
        {
            out[x] = Vram16((mode == 5 ? page : 0) + (ty * w + tx) * 2) & 0x7FFF;
        }
    }
}

/* --------------------------------------------------------------------- */
/* Sprites (OBJ)                                                         */
/* --------------------------------------------------------------------- */

#define OBJ_TILE_BASE 0x10000  /* sprite tiles live in VRAM's last 32 KiB */

#define OBJ_MODE_NORMAL 0
#define OBJ_MODE_SEMI   1  /* semi-transparent: always alpha-blends */
#define OBJ_MODE_WINDOW 2  /* not drawn; its pixels form the OBJ window */

/* One line of sprite output. */
struct ObjLine
{
    u16 color[WIDTH];   /* TRANSPARENT where no sprite pixel */
    u8 priority[WIDTH];
    bool8 semi[WIDTH];
    bool8 window[WIDTH]; /* covered by an OBJ-window sprite */
};

/* Width and height by [shape][size]: square, horizontal, vertical. */
static const u8 sObjSizes[3][4][2] = {
    { {  8,  8 }, { 16, 16 }, { 32, 32 }, { 64, 64 } },
    { { 16,  8 }, { 32,  8 }, { 32, 16 }, { 64, 32 } },
    { {  8, 16 }, {  8, 32 }, { 16, 32 }, { 32, 64 } },
};

static u16 Oam16(u32 index)
{
    return ((const u16 *)OAM)[index];
}

/* Colour index of sprite pixel (tx, ty) within a w-pixel-wide sprite whose
 * graphics start at tile `baseTile`; 0 = transparent. */
static u32 ObjPixel(u32 baseTile, u32 w, u32 tx, u32 ty, bool32 is8bpp, bool32 map1D, int mode)
{
    u32 tileStep = is8bpp ? 2 : 1;  /* tile numbers count 32-byte units */
    u32 rowStride = map1D ? (w / 8) * tileStep : 32;
    u32 tile = (baseTile + (ty / 8) * rowStride + (tx / 8) * tileStep) & 0x3FF;
    u32 addr;

    /* In the bitmap modes the lower half of sprite VRAM holds the bitmap. */
    if (mode >= 3 && tile < 512)
        return 0;
    addr = OBJ_TILE_BASE + tile * 32;
    if (is8bpp)
        return Vram()[addr + (ty & 7) * 8 + (tx & 7)];
    return (Vram()[addr + (ty & 7) * 4 + (tx & 7) / 2] >> ((tx & 1) * 4)) & 0xF;
}

static void DrawObjLine(int y, struct ObjLine *line)
{
    u16 dispcnt = Reg(REG_OFFSET_DISPCNT);
    int mode = dispcnt & 7;
    bool32 map1D = dispcnt & DISPCNT_OBJ_1D_MAP;
    u16 mosaicReg = Reg(REG_OFFSET_MOSAIC);
    int mosaicH = ((mosaicReg >> 8) & 0xF) + 1;
    int mosaicV = ((mosaicReg >> 12) & 0xF) + 1;
    int i, x;

    for (x = 0; x < WIDTH; x++)
    {
        line->color[x] = TRANSPARENT;
        line->priority[x] = 4;
        line->semi[x] = FALSE;
        line->window[x] = FALSE;
    }
    if (!(dispcnt & DISPCNT_OBJ_ON))
        return;

    for (i = 0; i < 128; i++)
    {
        u16 attr0 = Oam16(i * 4), attr1 = Oam16(i * 4 + 1), attr2 = Oam16(i * 4 + 2);
        bool32 affine = attr0 & 0x100;
        int objMode = (attr0 >> 10) & 3;
        int shape = attr0 >> 14;
        int w, h, boxW, boxH, top, left, lineInBox, lx;
        bool32 mosaic = attr0 & 0x1000;
        bool32 is8bpp = attr0 & 0x2000;
        u32 baseTile = attr2 & 0x3FF;
        int priority = (attr2 >> 10) & 3;
        u32 palBank = (attr2 >> 12) * 16;
        s32 pa = 0x100, pb = 0, pc = 0, pd = 0x100;

        if ((!affine && (attr0 & 0x200)) || objMode == 3 || shape == 3)
            continue;  /* hidden, or invalid */

        w = sObjSizes[shape][attr1 >> 14][0];
        h = sObjSizes[shape][attr1 >> 14][1];
        boxW = w;
        boxH = h;
        if (affine && (attr0 & 0x200))  /* double-size bounding box */
        {
            boxW *= 2;
            boxH *= 2;
        }

        /* Y is 8 bits and wraps around the 256-line space. */
        top = attr0 & 0xFF;
        lineInBox = (u8)(y - top);
        if (lineInBox >= boxH)
            continue;
        /* Mosaic snaps to the screen's grid, not the sprite's. */
        if (mosaic)
        {
            lineInBox -= y % mosaicV;
            if (lineInBox < 0)
                lineInBox = 0;
        }
        left = attr1 & 0x1FF;
        if (left >= WIDTH)
            left -= 512;

        if (affine)
        {
            u32 group = ((attr1 >> 9) & 0x1F) * 16;
            pa = (s16)Oam16(group + 3);
            pb = (s16)Oam16(group + 7);
            pc = (s16)Oam16(group + 11);
            pd = (s16)Oam16(group + 15);
        }

        for (lx = 0; lx < boxW; lx++)
        {
            int sx = left + lx;
            int col = lx;
            s32 tx, ty;
            u32 index;

            if (sx < 0 || sx >= WIDTH)
                continue;
            if (mosaic)
                col = (sx - sx % mosaicH) - left;
            if (col < 0)
                col = 0;

            if (affine)
            {
                /* Rotate around the box centre; the texture centre is the
                 * sprite's centre. */
                s32 cx = col - boxW / 2, cy = lineInBox - boxH / 2;
                tx = ((pa * cx + pb * cy) >> 8) + w / 2;
                ty = ((pc * cx + pd * cy) >> 8) + h / 2;
                if (tx < 0 || ty < 0 || tx >= w || ty >= h)
                    continue;
            }
            else
            {
                tx = (attr1 & 0x1000) ? w - 1 - col : col;
                ty = (attr1 & 0x2000) ? h - 1 - lineInBox : lineInBox;
            }

            index = ObjPixel(baseTile, w, tx, ty, is8bpp, map1D, mode);
            if (index == 0)
                continue;

            if (objMode == OBJ_MODE_WINDOW)
            {
                line->window[sx] = TRUE;
                continue;
            }
            /* Among sprites, a pixel is only taken over by a strictly better
             * priority, so on ties the lower OAM index stays in front. */
            if (line->color[sx] != TRANSPARENT && priority >= line->priority[sx])
                continue;
            line->color[sx] = ObjPalette(is8bpp ? index : palBank + index);
            line->priority[sx] = priority;
            line->semi[sx] = (objMode == OBJ_MODE_SEMI);
        }
    }
}

/* --------------------------------------------------------------------- */
/* Windows and colour effects                                             */
/* --------------------------------------------------------------------- */

/* GBATEK: an end past the screen edge, or start > end, means "to the edge". */
static bool32 InRange(int pos, int start, int end, int limit)
{
    if (end > limit || start > end)
        end = limit;
    return pos >= start && pos < end;
}

static bool32 InWindow(u32 hOffset, u32 vOffset, int x, int y)
{
    u16 h = Reg(hOffset), v = Reg(vOffset);
    return InRange(x, h >> 8, h & 0xFF, WIDTH) && InRange(y, v >> 8, v & 0xFF, HEIGHT);
}

/* Per pixel: which layers and effects the windows allow. */
/* Window priority: WIN0, then WIN1, then the OBJ window, then outside. */
static void ComputeWindowLine(int y, const struct ObjLine *obj, u8 *mask)
{
    u16 dispcnt = Reg(REG_OFFSET_DISPCNT);
    u16 winIn = Reg(REG_OFFSET_WININ);
    u16 winOut = Reg(REG_OFFSET_WINOUT);
    int x;

    for (x = 0; x < WIDTH; x++)
    {
        if (!(dispcnt & (DISPCNT_WIN0_ON | DISPCNT_WIN1_ON | DISPCNT_OBJWIN_ON)))
            mask[x] = WINDOW_ALL;
        else if ((dispcnt & DISPCNT_WIN0_ON) && InWindow(REG_OFFSET_WIN0H, REG_OFFSET_WIN0V, x, y))
            mask[x] = winIn & 0x3F;
        else if ((dispcnt & DISPCNT_WIN1_ON) && InWindow(REG_OFFSET_WIN1H, REG_OFFSET_WIN1V, x, y))
            mask[x] = (winIn >> 8) & 0x3F;
        else if ((dispcnt & DISPCNT_OBJWIN_ON) && obj->window[x])
            mask[x] = (winOut >> 8) & 0x3F;
        else
            mask[x] = winOut & 0x3F;
    }
}

static u16 Blend(u16 a, u16 b, u32 eva, u32 evb)
{
    u32 r = ((a & 0x1F) * eva + (b & 0x1F) * evb) >> 4;
    u32 g = (((a >> 5) & 0x1F) * eva + ((b >> 5) & 0x1F) * evb) >> 4;
    u32 bl = (((a >> 10) & 0x1F) * eva + ((b >> 10) & 0x1F) * evb) >> 4;

    return (r > 31 ? 31 : r) | (g > 31 ? 31 : g) << 5 | (bl > 31 ? 31 : bl) << 10;
}

static u16 Brighten(u16 c, u32 evy)
{
    u32 r = c & 0x1F, g = (c >> 5) & 0x1F, b = (c >> 10) & 0x1F;

    r += ((31 - r) * evy) >> 4;
    g += ((31 - g) * evy) >> 4;
    b += ((31 - b) * evy) >> 4;
    return r | g << 5 | b << 10;
}

static u16 Darken(u16 c, u32 evy)
{
    u32 r = c & 0x1F, g = (c >> 5) & 0x1F, b = (c >> 10) & 0x1F;

    r -= (r * evy) >> 4;
    g -= (g * evy) >> 4;
    b -= (b * evy) >> 4;
    return r | g << 5 | b << 10;
}

/* --------------------------------------------------------------------- */
/* Compositing                                                           */
/* --------------------------------------------------------------------- */

static uint32_t ToRgb(u16 color)
{
    uint32_t r = color & 0x1F;
    uint32_t g = (color >> 5) & 0x1F;
    uint32_t b = (color >> 10) & 0x1F;

    /* Spread 5 bits over 8 so that 31 maps to 255. */
    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);
    return (r << 16) | (g << 8) | b;
}

static void RenderLine(int y, uint32_t *out)
{
    static u16 sBgLine[4][WIDTH];
    static struct ObjLine sObjLine;
    u8 window[WIDTH];
    u16 dispcnt = Reg(REG_OFFSET_DISPCNT);
    int mode = dispcnt & 7;
    u16 bldcnt = Reg(REG_OFFSET_BLDCNT);
    u16 bldalpha = Reg(REG_OFFSET_BLDALPHA);
    int blendMode = (bldcnt >> 6) & 3;
    u32 eva = bldalpha & 0x1F, evb = (bldalpha >> 8) & 0x1F, evy = Reg(REG_OFFSET_BLDY) & 0x1F;
    int order[4], count = 0;
    int bg, prio, x, i;
    u16 backdrop = BgPalette(0);

    if (eva > 16) eva = 16;
    if (evb > 16) evb = 16;
    if (evy > 16) evy = 16;

    if (dispcnt & DISPCNT_FORCED_BLANK)
    {
        for (x = 0; x < WIDTH; x++)
            out[x] = 0xFFFFFF;
        return;
    }

    for (bg = 2; bg <= 3; bg++)
        UpdateAffineForLine(bg, y);

    /* Draw the backgrounds this mode has and DISPCNT enables, collecting
     * them front to back: lower priority value first, then lower BG number. */
    for (prio = 0; prio < 4; prio++)
    {
        for (bg = 0; bg < 4; bg++)
        {
            bool32 present = (mode == 0) || (mode == 1 && bg <= 2) || (mode == 2 && bg >= 2) || (mode >= 3 && bg == 2);

            if (!present || !(dispcnt & (DISPCNT_BG0_ON << bg)) || (Reg(REG_OFFSET_BG0CNT + 2 * bg) & 3) != prio)
                continue;
            order[count++] = bg;
        }
    }
    for (i = 0; i < count; i++)
    {
        bg = order[i];
        if (mode >= 3)
            DrawBitmapBgLine(mode, y, sBgLine[bg]);
        else if (mode == 0 || (mode == 1 && bg < 2))
            DrawTextBgLine(bg, y, sBgLine[bg]);
        else
            DrawAffineBgLine(bg, y, sBgLine[bg]);
    }

    DrawObjLine(y, &sObjLine);
    ComputeWindowLine(y, &sObjLine, window);

    for (x = 0; x < WIDTH; x++)
    {
        /* Top two visible layers at this pixel (the backdrop is always last). */
        u16 color[2] = { backdrop, backdrop };
        int layer[2] = { LAYER_BD, LAYER_BD };
        int found = 0;
        bool32 objPending = sObjLine.color[x] != TRANSPARENT && (window[x] & (1 << LAYER_OBJ));
        bool32 topIsSemiObj = FALSE;
        u16 result;

        /* Walk the backgrounds front to back; a sprite pixel goes in front of
         * every background whose priority is the same or lower. */
        for (i = 0; i <= count && found < 2; i++)
        {
            int bgPriority = (i < count) ? (Reg(REG_OFFSET_BG0CNT + 2 * order[i]) & 3) : 4;

            if (objPending && sObjLine.priority[x] <= bgPriority)
            {
                if (found == 0)
                    topIsSemiObj = sObjLine.semi[x];
                color[found] = sObjLine.color[x];
                layer[found] = LAYER_OBJ;
                found++;
                objPending = FALSE;
                if (found == 2)
                    break;
            }
            if (i == count)
                break;
            bg = order[i];
            if (!(window[x] & (1 << bg)) || sBgLine[bg][x] == TRANSPARENT)
                continue;
            color[found] = sBgLine[bg][x];
            layer[found] = LAYER_BG0 + bg;
            found++;
        }

        result = color[0];
        if (window[x] & WINDOW_EFFECT)
        {
            /* Semi-transparent sprites alpha-blend with any valid second
             * target, whatever the blend mode and first-target bits say. */
            if (topIsSemiObj && (bldcnt & (0x100 << layer[1])))
                result = Blend(color[0], color[1], eva, evb);
            else if (bldcnt & (1 << layer[0]))
            {
                if (blendMode == BLEND_ALPHA && (bldcnt & (0x100 << layer[1])))
                    result = Blend(color[0], color[1], eva, evb);
                else if (blendMode == BLEND_BRIGHTEN)
                    result = Brighten(color[0], evy);
                else if (blendMode == BLEND_DARKEN)
                    result = Darken(color[0], evy);
            }
        }
        out[x] = ToRgb(result);
    }
}

/* --------------------------------------------------------------------- */
/* Frame: the H-blank timeline                                           */
/* --------------------------------------------------------------------- */

static void HBlank(int line, bool32 visible)
{
    u16 dispstat = Reg(REG_OFFSET_DISPSTAT);

    SetReg(REG_OFFSET_VCOUNT, line);
    SetReg(REG_OFFSET_DISPSTAT, dispstat | DISPSTAT_HBLANK);

    /* H-blank DMA runs only on visible lines; the interrupt on every line. */
    if (visible)
        Host_DmaHBlank();
    if (REG_IME && (REG_IE & INTR_FLAG_HBLANK) && (dispstat & DISPSTAT_HBLANK_INTR) && gIntrTable[3])
        gIntrTable[3]();

    SetReg(REG_OFFSET_DISPSTAT, Reg(REG_OFFSET_DISPSTAT) & ~DISPSTAT_HBLANK);
}

void Host_RenderFrame(uint32_t *framebuffer)
{
    int line;

    /* V-blank lines (the game's V-blank interrupt has already run). */
    SetReg(REG_OFFSET_DISPSTAT, Reg(REG_OFFSET_DISPSTAT) | DISPSTAT_VBLANK);
    for (line = HEIGHT; line < TOTAL_LINES; line++)
        HBlank(line, FALSE);
    SetReg(REG_OFFSET_DISPSTAT, Reg(REG_OFFSET_DISPSTAT) & ~DISPSTAT_VBLANK);

    /* The affine reference points are latched at the start of each frame. */
    LatchAffine(2);
    LatchAffine(3);

    for (line = 0; line < HEIGHT; line++)
    {
        SetReg(REG_OFFSET_VCOUNT, line);
        RenderLine(line, framebuffer + line * WIDTH);
        HBlank(line, TRUE);
    }

    /* The game's next frame runs from here, which on hardware is
     * V-blank-adjacent; code that waits for "VCOUNT >= 160" mustn't spin. */
    SetReg(REG_OFFSET_VCOUNT, HEIGHT);
}
