/* bgfx.c -- P32W BASIC: the graphics statements, on the video card's screen through the graphics library
 * (gfx(3), /dev/video). The fourth file of BASIC (basic.h).
 *
 *   SCREEN 1                          the picture: 640 by 480 dots, black;  SCREEN 0: the console's text again
 *   PSET (x, y) [, c]                 a dot;  PRESET: a dot put out
 *   LINE [(x1, y1)]-(x2, y2) [, [c] [, B | BF]]      a line; B: the box with those corners; BF: filled
 *   CIRCLE (x, y), r [, [c] [, [start] [, [end] [, aspect]]]]
 *   COLOR c                           what a statement without a colour draws in
 *   LOCATE row, column                the next line PRINT writes goes into the picture, from there
 *   CLS                               (in SCREEN 1) the picture black
 *   POINT(x, y)                       1 if the dot is lit, 0 if not, -1 if it is off the screen
 *
 * Microsoft's conventions: x 0, y 0 is the top-left dot and y runs down (the library's runs up: turned over
 * here); a point may be STEP (dx, dy), that far from the last one named; angles are radians, anticlockwise
 * from the right, and a negative one also draws the radius to that end of the arc. A colour is 0 (black) or 1
 * (white); here 2 turns dots over, so that a thing drawn twice in it is gone.
 *
 * PRINT and INPUT stay on the terminal BASIC was started from. Started from the video card's own console,
 * its text cannot be seen while a picture is: so there, when a line has been typed at the prompt, the screen
 * goes back to text (as SCREEN 0), and until then the picture stays.
 */
#include "basic.h"
#include <sys/stat.h>
#include <gfx.h>

#define Y(y) (gheight - 1 - (y))

int gr_on;
int gr_text;
static int fg = 1;                      /* COLOR's */
static int lastx, lasty;                /* the last point named */
static int trow = 1, tcol = 1;          /* where the picture's next character goes (LOCATE's, from 1) */
static int tsave;                       /* the terminal's column, kept while PRINT goes into the picture */

static void need_screen(void) { if (!gr_on) basic_error(E_ILLEGAL); }

static void ink(int c)
{
    if (c < 0 || c > 2) basic_error(E_ILLEGAL);
    gink(c == 0 ? G_BLACK : c == 1 ? G_WHITE : G_XOR);
}

static int coord(void)                  /* an expression that is a coordinate */
{
    int v = int_arg();
    if (v < -32768 || v > 32767) basic_error(E_OVERFLOW);
    return v;
}

/* [STEP] (x, y) */
static void point(int *x, int *y)
{
    int rel = match(T_STEP), px, py;
    expect('(');
    px = coord();
    expect(',');
    py = coord();
    expect(')');
    if (rel) { px += lastx; py += lasty; }
    *x = px; *y = py;
}

/* is there an expression here (and not a comma or the statement's end: an argument left out)? */
static int given(void) { return !at_end() && *pc != ','; }

static void do_line(void)
{
    int x0 = lastx, y0 = lasty, x1, y1, c = fg, box = 0;
    sp();
    if (*pc != '-') point(&x0, &y0);
    expect('-');
    lastx = x0; lasty = y0;             /* (a STEP in the second point is from the first) */
    point(&x1, &y1);
    if (match(',')) {
        if (given()) c = int_arg();
        if (match(',')) {
            sp();
            if (upper(*pc) != 'B') basic_error(E_SYNTAX);
            pc++; box = 1;
            if (upper(*pc) == 'F') { pc++; box = 2; }
        }
    }
    ink(c);
    if (box == 2) gfbox(x0, Y(y0), x1, Y(y1));
    else if (box) gbox(x0, Y(y0), x1, Y(y1));
    else gline(x0, Y(y0), x1, Y(y1));
    lastx = x1; lasty = y1;
}

/* an angle of CIRCLE: radians, 0 to 2 pi, in whole degrees; *spoke: it was negative (its radius is drawn too) */
static int angle(int *spoke, fp64 *rad)
{
    struct val v;
    fp64 k, d;
    int deg;
    eval(&v);
    to_fp(&v, rad);
    *spoke = fp_sign(rad) < 0;
    fp_abs(rad, rad);
    fp_parse(&k, "57.29577951308232");
    fp_mul(&d, rad, &k);
    fp_round(&d, &d);
    deg = fp_toint(&d, 0);
    if (deg > 360) basic_error(E_ILLEGAL);
    return deg;
}

/* the radius of the ellipse rx, ry about x, y that points the way of the angle rad */
static void spoke(int x, int y, int rx, int ry, const fp64 *rad)
{
    fp64 c, s, a, b, t;
    int px, py;
    if (!rx || !ry) return;
    fp_cos(&c, rad); fp_sin(&s, rad);
    fp_fromint(&t, rx); fp_div(&a, &c, &t); fp_mul(&a, &a, &a);
    fp_fromint(&t, ry); fp_div(&b, &s, &t); fp_mul(&b, &b, &b);
    fp_add(&a, &a, &b); fp_sqrt(&a, &a);                /* the point is (c, s) / sqrt((c / rx)^2 + (s / ry)^2) */
    fp_div(&c, &c, &a); fp_round(&c, &c); px = fp_toint(&c, 0);
    fp_div(&s, &s, &a); fp_round(&s, &s); py = fp_toint(&s, 0);
    gline(x, Y(y), x + px, Y(y) + py);
}

