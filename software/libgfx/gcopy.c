/* gcopy.c -- libgfx: pieces of the picture copied, kept and put down again, and read and written as bits.
 *
 * The chip copies dots within its memory (AGCPY). gcopy moves a rectangle of the screen to another place on
 * it; the two may overlap, so the copy starts from the corner that is not written before it is read. gkeep
 * copies a rectangle into the part of the chip's memory that is the program's own, under the screen and the
 * font (video(4): VIOCSPARE), and gstamp copies it back wherever it is wanted: a kept picture costs the
 * program no memory and a few words to draw. They are set out in rows there, each row as high as its first
 * picture; the room of one that is dropped is given to the next that fits it. gmake makes an empty one, and
 * gtarget sends drawing into one instead of onto the screen (the chip's origin is moved to it): a picture made
 * out of sight and then put down whole, or a window's own picture, of which the screen shows what is not
 * covered.
 *
 * gread and gwrite take the dots through the program instead (the chip's RD and MOD, a word of 16 dots at a
 * time): for a picture that is to go to a file or come from one.
 */
#include <unistd.h>
#include <sgtty.h>
#include <gfx.h>
#include "gfxint.h"

#define NKEPT 64
static struct kept {
    short state;                        /* 1: in use; 2: dropped, its room free */
    short x, y, w, h;                   /* its bottom-left dot (as dots of the screen, y below 0), its size */
    short top, cap, caph;               /* its room: the top line, the width (whole words) and the height */
} kept[NKEPT];
static int nkept;
static struct vidspare spare;
static int asked;
static int rowy, rowh, rowx;            /* the row being filled: its top line, its height, how far along it is */

static void order(int *a, int *b) { int t; if (*a > *b) { t = *a; *a = *b; *b = t; } }

/* the rectangle cut to the screen: 0 if nothing is left of it */
static int onscreen(int *x0, int *y0, int *x1, int *y1)
{
    order(x0, x1); order(y0, y1);
    if (*x1 < 0 || *y1 < 0 || *x0 >= gwidth || *y0 >= gheight) return 0;
    if (*x0 < 0) *x0 = 0;
    if (*y0 < 0) *y0 = 0;
    if (*x1 > gwidth - 1) *x1 = gwidth - 1;
    if (*y1 > gheight - 1) *y1 = gheight - 1;
    return 1;
}

/* w by h dots copied from sx, sy to dx, dy (bottom-left dots both), with the operation and clipping op */
static void copy(int sx, int sy, int dx, int dy, int w, int h, int op)
{
    _gend();
    if (dy < sy || (dy == sy && dx <= sx)) {            /* upwards from the bottom-left */
        _gw(VC_AMOVE); _gw(dx); _gw(dy);
        _gw(VC_GCPYUP | op); _gw(sx); _gw(sy); _gw(w - 1); _gw(h - 1);
    } else {                                            /* downwards from the top-right */
        _gw(VC_AMOVE); _gw(dx + w - 1); _gw(dy + h - 1);
        _gw(VC_GCPYDN | op); _gw(sx + w - 1); _gw(sy + h - 1); _gw(-(w - 1)); _gw(-(h - 1));
    }
}

void gcopy(int x0, int y0, int x1, int y1, int x, int y)
{
    int ax = x0 < x1 ? x0 : x1, ay = y0 < y1 ? y0 : y1;        /* (where the rectangle's bottom-left was asked for) */
    if (_gwm) { _gwn('c', 6, x0, y0, x1, y1, x, y); return; }
    if (!onscreen(&x0, &y0, &x1, &y1)) return;
    copy(x0, y0, x + (x0 - ax), y + (y0 - ay), x1 - x0 + 1, y1 - y0 + 1, VA_INSIDE | VO_REPLACE);
}

/* room for a picture of w by h dots: its number, or -1. A picture begins at a whole word of the chip's memory
   and has whole words to itself, so that it can be cleared, and drawn into, without its neighbours. */
