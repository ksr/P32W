/* gell.c -- libgfx: circles, ellipses and their arcs, outlined or filled.
 *
 * The library works the dots out itself and has the chip draw them as level and upright runs (blocks one
 * dot thick, the chip's AFRCT; a lone dot is a DOT). The chip has commands for circles and arcs of its own;
 * they are not used, so that the dots are the same on any chip and in the emulators, each is drawn once,
 * and nothing here rests on a command that has not been run on the real chip.
 *
 * One quarter is walked, from the top (0, b) to the right-hand end (a, 0), by the midpoint method: where
 * the curve is flatter than 45 degrees each step goes a dot right and perhaps one down, whichever of the
 * two dots the curve passes nearer; where it is steeper, a dot down and perhaps one right. The numbers are
 * the ellipse's own, A x^2 + B y^2 = B b^2 with A = b^2 and B = a^2 (1 and 1 for a circle), taken four
 * times so that a half dot is a whole number. The other three quarters are its mirror images; a run that
 * touches an axis is joined to its image, so the dots on the axes are drawn once too.
 *
 * An arc is the same walk with each dot asked whether it lies between the arc's two directions.
 */
#include <gfx.h>
#include "gfxint.h"

#define RMAX 16383                      /* the largest radius */
#define FITS 268435456                  /* A and B are kept so small that 8 A (a + 2) and 8 B (b + 2) are below 2^31 */

static int ox, oy;                      /* the centre */
static int arc, wide;                   /* an arc: only the dots between two directions; wide: more than half a turn */
static int ax0, ay0, ax1, ay1;          /* ... the directions (gcos and gsin of the two angles) */
static int filling;

/* is the dot x, y (from the centre) in the arc? c0 >= 0: anticlockwise of the first direction by up to half
   a turn; c1 >= 0: as far clockwise of the second. Up to half a turn the arc is where both hold (and the dot
   is on the arc's side of the centre: with the two directions the same, c0 and c1 are 0 on the far side
   too); a wider arc is everything but the open wedge between its ends. */
static int inarc(int x, int y)
{
    int c0 = ax0 * y - ay0 * x, c1 = x * ay1 - y * ax1;
    if (wide) return !(c0 < 0 && c1 < 0);
    return c0 >= 0 && c1 >= 0 && x * (ax0 + ax1) + y * (ay0 + ay1) >= 0;
}

/* a level or upright run of dots, from the centre: x0 <= x1, y0 <= y1 */
static void dots(int x0, int y0, int x1, int y1)
{
    int n, k, dx, dy, x, y, sx = 0, sy = 0, on = 0;
    if (!arc) { _grun(ox + x0, oy + y0, ox + x1, oy + y1); return; }
    dx = x1 > x0; dy = y1 > y0;
    n = (x1 - x0) + (y1 - y0);
    for (k = 0; k <= n; k++) {
        x = x0 + k * dx; y = y0 + k * dy;
        if (inarc(x, y)) {
            if (!on) { sx = x; sy = y; on = 1; }
        } else if (on) {
            _grun(ox + sx, oy + sy, ox + x - dx, oy + y - dy);
            on = 0;
        }
    }
    if (on) _grun(ox + sx, oy + sy, ox + x1, oy + y1);
}

/* a level run of the quarter, xs to xe on the line y, and its images */
static void level(int xs, int xe, int y)
{
    for (;; y = -y) {
        if (xs == 0) dots(-xe, y, xe, y);
        else { dots(xs, y, xe, y); dots(-xe, y, -xs, y); }
        if (y <= 0) break;
    }
}

/* an upright run of the quarter, ys up to ye in the column x, and its images */
static void upright(int x, int ys, int ye)
{
    for (;; x = -x) {
        if (ys == 0) dots(x, -ye, x, ye);
        else { dots(x, ys, x, ye); dots(x, -ye, x, -ys); }
        if (x <= 0) break;
    }
}

/* the outline: the walk's dots gathered into runs, rightwards or downwards */
static int rn, rx0, ry0, rx1, ry1;

static void rend(void)
{
    if (!rn) return;
    if (ry0 == ry1) level(rx0, rx1, ry0);
    else upright(rx0, ry1, ry0);
    rn = 0;
}

static void odot(int x, int y)
{
    if (rn && ((y == ry1 && x == rx1 + 1 && ry0 == ry1) || (x == rx1 && y == ry1 - 1 && rx0 == rx1))) {
        rx1 = x; ry1 = y; rn++;
        return;
    }
    rend();
    rx0 = rx1 = x; ry0 = ry1 = y; rn = 1;
}