static void do_circle(void)
{
    int x, y, r, rx, ry, c = fg, a0 = 0, a1 = 360, arc = 0, s0 = 0, s1 = 0;
    fp64 r0, r1;
    point(&x, &y);
    expect(',');
    r = int_arg();
    if (r < 0 || r > 16383) basic_error(E_ILLEGAL);
    rx = ry = r;
    if (match(',')) {
        if (given()) c = int_arg();
        if (match(',')) {
            if (given()) { a0 = angle(&s0, &r0); arc = 1; }
            if (match(',')) {
                if (given()) { a1 = angle(&s1, &r1); arc = 1; }
                if (match(',')) {           /* the aspect: height over width; the radius is the larger of the two */
                    struct val v;
                    fp64 q, t;
                    eval(&v);
                    to_fp(&v, &q);
                    if (fp_sign(&q) <= 0) basic_error(E_ILLEGAL);
                    fp_fromint(&t, r);
                    if (fp_cmp(&q, &F_ONE) < 0) { fp_mul(&t, &t, &q); fp_round(&t, &t); ry = fp_toint(&t, 0); }
                    else { fp_div(&t, &t, &q); fp_round(&t, &t); rx = fp_toint(&t, 0); }
                }
            }
        }
    }
    ink(c);
    if (!arc) gellipse(x, Y(y), rx, ry);
    else {
        garc(x, Y(y), rx, ry, a0, a1);
        if (s0) spoke(x, y, rx, ry, &r0);
        if (s1) spoke(x, y, rx, ry, &r1);
    }
    lastx = x; lasty = y;
}

void gr_statement(int t)
{
    int x, y, c;
    if (t == T_SCREEN) {
        c = int_arg();
        if (c == 0) {
            if (gr_on) gclose();
            gr_on = gr_text = 0;
        } else if (c == 1 || c == 2) {
            if (gr_on) return;
            fflush(stdout);
            if (gopen() < 0) basic_error(E_DEVICE);
            gr_on = 1;
            fg = 1;
            lastx = gwidth / 2; lasty = gheight / 2;
            trow = tcol = 1;
        } else basic_error(E_ILLEGAL);
        return;
    }
    if (t == T_COLOR) {                 /* (a background and a border may follow, as in programs for other screens) */
        if (given()) { c = int_arg(); if (c < 0 || c > 2) basic_error(E_ILLEGAL); fg = c; }
        while (match(',')) if (given()) int_arg();
        return;
    }
    if (t == T_LOCATE) {
        y = given() ? int_arg() : trow;
        x = match(',') ? int_arg() : 1;
        if (y < 1 || x < 1 || y > 255 || x > 255) basic_error(E_ILLEGAL);
        if (!gr_on) { printf("\033[%d;%dH", y, x); col = x - 1; return; }      /* (the terminal's cursor) */
        if (!gr_text) tsave = col;
        trow = y; tcol = x; col = x - 1;
        gr_text = 1;
        return;
    }
    need_screen();
    switch (t) {
    case T_CLS: gclear(); trow = tcol = 1; return;
    case T_PSET: case T_PRESET:
        c = t == T_PSET ? fg : 0;
        point(&x, &y);
        if (match(',')) c = int_arg();
        ink(c);
        gpoint(x, Y(y));
        lastx = x; lasty = y;
        return;
    case T_LINE: do_line(); return;
    case T_CIRCLE: do_circle(); return;
    }
}

void gr_point(struct val *v)
{
    int x, y;
    expect('(');
    x = coord();
    expect(',');
    y = coord();
    expect(')');
    need_screen();
    set_int(v, x < 0 || y < 0 || x >= gwidth || y >= gheight ? -1 : gget(x, Y(y)));
}

/* a character of PRINT after LOCATE: into the picture, its whole cell (so text can be written over text). The
   end of the line ends it: what PRINT writes next goes to the terminal again. */
void gr_char(int c)
{
    char s[2];
    if (!gr_on) { gr_text = 0; col = tsave; out_char(0, c); return; }  /* (SCREEN 0 since the LOCATE) */
    if (c == '\n') { gr_text = 0; col = tsave; trow++; tcol = 1; return; }
    if (tcol > gwidth / gcellw) { tcol = 1; trow++; }
    if (c >= ' ' && trow <= gheight / gcellh) {
        s[0] = (char)c; s[1] = 0;
        ink(fg);
        glook(G_OPAQUE);
        gtext((tcol - 1) * gcellw, gheight - trow * gcellh, s);
    }
    tcol++; col++;
}

void gr_show(void) { if (gr_on) gflush(); }

/* is BASIC's terminal the video card's own console? Then its text and a picture cannot both be seen */
static int on_console(void)
{
    static int known = -1;
    struct stat a, b;
    if (known < 0)
        known = fstat(1, &a) == 0 && stat("/dev/tty4", &b) == 0 && (a.st_mode & S_IFMT) == S_IFCHR &&
                (b.st_mode & S_IFMT) == S_IFCHR && a.st_rdev == b.st_rdev;
    return known;
}

void gr_typed(void)
{
    gr_text = 0;
    if (gr_on && on_console()) { gclose(); gr_on = 0; }
}