static int room(int w, int h)
{
    int n, k, ww = (w + 15) & ~15;
    if (!asked) {
        asked = 1;
        if (ioctl(gfd(), VIOCSPARE, &spare) < 0) spare.lines = 0;
    }
    if (w < 1 || h < 1 || ww > _gvi.width) return -1;
    for (k = 0; k < nkept && kept[k].state == 2; k++)
        ;
    if (k == nkept) { nkept = 0; rowy = spare.y; rowh = rowx = 0; }         /* (none is in use: from the top again) */
    for (n = nkept, k = 0; k < nkept; k++)              /* the room of one that was dropped: the least that will do */
        if (kept[k].state == 2 && kept[k].cap >= ww && kept[k].caph >= h &&
            (n == nkept || kept[k].cap * kept[k].caph < kept[n].cap * kept[n].caph)) n = k;
    if (n == nkept) {
        if (n == NKEPT) return -1;
        if (h > rowh || rowx + ww > _gvi.width) {       /* a new row */
            rowy -= rowh; rowh = h; rowx = 0;
        }
        if (spare.y - (rowy - rowh) > spare.lines) return -1;
        kept[n].x = (short)rowx; kept[n].top = (short)rowy; kept[n].cap = (short)ww; kept[n].caph = (short)h;
        rowx += ww;
        nkept++;
    }
    kept[n].state = 1;
    kept[n].w = (short)w; kept[n].h = (short)h;         /* (what is put down again is what was kept this time) */
    kept[n].y = (short)(kept[n].top - h + 1);
    return n;
}

int gkeep(int x0, int y0, int x1, int y1)
{
    int n;
    if (_gwm) { _gwn('K', 4, x0, y0, x1, y1, 0, 0); return _gwa(&n, 1) == 1 ? n : -1; }
    if (!onscreen(&x0, &y0, &x1, &y1)) return -1;
    n = room(x1 - x0 + 1, y1 - y0 + 1);
    if (n >= 0) copy(x0, y0, kept[n].x - _gtx, kept[n].y - _gty, kept[n].w, kept[n].h, VO_REPLACE);
    return n;
}

int gmake(int w, int h)
{
    int n;
    if (_gwm) return -1;                /* (in a window there is one picture to draw into: the window's) */
    n = room(w, h);
    if (n < 0) return -1;
    _gend();
    _grwp(kept[n].x - _gtx, kept[n].top - _gty);
    _gw(VC_CLR); _gw(0); _gw((w + 15) / 16 - 1); _gw(-(h - 1));
    return n;
}

void gtarget(int n)
{
    if (_gwm) return;
    if (n < 0 || n >= nkept || kept[n].state != 1) _gaim(0, 0, _gvi.width, _gvi.height);
    else _gaim(kept[n].x, kept[n].y, kept[n].w, kept[n].h);
}

/* a kept picture put down. What of it would fall outside the clipping rectangle is not copied at all: the chip
   would leave those dots, but only after going over every one of them. */
void gstamp(int n, int x, int y, int how)
{
    int kx0, ky0, kx1, ky1, sx, sy, w, h;
    if (_gwm) { _gwn('S', 4, n, x, y, how, 0, 0); return; }
    if (n < 0 || n >= nkept || kept[n].state != 1) return;
    _gclip(&kx0, &ky0, &kx1, &ky1);
    sx = kept[n].x - _gtx; sy = kept[n].y - _gty; w = kept[n].w; h = kept[n].h;
    if (x < kx0) { sx += kx0 - x; w -= kx0 - x; x = kx0; }
    if (y < ky0) { sy += ky0 - y; h -= ky0 - y; y = ky0; }
    if (x + w - 1 > kx1) w = kx1 - x + 1;
    if (y + h - 1 > ky1) h = ky1 - y + 1;
    if (w < 1 || h < 1) return;
    copy(sx, sy, x, y, w, h, VA_INSIDE | (how & 3));
}

void gdrop(int n)
{
    if (_gwm) { _gwn('D', 1, n, 0, 0, 0, 0, 0); return; }
    if (n >= 0 && n < nkept && kept[n].state == 1) kept[n].state = 2;
}

#define MW 64                           /* words a line of the screen has at most */

/* The result's word j of a line is the 16 dots from a0 + 16 j: in the chip's words (16 dots from a multiple of
   16, bit 0 the left-hand one) they begin b = a0 & 15 dots into one word and end in the next. */