/* filled: a line reaches as far as the walk's last dot on it; lines that reach equally far make one block */
static int fn, fx, fyt, fyb;            /* the block being gathered: the lines fyb to fyt, each from -fx to fx */
static int ln, lx, ly;                  /* the walk's dot before this one */

static void fend(void)
{
    if (!fn) return;
    if (fyb == 0) _gfrun(ox - fx, oy - fyt, ox + fx, oy + fyt);
    else {
        _gfrun(ox - fx, oy + fyb, ox + fx, oy + fyt);
        _gfrun(ox - fx, oy - fyt, ox + fx, oy - fyb);
    }
    fn = 0;
}

static void frow(int x, int y)
{
    if (fn && x == fx) { fyb = y; return; }
    fend();
    fx = x; fyt = fyb = y; fn = 1;
}

static void dot(int x, int y)
{
    if (!filling) { odot(x, y); return; }
    if (ln && y != ly) frow(lx, ly);
    lx = x; ly = y; ln = 1;
}

static void ell(int cx, int cy, int a, int b, int fill)
{
    int A, B, x, y, d;
    unsigned u, v;
    _gend();
    ox = cx; oy = cy;
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    if (a > RMAX) a = RMAX;
    if (b > RMAX) b = RMAX;
    if (!a || !b) {                     /* no width or no height: a line, or a dot */
        if (fill) _gfrun(ox - a, oy - b, ox + a, oy + b);
        else dots(-a, -b, a, b);
        return;
    }
    if (a == b) A = B = 1;
    else {
        A = b * b; B = a * a;
        while (A > FITS / (a + 2) || B > FITS / (b + 2)) {     /* (an ellipse far larger than the screen: its */
            A >>= 1; B >>= 1;                                   /* shape to fewer places, a dot out here and there) */
        }
        if (!A) A = 1;
        if (!B) B = 1;
    }
    filling = fill;
    rn = fn = ln = 0;
    x = 0; y = b;
    dot(x, y);
    d = 4 * A - 4 * B * b + B;          /* at the midpoint (x + 1, y - 1/2): below 0, the curve passes above it */
    while (B * (2 * y - 1) > 2 * A * (x + 1)) {
        if (d < 0) d += 4 * A * (2 * x + 3);
        else { d += 4 * A * (2 * x + 3) - 8 * B * (y - 1); y--; }
        x++;
        dot(x, y);
    }
    u = 2 * x + 1; v = y - 1;           /* at the midpoint (x + 1/2, y - 1): the terms are large and the sum is */
    d = (int)((unsigned)A * u * u + 4 * (unsigned)B * v * v - 4 * (unsigned)B * b * b);    /* not: it is right */
    while (y > 0) {
        if (d < 0) { d += 8 * A * (x + 1) + 4 * B * (3 - 2 * y); x++; }
        else d += 4 * B * (3 - 2 * y);
        y--;
        dot(x, y);
    }
    while (x < a) dot(++x, 0);          /* (a very flat one reaches the axis early: the rest of its tip) */
    if (fill) { frow(lx, ly); fend(); }
    else rend();
}

void gellipse(int x, int y, int rx, int ry)
{
    if (_gwm) { _gwn('e', 4, x, y, rx, ry, 0, 0); return; }
    arc = 0; ell(x, y, rx, ry, 0);
}
void gfellipse(int x, int y, int rx, int ry)
{
    if (_gwm) { _gwn('E', 4, x, y, rx, ry, 0, 0); return; }
    arc = 0; ell(x, y, rx, ry, 1);
}
void gcircle(int x, int y, int r) { gellipse(x, y, r, r); }
void gfcircle(int x, int y, int r) { gfellipse(x, y, r, r); }

void garc(int x, int y, int rx, int ry, int a0, int a1)
{
    int sweep = a1 - a0;
    if (_gwm) { _gwn('a', 6, x, y, rx, ry, a0, a1); return; }
    if (sweep >= 360) { gellipse(x, y, rx, ry); return; }      /* (a whole turn or more: all of it) */
    sweep %= 360;
    if (sweep < 0) sweep += 360;
    ax0 = gcos(a0); ay0 = gsin(a0);
    ax1 = gcos(a0 + sweep); ay1 = gsin(a0 + sweep);
    wide = sweep > 180;
    arc = 1;
    ell(x, y, rx, ry, 0);
    arc = 0;
}
