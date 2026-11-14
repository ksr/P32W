/* gfill.c -- libgfx: filled outlines. gfpoly fills the outline through given points; gfill fills whatever
 * area of the screen a dot lies in, up to its border.
 *
 * gfpoly draws every dot that is inside the outline or on it: the dots with whole coordinates of the closed
 * figure, so that a filled outline and a filled box of the same corners are the same dots. Where the outline
 * crosses itself, a place is inside if a line from it to far away crosses the outline an odd number of
 * times. It is done a line of the screen at a time: where that line crosses the sides, in pairs; the sides
 * lying along it; the corners that only touch it. These are put in order and joined where they meet, so that
 * each dot is drawn once, and lines that come out the same are drawn as one block.
 *
 * gfill reads the screen (the chip cannot be asked what an area's shape is; its own PAINT command is not
 * used, as nothing here is that has not been run on the real chip). It takes what it needs of each line
 * from the chip as it comes to it, 128 dots at a time, and goes from the dot outwards a run at a time, a
 * word of 16 like dots at a step, keeping a list of the runs whose neighbours above or below are still to
 * be looked at (Heckbert's seed fill).
 */
#include <gfx.h>
#include "gfxint.h"

#define NPTS 256                        /* the points of an outline */
#define CMAX 16383                      /* ... and how far from 0 each may be: products of two differences fit 32 bits */

static int sl[2 * NPTS], sr[2 * NPTS], ns;      /* a line's pieces: from sl to sr */

static void piece(int l, int r)
{
    if (l <= r && ns < 2 * NPTS) { sl[ns] = l; sr[ns] = r; ns++; }
}

static int fdiv(int a, int b)           /* a / b rounded down (b above 0) */
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

void gfpoly(const int *xy, int n)
{
    static int cf[NPTS], cx[NPTS];      /* a line's crossings: the x rounded down, and whether it was exact */
    int kx0, ky0, kx1, ky1, y, lo, hi, k, j, nc, t;
    int bl = 0, br = 0, bt = 0, bb = 0, have = 0;       /* the block being gathered: lines bb to bt, each bl to br */
    if (n < 1 || n > NPTS) return;
    if (_gwm) { if (n <= 60) _gwv('Y', 2 * n, xy, 0); return; }
    for (k = 0; k < 2 * n; k++) if (xy[k] < -CMAX || xy[k] > CMAX) return;
    _gend();
    _gclip(&kx0, &ky0, &kx1, &ky1);
    lo = hi = xy[1];
    for (k = 1; k < n; k++) { if (xy[2 * k + 1] < lo) lo = xy[2 * k + 1]; if (xy[2 * k + 1] > hi) hi = xy[2 * k + 1]; }
    if (lo < ky0) lo = ky0;
    if (hi > ky1) hi = ky1;
    for (y = hi; y >= lo; y--) {
        nc = ns = 0;
        for (k = 0; k < n; k++) {
            int px = xy[2 * k], py = xy[2 * k + 1], qx = xy[2 * ((k + 1) % n)], qy = xy[2 * ((k + 1) % n) + 1];
            if (py == qy) {                             /* a side along this line (or a single point) */
                if (py == y) piece(px < qx ? px : qx, px < qx ? qx : px);
                continue;
            }
            if (py > qy) { t = px; px = qx; qx = t; t = py; py = qy; qy = t; }
            if (y == qy) piece(qx, qx);                 /* its upper end: not a crossing, but a dot of the figure */
            if (y < py || y >= qy) continue;
            {                                           /* a crossing: x = px + (y - py) (qx - px) / (qy - py) */
                int num = (y - py) * (qx - px), den = qy - py, f = fdiv(num, den);
                cf[nc] = px + f; cx[nc] = f * den == num; nc++;
            }
        }
        for (k = 1; k < nc; k++)                        /* the crossings in order, an exact one before others in its dot */
            for (j = k; j > 0 && (cf[j] < cf[j - 1] || (cf[j] == cf[j - 1] && cx[j] > cx[j - 1])); j--) {
                t = cf[j]; cf[j] = cf[j - 1]; cf[j - 1] = t;
                t = cx[j]; cx[j] = cx[j - 1]; cx[j - 1] = t;
            }
        for (k = 0; k + 1 < nc; k += 2) piece(cf[k] + !cx[k], cf[k + 1]);  /* inside: between each pair */
        for (k = 1; k < ns; k++)                        /* the pieces in order */
            for (j = k; j > 0 && sl[j] < sl[j - 1]; j--) {
                t = sl[j]; sl[j] = sl[j - 1]; sl[j - 1] = t;
                t = sr[j]; sr[j] = sr[j - 1]; sr[j - 1] = t;
            }
        for (k = j = 0; k < ns; k++)                    /* ... and joined where they meet or overlap */
            if (j && sl[k] <= sr[j - 1] + 1) { if (sr[k] > sr[j - 1]) sr[j - 1] = sr[k]; }
            else { sl[j] = sl[k]; sr[j] = sr[k]; j++; }
        ns = j;
        if (have && ns == 1 && sl[0] == bl && sr[0] == br) { bb = y; continue; }       /* the same as the line above */
        if (have) { _gfrun(bl, bb, br, bt); have = 0; }
        if (ns == 1) { bl = sl[0]; br = sr[0]; bt = bb = y; have = 1; }
        else for (k = 0; k < ns; k++) _gfrun(sl[k], y, sr[k], y);
    }
    if (have) _gfrun(bl, bb, br, bt);
}

