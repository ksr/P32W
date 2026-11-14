/* gfx_test.c -- the graphics library (software/libgfx, gfx(3), D111) against the emulators' model of the
   video card's chip, on the machine the system is built on. The library's own sources are compiled here
   (gfxhost/host.h) with /dev/video replaced by the model (gfxhost/dev.c), and the picture is looked at in the
   model's memory, dot by dot:
     - every circle of radius 0 to 239 and some thousands of ellipses against the same midpoint walk done here
       in 64 bits (the library's is in 32: nothing overflows), the eight-way and four-way symmetry, every
       outline joined up, how near the true curve the dots are;
     - each figure drawn once in G_XOR lights exactly the dots it lights in white (so no dot is drawn twice);
     - filled figures against their outlines; arcs against the angle of each dot; an arc and the rest of the
       turn against the whole;
     - lines, the pen's chains, boxes, clipping (nothing outside the rectangle, nothing under the screen),
       text against the font's pictures in each ink and look, gget, the sines.
   It prints "gfx_test: N checks", or what failed;  ./gfx_test -v  names each check as it passes. */
#include <stdint.h>
#include <math.h>
#include <gfx.h>
#include <sys/video.h>

#define MW 40
static const uint16_t *mem;
static int lit(int x, int y) { return mem[(479 - y) * MW + (x >> 4)] >> (x & 15) & 1; }
static int count(void) { int n = 0, x, y; for (y = 0; y < 480; y++) for (x = 0; x < 640; x++) n += lit(x, y); return n; }
static uint16_t snap[480 * MW], under[8 * 48 * MW];
static void keep(void) { memcpy(snap, mem, sizeof snap); }
static int same(void) { return !memcmp(snap, mem, sizeof snap); }
static int was(int x, int y) { return snap[(479 - y) * MW + (x >> 4)] >> (x & 15) & 1; }
static int checks, fails;
static int verbose;
static void ok(int c, const char *what) { checks++; if (!c) { fails++; printf("FAIL: %s\n", what); } else if (verbose) printf("ok: %s\n", what); }
#define OK(c, ...) do { char _b[200]; snprintf(_b, sizeof _b, __VA_ARGS__); ok(c, _b); } while (0)

/* the reference: the same midpoint walk in 64 bits, exact weights; marks ref[][] for the quarter x >= 0, y >= 0 */
static unsigned char ref[481][641];
static void refwalk(int a, int b)
{
    long long A = (long long)b * b, B = (long long)a * a, d; int x = 0, y = b;
    memset(ref, 0, sizeof ref);
#define M(x, y) do { if ((x) <= 640 && (y) <= 480) ref[y][x] = 1; } while (0)
    if (!a || !b) { for (x = 0; x <= a; x++) for (y = 0; y <= b; y++) M(x, y); return; }
    M(x, y);
    d = 4 * A - 4 * B * b + B;
    while (B * (2 * y - 1) > 2 * A * (x + 1)) {
        if (d < 0) d += 4 * A * (2 * x + 3); else { d += 4 * A * (2 * x + 3) - 8 * B * (y - 1); y--; }
        x++; M(x, y);
    }
    d = A * (2 * x + 1) * (2 * x + 1) + 4 * B * (y - 1) * (y - 1) - 4 * B * b * b;
    while (y > 0) {
        if (d < 0) { d += 8 * A * (x + 1) + 4 * B * (3 - 2 * y); x++; } else d += 4 * B * (3 - 2 * y);
        y--; M(x, y);
    }
    while (x < a) { x++; M(x, 0); }
}

/* a pattern (a cross in a square, with a line under it), all 1s, a triangle; a random number's seed */
static const unsigned short P[16] = { 0x8001, 0x4002, 0x2004, 0x1008, 0x0810, 0x0420, 0x0240, 0x0180,
                                      0x0180, 0x0240, 0x0420, 0x0810, 0x1008, 0x2004, 0x4002, 0xFFFF };
static const unsigned short ones[16] = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
                                         0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF };
static const int tri[] = { 50, 50, 250, 90, 120, 300 };
static unsigned seedv = 12345;

