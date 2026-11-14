/* gfx.c -- the graphics library, libgfx (gfx(3), D111), on the machine: figures drawn through it and read
   back from the chip's memory. Each figure's dots are counted and summed up (a number that depends on where
   every one of them is); the numbers must be the ones the same program gave when the system was built, where
   the library was run against the emulators' model of the chip and every dot checked. The rest is checked
   here: both ends of a line, a figure drawn again in G_XOR gone, nothing outside the clipping rectangle, text
   against the font. Needs the video card:  sh /usr/tests/run video */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <gfx.h>
#include <sys/video.h>

#define MW 40                           /* words a line of the chip's memory */
#define W  8                            /* the piece of the screen looked at: 8 words (128 dots) by 128 lines, */
#define N  128                          /* its bottom-left dot the screen's */
static unsigned short pic[N][W];
static int checks, failed;

static void ok(int cond, const char *what)
{
    checks++;
    if (!cond) { failed++; printf("FAIL: %s\n", what); }
}

/* n words of the chip's memory from word a, n up to 8 (the chip's read FIFO holds 8) */
static void words(unsigned a, unsigned short *to, int n)
{
    unsigned short c[4 + 8];
    int k;
    c[0] = VC_WPR | 0x0C; c[1] = 0x4000 | a >> 12; c[2] = VC_WPR | 0x0D; c[3] = (a << 4) & 0xFFFF;
    for (k = 0; k < n; k++) c[4 + k] = VC_RD;
    gwords(c, 4 + n);
    gflush();
    if (read(gfd(), to, 2 * n) != 2 * n) { memset(to, 0xFF, 2 * n); ok(0, "reading the chip's memory"); }
}
static void grab(void)                  /* the piece, as it is now */
{
    int y;
    for (y = 0; y < N; y++) words((unsigned)(gheight - 1 - y) * MW, pic[y], W);
}
static int lit(int x, int y) { return x >= 0 && y >= 0 && x < 16 * W && y < N && (pic[y][x >> 4] >> (x & 15) & 1); }
static int count(void)
{
    int x, y, n = 0;
    for (y = 0; y < N; y++) for (x = 0; x < 16 * W; x++) n += lit(x, y);
    return n;
}
static void show(const char *what)      /* the figure in the piece: how many dots, and their sum */
{
    unsigned s = 0;
    int x, y, n = 0;
    grab();
    for (y = 0; y < N; y++)
        for (x = 0; x < 16 * W; x++)
            if (lit(x, y)) { n++; s = s * 31 + (unsigned)(x * 131 + y); }
    printf("%s: %d dots, sum %u\n", what, n, s);
}
static int blank(void) { grab(); return count() == 0; }

/* is the cell whose bottom-left dot is x, y (in the piece) character ch of the font's picture a, each dot
   turned over if inv? The pictures are under the screen: video(4) */
static int cell(int x, int y, int ch, int a, int inv)
{
    unsigned short v;
    int j, i;
    for (j = 0; j < 16; j++) {
        words((unsigned)(480 + 48 * a + 16 * ((ch - 0x20) >> 5) + j) * MW + (ch & 31) / 2, &v, 1);
        for (i = 0; i < 8; i++)
            if (lit(x + i, y + 15 - j) != ((v >> ((ch & 1) ? 8 + i : i) & 1) ^ inv)) return 0;
    }
    return 1;
}