int gread(int x0, int y0, int x1, int y1, unsigned short *bits)
{
    unsigned short line[MW + 2];        /* the chip's words of a line, from the one a0 is in; 0 where nothing is read */
    int w, y, k, nw, w0, wb, b, a0 = x0 < x1 ? x0 : x1, a1 = x0 < x1 ? x1 : x0, b0 = y0 < y1 ? y0 : y1, b1 = y0 < y1 ? y1 : y0;
    w = (a1 - a0 + 16) >> 4;            /* words a line of the result */
    for (k = 0; k < w * (b1 - b0 + 1); k++) bits[k] = 0;
    if (_gwm) return -1;                /* (not in a window: the dots would come back through the terminal) */
    x0 = a0; y0 = b0; x1 = a1; y1 = b1;
    if (gwidth > 16 * MW || w > MW || !onscreen(&x0, &y0, &x1, &y1)) return 0;
    w0 = x0 >> 4; nw = (x1 >> 4) - w0 + 1;
    wb = a0 >= 0 ? a0 >> 4 : -((-a0 + 15) >> 4);        /* (a0 / 16 rounded down) */
    b = a0 - 16 * wb;
    for (k = 0; k < w + 1; k++) line[k] = 0;
    for (y = y1; y >= y0; y--) {
        unsigned short *out = bits + (b1 - y) * w, *in = line + (w0 - wb);
        if (_gread(y, w0, nw, in) < 0) return -1;
        in[0] &= 0xFFFF << (x0 & 15);                   /* (only the dots asked for) */
        in[nw - 1] &= 0xFFFF >> (15 - (x1 & 15));
        if (!b) for (k = 0; k < w; k++) out[k] = (unsigned short)_grev(line[k]);
        else for (k = 0; k < w; k++) out[k] = (unsigned short)_grev((line[k] >> b | line[k + 1] << (16 - b)) & 0xFFFF);
    }
    return 0;
}

int gwrite(int x0, int y0, int x1, int y1, const unsigned short *bits, int how)
{
    unsigned short line[MW + 2];        /* a line of the picture as the chip has words: bit 0 the left-hand dot */
    int kx0, ky0, kx1, ky1, w, y, k, x, j, b, a0 = x0 < x1 ? x0 : x1, a1 = x0 < x1 ? x1 : x0, b1 = y0 < y1 ? y1 : y0;
    unsigned mask = 0xFFFF;
    w = (a1 - a0 + 16) >> 4;
    if (_gwm) return -1;
    if (w > MW || !onscreen(&x0, &y0, &x1, &y1)) return 0;
    _gend();
    _gclip(&kx0, &ky0, &kx1, &ky1);
    if (x0 < kx0) x0 = kx0;
    if (y0 < ky0) y0 = ky0;
    if (x1 > kx1) x1 = kx1;
    if (y1 > ky1) y1 = ky1;
    if (x0 > x1 || y0 > y1) return 0;
    for (y = y1; y >= y0; y--) {
        const unsigned short *in = bits + (b1 - y) * w;
        _grwp(x0, y);
        line[0] = 0;                                    /* (the picture's word j is line[j + 1]) */
        for (k = 0; k < w; k++) line[k + 1] = (unsigned short)_grev(in[k]);
        line[w + 1] = 0;
        for (k = x0 >> 4; k <= x1 >> 4; k++) {          /* the screen's word k: the dots 16 k to 16 k + 15 */
            unsigned v, m = 0xFFFF;
            int at = 16 * k - a0;                       /* ... which are the picture's dots at to at + 15 */
            j = at >= 0 ? at >> 4 : -1;                 /* (at is -15 or more: x0 is not left of a0) */
            b = at - 16 * j;
            v = b ? line[j + 1] >> b | line[j + 2] << (16 - b) : line[j + 1];
            x = 16 * k;
            if (x < x0) m &= 0xFFFF << (x0 - x);
            if (x + 15 > x1) m &= 0xFFFF >> (x + 15 - x1);
            if (m != mask) { _gw(VC_WPR | 0x04); _gw(m); mask = m; }
            _gw(VC_MOD | (how & 3)); _gw(v & 0xFFFF);
        }
    }
    if (mask != 0xFFFF) { _gw(VC_WPR | 0x04); _gw(0xFFFF); }
    return gflush();
}