int main(int argc, char **argv)
{
    int x, y, r, a, b, k, n, bad;
    (void)argv;
    verbose = argc > 1;                 /* (any argument: every check is named as it passes) */
    mem = dev_mem();
    memcpy(under, mem + 480 * MW, sizeof under);
    OK(gopen() == 0, "gopen");
    OK(gwidth == 640 && gheight == 480 && gcellw == 8 && gcellh == 16, "the screen's size");
    gflush();
    OK(count() == 0, "gopen clears");

    /* dots, gget */
    gpoint(0, 0); gpoint(639, 479); gpoint(17, 3); gflush();
    OK(count() == 3 && lit(0, 0) && lit(639, 479) && lit(17, 3), "three dots");
    OK(gget(0, 0) == 1 && gget(639, 479) == 1 && gget(17, 3) == 1 && gget(16, 3) == 0 && gget(18, 3) == 0 && gget(640, 0) == 0 && gget(0, -1) == 0, "gget");
    gink(G_BLACK); gpoint(17, 3); gflush(); OK(count() == 2, "a dot put out");
    gink(G_XOR); gpoint(17, 3); gpoint(0, 0); gflush(); OK(count() == 2 && lit(17, 3) && !lit(0, 0), "dots turned over");
    gink(G_WHITE);

    /* lines */
    gclear(); gline(10, 10, 50, 30); gflush();
    OK(count() == 41 && lit(10, 10) && lit(50, 30), "a line has both its ends: %d dots", count());
    gclear(); gline(50, 30, 10, 10); gline(5, 5, 5, 5); gflush();
    OK(count() == 42 && lit(5, 5), "a line the other way, a line of one dot");
    gclear(); gmove(100, 100); gdraw(200, 100); gdraw(200, 200); gdraw(100, 200); gdraw(100, 100); gflush();
    OK(count() == 400, "a square drawn with the pen: %d dots", count());
    keep();
    gclear(); gink(G_XOR); gmove(100, 100); gdraw(200, 100); gdraw(200, 200); gdraw(100, 200); gdraw(100, 100); gflush();
    OK(same(), "... the same dots in G_XOR (none drawn twice)");
    gbox(100, 100, 200, 200); gflush(); OK(count() == 0, "... and gbox is that square");
    { static const int pen[] = { 300, 50, 420, 90, 440, 200, 350, 300, 280, 180 };       /* (no sharp corner: no two sides on the same dots) */
      static const int tri[] = { 300, 50, 420, 90, 350, 300 };
      gpoly(pen, 5); gflush(); keep(); n = count();
      gclear(); gink(G_WHITE); gpoly(pen, 5); gflush(); OK(same() && n > 0, "a five-sided outline: white and G_XOR the same %d dots", n);
      gink(G_XOR); gpoly(pen, 5); gflush(); OK(count() == 0, "... drawn again in G_XOR it is gone");
      gpoly(tri, 3); gpoly(tri, 3); gpoly(tri, 1); gpoly(tri, 2); gpoly(tri, 2); gflush(); OK(count() == 1 && lit(300, 50), "a triangle twice, a line there and back twice, in G_XOR: gone; one point: a dot");
      gclear(); }
    gmove(10, 10); gdraw(60, 10); gdraw(60, 40); gflush(); OK(count() == 81 && lit(60, 40) && lit(60, 10), "an open chain: its last dot, its corner once");
    gdraw(10, 40); gflush(); n = count(); OK(n == 81 + 50 - 1, "a chain taken up after gflush starts on a dot already drawn (G_XOR): %d", n);
    gclear(); gink(G_WHITE);
    gmove(5, 5); gdraw(5, 5); gflush(); OK(count() == 1, "the pen drawn to where it is: a dot");

    /* rectangles */
    gclear(); gbox(10, 20, 30, 50); gflush(); OK(count() == 2 * 21 + 2 * 29, "gbox");
    gclear(); gbox(30, 50, 10, 20); gflush(); OK(count() == 2 * 21 + 2 * 29 && lit(10, 20) && lit(30, 50) && !lit(11, 21), "gbox from the other corner");
    gclear(); gink(G_XOR); gbox(10, 20, 10, 50); gbox(40, 7, 90, 7); gbox(3, 3, 3, 3); gflush(); OK(count() == 31 + 51 + 1, "boxes with no inside, in G_XOR: %d", count());
    gclear(); gink(G_WHITE); gfbox(10, 20, 30, 50); gflush(); OK(count() == 21 * 31, "gfbox");
    gink(G_XOR); gfbox(30, 50, 10, 20); gflush(); OK(count() == 0, "gfbox in G_XOR from the other corner");
    gink(G_WHITE);

    /* circles: against the 64-bit walk, the eight-way symmetry, G_XOR */
    bad = 0;
    for (r = 0; r <= 239 && !bad; r++) {
        gclear(); gcircle(320, 240, r); gflush(); keep();
        refwalk(r, r);
        for (y = -240; y < 240 && !bad; y++) for (x = -320; x < 320; x++) {
            int want = abs(x) <= 640 && abs(y) <= 480 ? ref[abs(y)][abs(x)] : 0;
            if (lit(320 + x, 240 + y) != want) { printf("circle %d: dot %d,%d is %d\n", r, x, y, !want); bad = 1; break; }
            if (abs(x) < 240 && abs(y) < 240 && lit(320 + x, 240 + y) != lit(320 + y, 240 + x)) { printf("circle %d: not the same turned over at %d,%d\n", r, x, y); bad = 1; break; }
        }
        gclear(); gink(G_XOR); gcircle(320, 240, r); gflush();
        if (!same()) { printf("circle %d: a dot drawn twice\n", r); bad = 1; }
        gink(G_WHITE);
    }
    OK(!bad, "circles of radius 0 to 239: the walk's dots, the same in all eight parts, each drawn once");
    /* a circle against the textbook's: one eighth and its seven images */
    bad = 0; n = 0;
    for (r = 1; r <= 239 && !bad; r++) {
        int px = 0, py = r, dd = 1 - r;
        static unsigned char t[481][481];
        memset(t, 0, sizeof t);
        while (px <= py) { t[py][px] = t[px][py] = 1; if (dd < 0) dd += 2 * px + 3; else { dd += 2 * (px - py) + 5; py--; } px++; }
        refwalk(r, r);
        for (y = 0; y <= r; y++) for (x = 0; x <= r; x++) if (t[y][x] != ref[y][x]) {
            if (x == y && t[y][x]) { n++; continue; }
            printf("circle %d differs from the textbook's at %d,%d\n", r, x, y); bad = 1;
        }
    }
    OK(!bad, "... and they are the textbook midpoint circle's, but for a corner dot on the diagonal that it has and this has not (%d of the 239)", n);

    /* ellipses: the quarter that shows with the centre in the corner, against the 64-bit walk; then whole ones */
    bad = 0; n = 0;
    for (a = 0; a <= 639 && !bad; a += (a < 40 ? 1 : 37)) for (b = 0; b <= 479 && !bad; b += (b < 40 ? 1 : 29)) {
        gclear(); gellipse(0, 0, a, b); gflush(); n++;
        refwalk(a, b);
        for (y = 0; y < 480 && !bad; y++) for (x = 0; x < 640; x++)
            if (lit(x, y) != ref[y][x]) { printf("ellipse %d,%d: dot %d,%d is %d\n", a, b, x, y, lit(x, y)); bad = 1; break; }
    }
    gclear(); gellipse(0, 0, 639, 479); gflush(); refwalk(639, 479);
    for (y = 0; y < 480; y++) for (x = 0; x < 640; x++) if (lit(x, y) != ref[y][x]) bad = 1;
    OK(!bad, "%d ellipses up to 639 by 479: the 64-bit walk's dots (nothing overflows)", n + 1);
    bad = 0; n = 0;
    for (a = 0; a <= 300 && !bad; a += (a < 12 ? 1 : 23)) for (b = 0; b <= 230 && !bad; b += (b < 12 ? 1 : 19)) {
        int c8 = 1;
        gclear(); gink(G_WHITE); gellipse(320, 240, a, b); gflush(); keep(); n++;
        refwalk(a, b);
        for (y = -239; y < 240 && !bad; y++) for (x = -319; x < 320; x++)
            if (lit(320 + x, 240 + y) != ref[abs(y)][abs(x)]) { printf("ellipse %d,%d: %d,%d\n", a, b, x, y); bad = 1; break; }
        for (y = -b; y <= b && c8; y++) for (x = -a; x <= a; x++) if (was(320 + x, 240 + y)) {      /* joined up: a neighbour each side */
            int nb = 0, i, j;
            for (j = -1; j <= 1; j++) for (i = -1; i <= 1; i++) if ((i || j) && was(320 + x + i, 240 + y + j)) nb++;
            if (nb < 2 && a + b > 0 && !(nb == 1 && (x == 0 || y == 0))) {       /* (a thin one's tip, a line's end: one neighbour) */ printf("ellipse %d,%d: %d,%d has %d neighbours\n", a, b, x, y, nb); c8 = 0; bad = 1; break; }
        }
        gclear(); gink(G_XOR); gellipse(320, 240, a, b); gflush();
        if (!same()) { printf("ellipse %d,%d: a dot drawn twice\n", a, b); bad = 1; }
        /* filled: every line from its outline's leftmost dot to its rightmost */
        gclear(); gink(G_WHITE); gfellipse(320, 240, a, b); gflush();
        for (y = -239; y < 240 && !bad; y++) {
            int lo = 999, hi = -999;
            for (x = -319; x < 320; x++) if (was(320 + x, 240 + y)) { if (x < lo) lo = x; hi = x; }
            for (x = -319; x < 320; x++) if (lit(320 + x, 240 + y) != (x >= lo && x <= hi)) { printf("filled %d,%d: %d,%d\n", a, b, x, y); bad = 1; break; }
        }
        keep(); gclear(); gink(G_XOR); gfellipse(320, 240, a, b); gflush();
        if (!same()) { printf("filled %d,%d: a dot drawn twice\n", a, b); bad = 1; }
    }
    gink(G_WHITE);
    OK(!bad, "%d whole ellipses: the walk's dots, joined up, each once; filled: each line from edge to edge, each dot once", n);

    /* how near the true curve: every dot within 0.75 of a dot of it */
    bad = 0;
    for (k = 0; k < 6 && !bad; k++) {
        static const int ab[6][2] = { { 200, 200 }, { 300, 100 }, { 60, 220 }, { 310, 7 }, { 5, 230 }, { 1, 100 } };
        a = ab[k][0]; b = ab[k][1];
        refwalk(a, b);
        for (y = 0; y <= b; y++) for (x = 0; x <= a; x++) if (ref[y][x]) {
            double best = 1e9, t;
            for (t = 0; t <= 1.5708; t += 0.00005) { double ex = a * cos(t) - x, ey = b * sin(t) - y, e = ex * ex + ey * ey; if (e < best) best = e; }
            if (sqrt(best) > 0.75) { printf("ellipse %d,%d: dot %d,%d is %.2f from the curve\n", a, b, x, y, sqrt(best)); bad = 1; }
        }
    }
    OK(!bad, "the walk's dots are within 0.75 of a dot of the true curve (six shapes, two of them thin)");

    /* far larger than the screen: weights to fewer places; still each dot once, still joined up */
    bad = 0;
    gclear(); gink(G_XOR); gellipse(-2000, 240, 2400, 900); gellipse(-2000, 240, 2400, 900); gflush(); if (count()) bad = 1;
    gink(G_WHITE); gellipse(-2000, 240, 2400, 900); gcircle(320, -9000, 9300); garc(320, 240, 5000, 3000, 80, 100); gflush();
    for (n = 0, y = 0; y < 480; y++) if (lit(399, y) || lit(400, y) || lit(398, y)) n++;
    OK(!bad && n > 0 && count() > 600, "shapes far larger than the screen: %d dots on it", count());

    /* arcs */
    bad = 0; n = 0;
    {
        static const int an[][2] = { { 0, 90 }, { 90, 180 }, { 45, 135 }, { 10, 350 }, { 350, 10 }, { 200, 20 }, { 0, 180 }, { 0, 181 }, { 30, 30 }, { -90, 90 }, { 0, 360 }, { 100, 460 }, { 0, 359 }, { 123, 124 } };
        static const int sh[][2] = { { 100, 100 }, { 150, 60 }, { 40, 200 }, { 7, 7 } };
        int i, j;
        for (j = 0; j < 4; j++) for (i = 0; i < (int)(sizeof an / sizeof *an) && !bad; i++) {
            int a0 = an[i][0], a1 = an[i][1], sw = a1 - a0 >= 360 ? 360 : ((a1 - a0) % 360 + 360) % 360, in = 0, out = 0;
            a = sh[j][0]; b = sh[j][1];
            gclear(); gink(G_WHITE); gellipse(320, 240, a, b); gflush(); keep();
            gclear(); garc(320, 240, a, b, a0, a1); gflush(); n++;
            for (y = -239; y < 240; y++) for (x = -319; x < 320; x++) {
                double th = atan2(y, x) * 180 / 3.14159265358979 - a0, slack = 0.6 + 40.0 / (abs(x) + abs(y) + 1);
                int on = lit(320 + x, 240 + y);
                th = fmod(fmod(th, 360) + 360, 360);
                if (on && !was(320 + x, 240 + y)) { printf("arc %d,%d %d..%d: %d,%d is not of the ellipse\n", a, b, a0, a1, x, y); bad = 1; }
                if (!was(320 + x, 240 + y)) continue;
                if (sw == 360) { if (!on) bad = 1; continue; }
                if (th > slack && th < sw - slack && !on) { printf("arc %d,%d %d..%d: %d,%d (%.1f) missing\n", a, b, a0, a1, x, y, th); bad = 1; }
                if (th > sw + slack && th < 360 - slack && on) { printf("arc %d,%d %d..%d: %d,%d (%.1f) too many\n", a, b, a0, a1, x, y, th); bad = 1; }
                if (on) in++; else out++;
            }
            if (sw > 2 && sw < 358 && (!in || !out)) { printf("arc %d,%d %d..%d: %d in, %d out\n", a, b, a0, a1, in, out); bad = 1; }
            keep(); gclear(); gink(G_XOR); garc(320, 240, a, b, a0, a1); gflush();
            if (!same()) { printf("arc %d,%d %d..%d: a dot drawn twice\n", a, b, a0, a1); bad = 1; }
            /* the arc and the rest of the turn make the ellipse, sharing at most their end dots */
            if (sw > 0 && sw < 360) {
                int both = 0, miss = 0;
                gclear(); gink(G_WHITE);
                gellipse(320, 240, a, b); gflush();
                { static uint16_t e[480 * MW]; memcpy(e, mem, sizeof e);
                  gclear(); garc(320, 240, a, b, a0, a1); gflush(); keep();
                  gclear(); garc(320, 240, a, b, a1, a0); gflush();
                  for (y = 0; y < 480; y++) for (x = 0; x < 640; x++) { int el = e[(479 - y) * MW + (x >> 4)] >> (x & 15) & 1; if (was(x, y) && lit(x, y)) both++; if (el && !was(x, y) && !lit(x, y)) miss++; }
                }
                if (miss || both > 4) { printf("arc %d,%d %d..%d and the rest: %d dots in both, %d in neither\n", a, b, a0, a1, both, miss); bad = 1; }
            }
        }
    }
    gink(G_WHITE);
    OK(!bad, "%d arcs: dots of the ellipse, those between the two directions, each once; an arc and the rest make the whole", n);

    /* clipping */
    gclear(); gclip(100, 100, 200, 180);
    gfbox(0, 0, 639, 479); gflush(); OK(count() == 101 * 81, "a filled screen through a clipping rectangle: %d", count());
    gclear(); gcircle(150, 140, 60); gline(0, 0, 639, 479); gtext(90, 170, "clipped text"); gfcircle(200, 180, 30); gflush();
    for (bad = 0, y = 0; y < 480; y++) for (x = 0; x < 640; x++) if (lit(x, y) && (x < 100 || x > 200 || y < 100 || y > 180)) bad = 1;
    OK(!bad && count() > 500, "circle, line, text, disc: nothing outside it");
    gclip(-50, -50, 5000, 5000);
    gclear(); gfbox(-100, -100, 1000, 1000); gline(-10, 470, 700, 475); gcircle(639, 0, 50); gtext(600, -8, "edge"); gtext(-4, 470, "edge"); gflush();
    OK(count() == 640 * 480, "the clipping rectangle is never larger than the screen");
    OK(!memcmp(under, mem + 480 * MW, sizeof under), "... and nothing was drawn under it, in the font's pictures");
    gclip(0, 0, gwidth - 1, gheight - 1);

    /* text */
    {
        int c, i, j, a8;
        bad = 0;
        for (a8 = 0; a8 < 8; a8 += 2) {
            gclear(); gink(G_WHITE); glook(a8); gtext(16, 100, "Aq~ {"); gflush();
            for (k = 0; k < 5; k++) { c = "Aq~ {"[k];
                for (j = 0; j < 16; j++) for (i = 0; i < 8; i++) {
                    int fx = (c & 31) * 8 + i, fy = -1 - 48 * a8 - 16 * ((c - 32) >> 5) - j;       /* the picture's dot */
                    int f = mem[(479 - fy) * MW + (fx >> 4)] >> (fx & 15) & 1;
                    if (lit(16 + 8 * k + i, 115 - j) != f) bad = 1;
                } }
        }
        OK(!bad && count() > 30, "text: the font's dots, plain, underlined, bold, both; its bottom-left dot where asked");
        glook(0);
        gclear(); gtext(16, 100, "Hello"); gflush(); keep(); n = count();
        gclear(); gfbox(0, 90, 100, 130); gink(G_BLACK); gtext(16, 100, "Hello"); gflush();
        for (bad = 0, y = 90; y <= 130; y++) for (x = 0; x <= 100; x++) if (lit(x, y) == was(x, y)) bad = 1;
        OK(!bad, "black text on white: the same dots, put out");
        gclear(); gink(G_WHITE); gfbox(0, 90, 100, 130); gink(G_XOR); gtext(16, 100, "Hello"); gflush();
        for (bad = 0, y = 90; y <= 130; y++) for (x = 0; x <= 100; x++) if (lit(x, y) == was(x, y)) bad = 1;
        gtext(16, 100, "Hello"); gflush();
        OK(!bad && count() == 101 * 41, "text in G_XOR, and again: gone");
        gclear(); gink(G_WHITE); gfbox(0, 90, 100, 130); glook(G_OPAQUE); gtext(16, 100, "Hello"); gflush();
        for (bad = 0, y = 90; y <= 130; y++) for (x = 0; x <= 100; x++) {
            int cell = x >= 16 && x < 56 && y >= 100 && y < 116;
            if (lit(x, y) != (cell ? was(x, y) : 1)) bad = 1;
        }
        OK(!bad, "opaque white text: its cells black but for the letters");
        gclear(); gink(G_BLACK); gtext(16, 100, "Hello"); gflush();
        for (bad = 0, y = 90; y <= 130; y++) for (x = 0; x <= 100; x++) {
            int cell = x >= 16 && x < 56 && y >= 100 && y < 116;
            if (lit(x, y) != (cell ? !was(x, y) : 0)) bad = 1;
        }
        OK(!bad, "opaque black text: its cells white but for the letters");
        glook(0); gink(G_WHITE);
        gclear(); gtext(16, 100, "ab\ncd\x01"); gflush(); keep();
        gclear(); gtext(16, 100, "ab"); gtext(16, 84, "cd?"); gflush();
        OK(same() && n > 0, "a new line in text goes down a cell; no such character is a ?");
    }

    /* line styles: the 16 dots over and over along the line, the top bit first; only the 1s are drawn */
    {
#define ST(k) (0xF0F0 >> (15 - ((k) & 15)) & 1)
        gclear(); gstyle(0xF0F0); gline(10, 50, 73, 50); gflush();
        for (bad = 0, x = 10; x <= 73; x++) if (lit(x, 50) != ST(x - 10)) bad = 1;
        OK(!bad && count() == 32, "a line with a style: its first dot the style's top bit");
        gclear(); gline(100, 200, 100, 137); gline(300, 300, 251, 300); gflush();
        for (bad = 0, k = 0; k < 64; k++) if (lit(100, 200 - k) != ST(k)) bad = 1;
        for (k = 0; k < 50; k++) if (lit(300 - k, 300) != ST(k)) bad = 1;
        OK(!bad && count() == 32 + 26, "styled lines downwards and leftwards: from their first dot");
        gclear(); gmove(0, 0); gdraw(20, 0); gdraw(20, 20); gflush();
        for (bad = 0, k = 0; k <= 40; k++) if (lit(k < 20 ? k : 20, k < 20 ? 0 : k - 20) != ST(k)) bad = 1;
        OK(!bad && count() == 21, "the style goes on round a corner of a chain");
        gclear(); gbox(10, 10, 40, 30); gflush();
        for (bad = 0, k = 0; k < 100; k++) {
            x = k < 30 ? 10 + k : k < 50 ? 40 : k < 80 ? 40 - (k - 50) : 10;
            y = k < 30 ? 10 : k < 50 ? 10 + (k - 30) : k < 80 ? 30 : 30 - (k - 80);
            if (lit(x, y) != ST(k)) bad = 1;
        }
        for (n = 0, k = 0; k < 100; k++) n += ST(k);
        OK(!bad && count() == n, "a styled box: round from its first corner");
        gclear(); gstyle(0xFFFF); gline(5, 5, 300, 123); gflush(); keep(); n = count();
        gclear(); gstyle(0xF0F0); gline(5, 5, 300, 123); gflush();
        for (bad = 0, y = 0; y < 480; y++) for (x = 0; x < 640; x++) if (lit(x, y) && !was(x, y)) bad = 1;
        OK(!bad && n == 296 && count() == 148, "a slanted styled line: half the whole line's dots, and none else");
        gink(G_XOR); gline(5, 5, 300, 123); gflush(); OK(count() == 0, "a styled line in G_XOR, again: gone");
        gink(G_WHITE); gstyle(0xFFFF); gfbox(0, 0, 99, 9); gink(G_BLACK); gstyle(0xF0F0); gline(0, 5, 99, 5); gflush();
        for (bad = 0, x = 0; x < 100; x++) if (lit(x, 5) == ST(x)) bad = 1;
        OK(!bad && count() == 1000 - 52, "a styled line in black puts out its 1s only");
        gink(G_WHITE); gclear(); gcircle(100, 100, 30); gfbox(200, 200, 230, 230); gpoint(5, 5); gflush(); keep();
        gstyle(0xFFFF); gclear(); gcircle(100, 100, 30); gfbox(200, 200, 230, 230); gpoint(5, 5); gflush();
        OK(same(), "a style is for lines: circles, filled boxes and dots are whole");
        gclear(); gline(10, 50, 73, 50); gflush(); OK(count() == 64, "gstyle(0xFFFF): whole lines again");
    }

    /* fill patterns: laid over the screen from its top-left corner; only the 1s are drawn */
    {
#define PT(x, y) (P[(479 - (y)) & 15] >> (15 - ((x) & 15)) & 1)
        gclear(); gpattern(P); gfbox(3, 5, 200, 150); gflush();
        for (bad = 0, y = 0; y < 480; y++) for (x = 0; x < 640; x++)
            if (lit(x, y) != (x >= 3 && x <= 200 && y >= 5 && y <= 150 && PT(x, y))) bad = 1;
        OK(!bad, "a patterned box: each dot the pattern's for that place on the screen");
        gpattern(0); gclear(); gfcircle(300, 200, 77); gfellipse(120, 300, 100, 37); gfpoly(tri, 3); gflush(); keep();
        gclear(); gpattern(P); gfcircle(300, 200, 77); gfellipse(120, 300, 100, 37); gflush();
        gfpoly(tri, 3); gflush();
        for (bad = 0, y = 0; y < 480; y++) for (x = 0; x < 640; x++) if (lit(x, y) != (was(x, y) && PT(x, y))) bad = 1;
        OK(!bad, "a patterned circle, ellipse and outline: the solid ones' dots where the pattern has a 1");
        gclear(); gfcircle(300, 200, 77); gink(G_XOR); gfcircle(300, 200, 77); gflush(); n = count();
        gink(G_WHITE); gfellipse(120, 300, 100, 37); gink(G_XOR); gfellipse(120, 300, 100, 37); gflush(); n += count();
        gink(G_WHITE); gfpoly(tri, 3); gink(G_XOR); gfpoly(tri, 3); gflush(); n += count();
        OK(n == 0, "patterned figures in G_XOR over themselves: gone");
        gink(G_WHITE); gpattern(0); gfbox(0, 0, 63, 63); gink(G_BLACK); gpattern(P); gfbox(0, 0, 63, 63); gflush();
        for (bad = 0, y = 0; y < 64; y++) for (x = 0; x < 64; x++) if (lit(x, y) == PT(x, y)) bad = 1;
        OK(!bad, "a pattern in black puts out its 1s only");
        gink(G_WHITE); gclear(); gstyle(0xF0F0); gline(0, 100, 63, 100); gfbox(0, 0, 63, 63); gstyle(0xFFFF); gflush();
        for (bad = 0, y = 0; y < 64; y++) for (x = 0; x < 64; x++) if (lit(x, y) != PT(x, y)) bad = 1;
        for (x = 0; x < 64; x++) if (lit(x, 100) != ST(x)) bad = 1;
        OK(!bad, "a styled line and then a patterned box: each as it should be");
        gclear(); gcircle(100, 100, 30); gbox(200, 200, 230, 230); gline(0, 0, 50, 20); gtext(300, 300, "Ab"); gflush(); keep();
        gpattern(0); gclear(); gcircle(100, 100, 30); gbox(200, 200, 230, 230); gline(0, 0, 50, 20); gtext(300, 300, "Ab"); gflush();
        OK(same(), "a pattern is for filled figures: outlines, lines and text are whole");
        gclear(); gpattern(ones); gfbox(0, 0, 99, 99); gpattern(P); gpattern(0); gfbox(100, 0, 199, 99); gflush();
        OK(count() == 20000, "a pattern of all 1s, and gpattern(0): solid");
    }

    /* filled outlines: every dot inside or on the outline, each once */
    {
        static int q[2 * 16];
        static const int star[] = { 320, 440, 250, 60, 600, 300, 40, 300, 390, 60 };
        static const int box[] = { 10, 10, 80, 10, 80, 60, 10, 60 };
        static const int steps[] = { 0, 0, 100, 0, 100, 20, 60, 20, 60, 50, 100, 50, 100, 80, 0, 80, 0, 50, 30, 50, 30, 20, 0, 20 };
        int t, m, in;
#define INPOLY(P_, N_, X_, Y_, R_) do { int _k, _on = 0, _c = 0; \
            for (_k = 0; _k < (N_); _k++) { \
                long long px = (P_)[2 * _k], py = (P_)[2 * _k + 1], qx = (P_)[2 * ((_k + 1) % (N_))], qy = (P_)[2 * ((_k + 1) % (N_)) + 1]; \
                if ((qx - px) * ((Y_) - py) == (qy - py) * ((X_) - px) && (X_) >= (px < qx ? px : qx) && (X_) <= (px < qx ? qx : px) \
                    && (Y_) >= (py < qy ? py : qy) && (Y_) <= (py < qy ? qy : py)) _on = 1; \
                if ((py > (Y_)) != (qy > (Y_))) { long long l = ((X_) - px) * (qy - py), r = ((Y_) - py) * (qx - px); if (qy > py ? l < r : l > r) _c ^= 1; } \
            } (R_) = _on || _c; } while (0)
#define POLYCHECK(P_, N_, WHAT) do { gclear(); gfpoly(P_, N_); gflush(); \
            for (bad = 0, y = 0; y < 480; y++) for (x = 0; x < 640; x++) { INPOLY(P_, N_, x, y, in); if (lit(x, y) != in) bad = 1; } \
            n = count(); gink(G_XOR); gfpoly(P_, N_); gflush(); gink(G_WHITE); OK(!bad && n > 0 && count() == 0, WHAT); } while (0)
        POLYCHECK(tri, 3, "a filled triangle: the dots inside it or on it, each once");
        POLYCHECK(star, 5, "a filled star (its sides cross): inside by odd crossings");
        POLYCHECK(steps, 12, "a filled outline with level sides and inward corners");
        gclear(); gfpoly(box, 4); gflush(); keep(); gclear(); gfbox(10, 10, 80, 60); gflush();
        OK(same() && count() == 71 * 51, "a filled outline of four square corners is the filled box");
        for (bad = 0, t = 0; t < 60 && !bad; t++) {
            m = 3 + t % 10;
            for (k = 0; k < 2 * m; k++) { seedv = seedv * 1103515245u + 12345u; q[k] = (int)(seedv >> 16) % (k & 1 ? 560 : 760) - 40; }
            gclear(); gfpoly(q, m); gflush();
            for (y = 0; y < 480 && !bad; y++) for (x = 0; x < 640; x++) { INPOLY(q, m, x, y, in); if (lit(x, y) != in) { bad = 1; break; } }
            gink(G_XOR); gfpoly(q, m); gflush(); gink(G_WHITE);
            if (count()) bad = 1;
        }
        OK(!bad, "60 outlines of 3 to 12 points taken at random, partly off the screen: the same");
        gclear(); gclip(100, 100, 300, 200); gfpoly(star, 5); gclip(0, 0, 639, 479); gflush();
        for (bad = 0, y = 0; y < 480; y++) for (x = 0; x < 640; x++) {
            INPOLY(star, 5, x, y, in);
            if (lit(x, y) != (in && x >= 100 && x <= 300 && y >= 100 && y <= 200)) bad = 1;
        }
        OK(!bad, "a filled outline is clipped");
        { static const int one[] = { 33, 44 }; gclear(); gfpoly(one, 1); gflush(); OK(count() == 1 && lit(33, 44), "an outline of one point is a dot"); }
    }

    /* gfill: an area up to its border */
    {
        static unsigned char ref2[480][640];
        static int st[640 * 480];
        int sp, c, rc;
        gclear(); gfcircle(200, 200, 50); gflush(); keep();
        gclear(); gcircle(200, 200, 50); rc = gfill(200, 200); gflush();
        OK(rc == 0 && same(), "a circle filled from its middle is the filled circle");
        rc = gfill(0, 0); gflush(); OK(rc == 0 && count() == 640 * 480, "... and from outside it: the rest of the screen");
        gclear(); gclip(100, 100, 200, 180); rc = gfill(150, 150); gclip(0, 0, 639, 479); gflush();
        OK(rc == 0 && count() == 101 * 81 && lit(100, 100) && lit(200, 180), "gfill stops at the clipping rectangle");
        gfill(300, 300); gflush(); OK(count() == 640 * 480, "... and goes round it");
        gink(G_BLACK); gfill(5, 5); gflush(); OK(count() == 0, "a lit area filled in black");
        gink(G_WHITE);
        /* a room with a wall half way down its middle: from either side the other is reached only round the wall's end */
        gclear(); gbox(100, 100, 400, 400); gline(250, 250, 250, 400); rc = gfill(300, 350); gflush(); n = count();
        gclear(); gbox(100, 100, 400, 400); gline(250, 250, 250, 400); rc |= gfill(200, 350); gflush();
        OK(rc == 0 && n == 301 * 301 && count() == 301 * 301, "gfill round the end of a wall, leftwards and rightwards");
        gclear(); gbox(100, 100, 400, 400); gline(250, 100, 250, 250); rc = gfill(300, 150); gflush(); n = count();
        gclear(); gbox(100, 100, 400, 400); gline(250, 100, 250, 250); rc |= gfill(200, 150); gflush();
        OK(rc == 0 && n == 301 * 301 && count() == 301 * 301, "... and with the wall coming up from below");
        for (k = 0; k < 40; k++) {              /* a maze of lines and circles, then the area a dot lies in */
            seedv = seedv * 1103515245u + 12345u; x = (int)(seedv >> 16) % 640;
            seedv = seedv * 1103515245u + 12345u; y = (int)(seedv >> 16) % 480;
            seedv = seedv * 1103515245u + 12345u; a = (int)(seedv >> 16) % 640;
            seedv = seedv * 1103515245u + 12345u; b = (int)(seedv >> 16) % 480;
            if (k % 3) gline(x, y, a, b); else gcircle(x, y, 10 + a % 90);
        }
        gflush(); keep();
        for (y = 0; y < 480; y++) for (x = 0; x < 640; x++) ref2[y][x] = 0;
        c = was(317, 233); sp = 0; st[sp++] = 233 * 640 + 317; ref2[233][317] = 1;
        while (sp) {
            static const int dx[] = { 1, -1, 0, 0 }, dy[] = { 0, 0, 1, -1 };
            int at = st[--sp];
            for (k = 0; k < 4; k++) {
                x = at % 640 + dx[k]; y = at / 640 + dy[k];
                if (x >= 0 && x < 640 && y >= 0 && y < 480 && !ref2[y][x] && was(x, y) == c) { ref2[y][x] = 1; st[sp++] = y * 640 + x; }
            }
        }
        gink(c ? G_BLACK : G_WHITE); rc = gfill(317, 233); gflush(); gink(G_WHITE);
        for (bad = 0, n = 0, y = 0; y < 480; y++) for (x = 0; x < 640; x++) { n += ref2[y][x]; if (lit(x, y) != (ref2[y][x] ? !c : was(x, y))) bad = 1; }
        OK(rc == 0 && !bad && n > 1000, "gfill among 40 lines and circles: the dots joined to that one, and no other (%d)", n);
        gclear(); gbox(50, 50, 150, 130); gpattern(P); rc = gfill(100, 100); gpattern(0); gflush();
        for (bad = 0, y = 0; y < 480; y++) for (x = 0; x < 640; x++) {
            int edge = (x == 50 || x == 150) ? y >= 50 && y <= 130 : (y == 50 || y == 130) && x >= 50 && x <= 150;
            if (lit(x, y) != (edge || (x > 50 && x < 150 && y > 50 && y < 130 && PT(x, y)))) bad = 1;
        }
        OK(rc == 0 && !bad, "gfill with a pattern");
        gclear(); rc = gfill(-1, 5) | gfill(5, 480); gflush(); OK(rc == 0 && count() == 0, "gfill from a dot off the screen: nothing");
    }

    /* pieces of the picture copied, kept, put down, read and written */
    {
        static unsigned short bits[480 * 40], bits2[480 * 40];
        int id, id2, ids[8], w;
        gclear(); gtext(40, 100, "Copy me"); gcircle(70, 108, 25); gflush(); keep();
        gcopy(10, 80, 110, 140, 300, 200); gflush();
        for (bad = 0, y = 0; y < 480; y++) for (x = 0; x < 640; x++) {
            int in = x >= 300 && x <= 400 && y >= 200 && y <= 260;
            if (lit(x, y) != (in ? was(x - 290, y - 120) : was(x, y))) bad = 1;
        }
        OK(!bad, "gcopy: a rectangle copied, the rest as it was");
        for (k = 0; k < 8; k++) {               /* onto itself, a little way in each direction */
            static const int ox[] = { 7, -7, 0, 0, 5, -5, 5, -5 }, oy[] = { 0, 0, 6, -6, 4, 4, -4, -4 };
            gclear(); gtext(40, 100, "Copy me"); gcircle(70, 108, 25); gline(10, 80, 110, 140); gflush(); keep();
            gcopy(10, 80, 110, 140, 10 + ox[k], 80 + oy[k]); gflush();
            for (y = 0; y < 480; y++) for (x = 0; x < 640; x++) {
                int in = x >= 10 + ox[k] && x <= 110 + ox[k] && y >= 80 + oy[k] && y <= 140 + oy[k];
                if (lit(x, y) != (in ? was(x - ox[k], y - oy[k]) : was(x, y))) bad = 1;
            }
        }
        OK(!bad, "gcopy onto itself, eight ways: what was there is what arrives");
        gclear(); gfbox(0, 0, 30, 30); gflush(); gclip(100, 100, 110, 110); gcopy(0, 0, 30, 30, 95, 95); gclip(0, 0, 639, 479);
        gcopy(-10, -10, 9, 9, 200, 200); gflush();
        for (bad = 0, y = 0; y < 480; y++) for (x = 0; x < 640; x++)
            if (lit(x, y) != ((x <= 30 && y <= 30) || (x >= 100 && x <= 110 && y >= 100 && y <= 110) || (x >= 210 && x <= 219 && y >= 210 && y <= 219))) bad = 1;
        OK(!bad, "gcopy is clipped; a rectangle partly off the screen is copied as far as it is on it");

        gclear(); gtext(40, 100, "Keep"); gfcircle(60, 108, 12); gflush(); keep();
        id = gkeep(30, 90, 90, 125); gclear(); gstamp(id, 400, 300, G_COPY); gflush();
        for (bad = 0, y = 0; y < 480; y++) for (x = 0; x < 640; x++) {
            int in = x >= 400 && x <= 460 && y >= 300 && y <= 335;
            if (lit(x, y) != (in && was(x - 370, y - 210))) bad = 1;
        }
        OK(id >= 0 && !bad, "gkeep and gstamp: a piece of the picture kept, and put down elsewhere");
        n = count(); gstamp(id, 400, 300, G_FLIP); gflush(); OK(n > 0 && count() == 0, "gstamp G_FLIP on itself: gone");
        gfbox(390, 290, 470, 345); gstamp(id, 400, 300, G_MASK); gflush(); OK(count() == n + 81 * 56 - 61 * 36, "gstamp G_MASK: only its own dots stay lit in its rectangle");
        gclear(); gfbox(400, 300, 430, 320); gstamp(id, 400, 300, G_OVER); gflush();
        for (bad = 0, y = 0; y < 480; y++) for (x = 0; x < 640; x++) {
            int in = x >= 400 && x <= 460 && y >= 300 && y <= 335;
            if (lit(x, y) != ((x >= 400 && x <= 430 && y >= 300 && y <= 320) || (in && was(x - 370, y - 210)))) bad = 1;
        }
        OK(!bad && count() < 31 * 21 + n, "gstamp G_OVER: its dots added to what is there");
        gclear(); gclip(410, 310, 420, 320); gstamp(id, 400, 300, G_COPY); gclip(0, 0, 639, 479); gflush();
        for (bad = 0, y = 0; y < 480; y++) for (x = 0; x < 640; x++)
            if (lit(x, y) != (x >= 410 && x <= 420 && y >= 310 && y <= 320 && was(x - 370, y - 210))) bad = 1;
        OK(!bad, "gstamp is clipped");
        gclear();
        for (k = 0; k < 8; k++) { gclear(); gfbox(0, 0, 9 + 3 * k, 5 + 7 * k); gflush(); ids[k] = gkeep(0, 0, 9 + 3 * k, 5 + 7 * k); }
        gclear();
        for (bad = 0, k = 0; k < 8; k++) {
            if (ids[k] < 0) bad = 1;
            gclear(); gstamp(ids[k], 100, 100, G_COPY); gflush();
            if (count() != (10 + 3 * k) * (6 + 7 * k) || !lit(100, 100) || !lit(109 + 3 * k, 105 + 7 * k)) bad = 1;
        }
        gclear(); gstamp(id, 400, 300, G_COPY); gflush();
        OK(!bad && count() == n, "eight more kept pictures of different sizes: each whole, the first too");
        gdrop(ids[3]); gdrop(ids[5]); gclear(); gfbox(0, 0, 4, 4); gflush(); id2 = gkeep(0, 0, 4, 4);
        gclear(); gstamp(id2, 7, 7, G_COPY); gstamp(ids[5], 50, 50, G_COPY); gstamp(-1, 60, 60, G_COPY); gstamp(99, 60, 60, G_COPY); gflush();
        OK(id2 == ids[3] && count() == 25, "a dropped picture's room is used again; a dropped or unknown one is not put down");
        for (k = 0; k < 8; k++) gdrop(ids[k]);
        gdrop(id); gdrop(id2);
        gclear(); gfbox(0, 0, 639, 479); gflush();
        for (k = 0; gkeep(0, 0, 639, 479) >= 0; k++) ;
        OK(k == 5, "whole screens kept until there is no more room: 5 (%d)", k);
        for (id = 0; id < k; id++) gdrop(id);
        gclear(); gfbox(0, 0, 9, 9); gflush(); id = gkeep(0, 0, 9, 9); gclear(); gstamp(id, 20, 20, G_COPY); gflush();
        OK(id == 0 && count() == 100, "all dropped: there is room again");
        gdrop(id);
        OK(!memcmp(under, mem + 480 * MW, sizeof under), "the font's pictures under the screen are as they were");

        gclear(); gtext(37, 201, "Read"); gcircle(60, 210, 30); gflush(); keep();
        OK(gread(21, 180, 99, 245, bits) == 0, "gread");
        w = (99 - 21 + 16) / 16;
        for (bad = 0, y = 180; y <= 245; y++) for (x = 21; x <= 99; x++)
            if ((bits[(245 - y) * w + ((x - 21) >> 4)] >> (15 - ((x - 21) & 15)) & 1) != lit(x, y)) bad = 1;
        for (y = 180; y <= 245; y++) if (bits[(245 - y) * w + w - 1] & 1) bad = 1;       /* (the dots past the width: 0) */
        OK(!bad, "gread: a line's first dot the top bit of its first word, the top line first");
        gclear(); OK(gwrite(303, 100, 381, 165, bits, G_COPY) == 0, "gwrite");
        for (bad = 0, y = 0; y < 480; y++) for (x = 0; x < 640; x++) {
            int in = x >= 303 && x <= 381 && y >= 100 && y <= 165;
            if (lit(x, y) != (in && was(x - 282, y + 80))) bad = 1;
        }
        OK(!bad && count() > 100, "gwrite: the same dots, somewhere else");
        n = count(); gwrite(303, 100, 381, 165, bits, G_FLIP); OK(count() == 0, "gwrite G_FLIP on itself: gone");
        gfbox(300, 95, 390, 170); gwrite(303, 100, 381, 165, bits, G_COPY);
        OK(count() == n + 91 * 76 - 79 * 66, "gwrite G_COPY writes the 0s of its rectangle too, and nothing outside it");
        gclear(); gclip(310, 110, 330, 130); gwrite(303, 100, 381, 165, bits, G_OVER); gclip(0, 0, 639, 479);
        for (bad = 0, y = 0; y < 480; y++) for (x = 0; x < 640; x++)
            if (lit(x, y) != (x >= 310 && x <= 330 && y >= 110 && y <= 130 && was(x - 282, y + 80))) bad = 1;
        OK(!bad, "gwrite is clipped");
        gclear(); gfbox(0, 0, 639, 479); gread(-8, -8, 23, 7, bits2);
        OK(bits2[0] == 0x00FF && bits2[1] == 0xFFFF && bits2[2 * 7] == 0x00FF && bits2[2 * 8] == 0 && bits2[2 * 15 + 1] == 0, "gread of a rectangle partly off the screen: 0s there");
        gclear(); gtext(0, 464, "All of it"); gline(0, 0, 639, 479); gflush(); keep();
        gread(0, 0, 639, 479, bits); gclear(); gwrite(0, 0, 639, 479, bits, G_COPY);
        OK(same(), "the whole screen read, cleared and written back");
    }

    /* a kept picture drawn into (gmake, gtarget), and put down */
    {
        static unsigned short bits[120 * 13];
        int pa, pb, pc, rc, g1, g2;
        gclear(); gflush();
        pa = gmake(200, 120);
        OK(pa >= 0 && count() == 0, "gmake");
        gtarget(pa);
        OK(gwidth == 200 && gheight == 120, "gtarget: the picture's size is the drawing's");
        gline(0, 0, 199, 119); gcircle(100, 60, 50); gfbox(150, 10, 300, 40); gtext(8, 96, "In it");
        gpattern(P); gfcircle(40, 40, 30); gpattern(0); gflush();
        OK(count() == 0 && !memcmp(under, mem + 480 * MW, sizeof under), "drawing into it: nothing on the screen, nor on the font");
        gtarget(-1);
        OK(gwidth == 640 && gheight == 480, "gtarget(-1): the screen again");
        gstamp(pa, 160, 200, G_COPY); gflush(); keep(); n = count();
        gclear(); gclip(160, 200, 359, 319);        /* the same calls on the screen, there, cut to the picture's size */
        gline(160, 200, 359, 319); gcircle(260, 260, 50); gfbox(310, 210, 460, 240); gtext(168, 296, "In it");
        gpattern(P); gfcircle(200, 240, 30); gpattern(0); gclip(0, 0, 639, 479); gflush();
        OK(same() && n > 500, "what was drawn into it is what the same calls draw on the screen");
        gtarget(pa); gclear(); gbox(10, 10, 100, 100); rc = gfill(50, 50); g1 = gget(50, 50); g2 = gget(150, 50);
        rc |= gread(0, 0, 199, 119, bits); gtarget(-1);
        gclear(); gwrite(300, 300, 499, 419, bits, G_COPY); gflush(); keep();
        gclear(); gstamp(pa, 300, 300, G_COPY); gflush();
        OK(rc == 0 && g1 == 1 && g2 == 0 && same() && count() == 91 * 91, "gclear, gfill, gget and gread in a kept picture");
        pb = gmake(50, 30); pc = gmake(50, 30);
        gtarget(pb); gfbox(-1000, -1000, 1000, 1000); gtarget(pc); gline(0, 0, 49, 29); gtarget(-1);
        gclear(); gstamp(pb, 0, 0, G_COPY); gstamp(pc, 100, 0, G_COPY); gstamp(pa, 300, 300, G_COPY); gflush();
        OK(pb >= 0 && pc >= 0 && count() == 50 * 30 + 50 + 91 * 91 && !memcmp(under, mem + 480 * MW, sizeof under),
           "pictures side by side: what is drawn into one stays in it");
        gtarget(pb); gclear(); gtarget(-1); gclear(); gstamp(pb, 0, 0, G_COPY); gstamp(pc, 100, 0, G_COPY); gflush();
        OK(count() == 50, "gclear in a kept picture clears that one");
        gclear(); gtarget(pb); gfbox(0, 0, 49, 29); gtarget(-1); gclip(10, 5, 19, 9); gstamp(pb, 0, 0, G_COPY);
        gstamp(pb, 100, 100, G_COPY); gclip(0, 0, 639, 479); gflush();
        OK(count() == 50 && lit(10, 5) && lit(19, 9), "gstamp is cut to the clipping rectangle");
        gtarget(pb); gclear(); gfbox(0, 0, 24, 29); gline(30, 0, 49, 0);        /* (its left half, and a line) */
        gtarget(pc); gclear(); gstamp(pb, 10, 5, G_COPY); gtarget(-1); gclear(); gstamp(pc, 0, 0, G_COPY); gflush();
        OK(count() == 25 * 25 + 10 && lit(10, 5) && !lit(9, 5) && !lit(10, 4) && lit(34, 29) && !lit(35, 6) && lit(49, 5),
           "one kept picture put down in another");
        gtarget(pc); gclear(); gtext(0, 0, "ab"); k = gkeep(0, 0, 15, 15); gtarget(-1);
        gclear(); gstamp(k, 64, 64, G_COPY); gflush(); keep(); gclear(); gtext(64, 64, "ab"); gflush();
        OK(k >= 0 && same() && count() > 20, "gkeep from a kept picture");
        gdrop(pa); gdrop(pb); gdrop(pc); gdrop(k);
        gclear();
    }

    /* the chip's own words, the sines */
    { static const unsigned short w[] = { VC_AMOVE, 7, 7, VC_DOT }; gclear(); gwords(w, 4); gflush(); OK(count() == 1 && lit(7, 7), "gwords"); }
    for (bad = 0, k = -720; k <= 720; k++) {
        if (abs(gsin(k) - (int)lround(16384 * sin(k * 3.14159265358979 / 180))) > 0) bad = 1;
        if (abs(gcos(k) - (int)lround(16384 * cos(k * 3.14159265358979 / 180))) > 0) bad = 1;
    }
    OK(!bad && gsin(90) == G_ONE && gcos(180) == -G_ONE, "gsin and gcos, -720 to 720 degrees");
    OK(gsync() == 0, "gsync");
    OK(!dev_bad(), "the chip had no command it does not know and lost no word");
    gclose();
    OK(gfd() == -1 && gflush() == -1, "gclose");
    if (fails) printf("%d of %d checks failed\n", fails, checks); else printf("gfx_test: %d checks\n", checks);
    return fails != 0;
}