int main(void)
{
    static const int five[] = { 20, 10, 100, 20, 110, 70, 60, 120, 15, 80 };
    static const int three[] = { 20, 10, 100, 30, 60, 120 };
    int x, y, k, bad;

    if (gopen() < 0) { perror("gfx: /dev/video"); return 2; }
    ok(gwidth == 640 && gheight == 480 && gcellw == 8 && gcellh == 16, "the screen's size");
    ok(blank(), "gopen clears the screen");

    gpoint(0, 0); gpoint(127, 127); gpoint(17, 3);
    ok(gget(0, 0) == 1 && gget(127, 127) == 1 && gget(17, 3) == 1 && gget(16, 3) == 0 && gget(18, 3) == 0 &&
       gget(640, 0) == 0 && gget(0, -1) == 0, "dots, and gget");
    gink(G_BLACK); gpoint(17, 3);
    gink(G_XOR); gpoint(0, 0); gpoint(1, 0);
    ok(gget(17, 3) == 0 && gget(0, 0) == 0 && gget(1, 0) == 1, "a dot put out, dots turned over");
    gink(G_WHITE);

    gclear(); gline(10, 10, 50, 30); grab();
    ok(count() == 41 && lit(10, 10) && lit(50, 30), "a line has both its ends");
    gclear(); gline(60, 60, 20, 20); gline(0, 100, 127, 100); gline(5, 0, 5, 127); gline(9, 9, 9, 9); grab();
    for (bad = 0, k = 20; k <= 60; k++) if (!lit(k, k)) bad = 1;
    ok(!bad && count() == 41 + 128 + 128 - 1 + 1, "lines at 45 degrees, level, upright, of one dot");

    gclear(); gmove(20, 20); gdraw(100, 20); gdraw(100, 90); gdraw(20, 90); gdraw(20, 20); grab();
    ok(count() == 2 * 81 + 2 * 69, "a rectangle drawn with the pen");
    gink(G_XOR); gbox(20, 20, 100, 90);
    ok(blank(), "... and gbox in G_XOR takes it away: the same dots, each once");
    gpoly(five, 5); show("a five-sided outline");
    gpoly(five, 5); gpoly(three, 3); gpoly(three, 3); gpoly(three, 2); gpoly(three, 2);
    ok(blank(), "outlines drawn again in G_XOR are gone");
    gbox(10, 20, 10, 50); gbox(40, 7, 90, 7); gbox(3, 3, 3, 3); grab();
    ok(count() == 31 + 51 + 1, "boxes with no inside");
    gclear(); gink(G_WHITE); gfbox(100, 90, 20, 20); grab();
    ok(count() == 81 * 71 && lit(20, 20) && lit(100, 90) && !lit(19, 20) && !lit(100, 91), "a filled box");

    gclear(); gcircle(64, 64, 30); show("a circle of radius 30");
    for (bad = 0, y = -63; y < 64; y++)
        for (x = -63; x < 64; x++) {
            int d = x * x + y * y - 900;
            if (lit(64 + x, 64 + y) != lit(64 + y, 64 - x) || lit(64 + x, 64 + y) != lit(64 + y, 64 + x)) bad = 1;
            if (lit(64 + x, 64 + y) && (d > 45 || d < -45)) bad = 1;       /* (within 3/4 of a dot of the circle) */
        }
    ok(!bad && lit(94, 64) && lit(64, 94) && lit(34, 64) && lit(64, 34), "... round, and the same turned or mirrored");
    gink(G_XOR); gcircle(64, 64, 30);
    ok(blank(), "... and gone drawn again in G_XOR");
    gink(G_WHITE);
    gcircle(64, 64, 0); gcircle(64, 64, 1); gcircle(64, 64, 2); gcircle(64, 64, 4); show("circles of radius 0, 1, 2 and 4");

    gclear(); gellipse(64, 64, 50, 20); show("an ellipse 50 by 20");
    gink(G_XOR); gellipse(64, 64, 50, 20); gellipse(64, 64, 60, 1); gellipse(64, 64, 1, 60); show("thin ellipses, 60 by 1 and 1 by 60");
    gellipse(64, 64, 1, 60); gellipse(64, 64, 60, 1);
    ok(blank(), "ellipses drawn again in G_XOR are gone");
    gink(G_WHITE);
    gfcircle(64, 64, 30); show("a filled circle of radius 30");
    for (bad = 0, y = -63; y < 64; y++)
        for (x = -63; x < 64; x++)
            if (lit(64 + x, 64 + y) ? x * x + y * y > 900 + 30 : x * x + y * y < 900 - 30) bad = 1;
    ok(!bad, "... every dot inside the circle and none outside (to half a dot)");
    gink(G_XOR); gfcircle(64, 64, 30); gfellipse(64, 64, 20, 60); show("a filled ellipse 20 by 60");
    gfellipse(64, 64, 20, 60);
    ok(blank(), "filled figures drawn again in G_XOR are gone");
    gink(G_WHITE);

    garc(64, 64, 40, 40, 30, 150); show("an arc from 30 to 150 degrees");
    for (bad = 0, y = 0; y < N; y++) for (x = 0; x < 16 * W; x++) if (lit(x, y) && (y < 64 + 19 || x < 64 - 35 || x > 64 + 35)) bad = 1;
    ok(!bad && lit(64, 104) && !lit(104, 64), "... on its side of the circle only");
    gink(G_XOR); garc(64, 64, 40, 40, 150, 30); gcircle(64, 64, 40); grab();
    ok(count() <= 2, "... with the rest of the turn it is the circle (their two end dots apart)");
    gclear(); garc(64, 64, 55, 25, 200, 20); garc(64, 64, 10, 10, 0, 360); show("an ellipse's arc from 200 to 20 degrees, and a whole turn");
    gink(G_WHITE);

    gclear(); gclip(20, 30, 99, 89);
    gfbox(0, 0, 639, 479); grab();
    ok(count() == 80 * 60, "a clipping rectangle: the filled screen is 80 by 60 dots");
    gclear(); gcircle(60, 60, 45); gline(0, 0, 127, 127); gfcircle(100, 90, 20); gtext(4, 80, "clipped text"); grab();
    for (bad = 0, y = 0; y < N; y++) for (x = 0; x < 16 * W; x++) if (lit(x, y) && (x < 20 || x > 99 || y < 30 || y > 89)) bad = 1;
    ok(!bad && count() > 300, "... nothing outside it: a circle, a line, a disc, text");
    gclip(0, 0, gwidth - 1, gheight - 1);

    gclear(); gtext(8, 32, "P3~"); grab();
    ok(cell(8, 32, 'P', 0, 0) && cell(16, 32, '3', 0, 0) && cell(24, 32, '~', 0, 0) && count() > 40, "text: the font's dots");
    gclear(); glook(G_BOLD | G_UNDER); gtext(8, 32, "Wq"); grab();
    ok(cell(8, 32, 'W', 6, 0) && cell(16, 32, 'q', 6, 0), "... bold and underlined");
    glook(0);
    gclear(); gfbox(0, 24, 63, 55); gink(G_BLACK); gtext(8, 32, "Wq"); grab();
    ok(cell(8, 32, 'W', 0, 1) && cell(16, 32, 'q', 0, 1) && lit(7, 32) && lit(24, 40), "... black on white");
    gink(G_XOR); gtext(8, 32, "Wq"); grab();
    ok(count() == 64 * 32, "... and turned over again in G_XOR: the white block whole");
    gink(G_WHITE); glook(G_OPAQUE); gtext(8, 32, "Wq"); grab();
    ok(cell(8, 32, 'W', 0, 0) && cell(16, 32, 'q', 0, 0) && lit(7, 32) && lit(24, 40), "... opaque: its cells black but for the letters");
    glook(0);
    gclear(); gtext(8, 32, "a\nb\001"); grab();
    ok(cell(8, 32, 'a', 0, 0) && cell(8, 16, 'b', 0, 0) && cell(16, 16, '?', 0, 0), "... a new line goes down a cell; no such character is a ?");

    ok(gsin(0) == 0 && gsin(30) == 8192 && gsin(90) == G_ONE && gsin(-90) == -G_ONE && gsin(450) == G_ONE &&
       gcos(0) == G_ONE && gcos(60) == 8192 && gcos(180) == -G_ONE && gcos(-360) == G_ONE && gsin(45) == gcos(45), "gsin and gcos");
    ok(gsync() == 0, "gsync");
    gclose();
    ok(gfd() == -1 && gflush() == -1, "gclose");
    printf("gfx: %d checks, %d failed\n", checks, failed);
    return failed != 0;
}