/* ---------------------------------------------------------------- gfill */
#define MW 64                           /* words a line of the screen has at most */
#define MH 512                          /* ... and lines */
#define NSTK 1024

static unsigned short scr[MH][MW];      /* the screen as it was read: 8 words (128 dots) of a line at a time, as the */
static unsigned char got[MH];           /* area comes to them (got: which eighths of a line are here). A dot that */
static struct seg { short y, xl, xr, dy; } stk[NSTK];   /* has been filled is turned over here: not come to again */
static int nstk, over, seed, failed;
static unsigned full;                   /* a word of 16 dots all still to fill */
static int fx0, fy0, fx1, fy1;          /* the clipping rectangle */

static int is(int x, int y)             /* is the dot x, y still to fill? (its part of the line read first) */
{
    int c = x >> 7, n;
    if (!(got[y] >> c & 1)) {
        n = (gwidth + 15) / 16 - 8 * c;
        if (_gread(y, 8 * c, n < 8 ? n : 8, scr[y] + 8 * c) < 0) { failed = 1; return 0; }
        got[y] |= (unsigned char)(1 << c);
    }
    return (int)(scr[y][x >> 4] >> (x & 15) & 1) == seed;
}

#define DONE(x, y) (scr[y][(x) >> 4] ^= 1 << ((x) & 15))

static void push(int y, int xl, int xr, int dy)
{
    if (y + dy < fy0 || y + dy > fy1) return;
    if (nstk == NSTK) { over = 1; return; }
    stk[nstk].y = (short)y; stk[nstk].xl = (short)xl; stk[nstk].xr = (short)xr; stk[nstk].dy = (short)dy;
    nstk++;
}

int gfill(int x, int y)
{
    int x1, x2, dy, l, k;
    if (_gwm) { _gwn('f', 2, x, y, 0, 0, 0, 0); return _gwa(&k, 1) == 1 ? k : -1; }
    if (gwidth > 16 * MW || gheight > MH) return -1;
    _gend();
    _gclip(&fx0, &fy0, &fx1, &fy1);
    if (x < fx0 || x > fx1 || y < fy0 || y > fy1) return 0;
    for (k = 0; k < MH; k++) got[k] = 0;
    nstk = over = failed = seed = 0;
    seed = !is(x, y);                   /* (the dot as it is, lit or not: is() says whether it is like seed) */
    if (failed) return -1;
    full = seed ? 0xFFFF : 0;
    push(y, x, x, 1);                   /* (the line itself is looked at as the one below the line above) */
    push(y + 1, x, x, -1);
    while (nstk) {
        nstk--;
        dy = stk[nstk].dy; y = stk[nstk].y + dy; x1 = stk[nstk].xl; x2 = stk[nstk].xr;
        for (x = x1; x >= fx0 && is(x, y); )                   /* leftwards from the run's left end */
            if ((x & 15) == 15 && x - 15 >= fx0 && scr[y][x >> 4] == full) { scr[y][x >> 4] ^= 0xFFFF; x -= 16; }
            else { DONE(x, y); x--; }
        if (failed) return -1;
        if (x >= x1) goto skip;
        l = x + 1;
        if (l < x1) push(y, l, x1 - 1, -dy);                   /* (further left than the run it came from: back) */
        x = x1 + 1;
        do {
            while (x <= fx1 && is(x, y))                       /* rightwards */
                if (!(x & 15) && x + 15 <= fx1 && scr[y][x >> 4] == full) { scr[y][x >> 4] ^= 0xFFFF; x += 16; }
                else { DONE(x, y); x++; }
            _gfrun(l, y, x - 1, y);
            push(y, l, x - 1, dy);
            if (x > x2 + 1) push(y, x2 + 1, x - 1, -dy);       /* (further right: back) */
skip:       for (x++; x <= x2 && !is(x, y); )
                if (!(x & 15) && x + 15 <= x2 && scr[y][x >> 4] == (full ^ 0xFFFF)) x += 16; else x++;
            l = x;
            if (failed) return -1;
        } while (x <= x2);
    }
    return over ? -1 : 0;
}
