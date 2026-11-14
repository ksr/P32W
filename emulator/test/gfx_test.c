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
