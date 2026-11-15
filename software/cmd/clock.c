/* clock -- a clock with hands: in a window of the window manager, or on the whole of the video card's
 * screen (clock(1), D125).
 *
 *   clock [seconds]
 *
 * The face is drawn once, as big as there is room for; the day and the time are written under it where a
 * line of text fits. The hands are drawn in the ink that turns dots over (G_XOR), so drawing the same hands
 * again takes them away and leaves the face as it was. It goes until q is typed, or its window's close box
 * is clicked, or for the number of seconds given. Its window may be made bigger or smaller: the face is
 * drawn again to fit.
 *
 * It is a program like any other that draws with the graphics library (gfx(3)): under wm the library has
 * the window manager open a window for it; with no wm it has the screen itself. /usr/examples/clock.c is
 * the same thing at its shortest.
 */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <gfx.h>

static int CX, CY, R;                   /* the centre, and the face's radius */
static int room, wide;                  /* lines of dots under the face for a line of text; the day in it as well */
static volatile int stop;

static void sig(int n) { (void)n; stop = 1; }

/* the dot d away from the centre in the direction a: degrees as a clock has them, 0 at twelve, clockwise */
static int px(int a, int d) { return CX + d * gsin(a) / G_ONE; }
static int py(int a, int d) { return CY + d * gcos(a) / G_ONE; }

static void face(void)
{
    char s[4];
    int k, in, mark = R / 20 < 2 ? 2 : R / 20, hour = R / 7 < 4 ? 4 : R / 7;
    gcircle(CX, CY, R);
    if (R >= 60) gcircle(CX, CY, R - 3);
    for (k = 0; k < 60; k++) {          /* a mark for each minute (on a face with room), a longer one for each hour */
        if (k % 5 && R < 60) continue;
        in = R - 6 - (k % 5 ? mark : hour);
        gline(px(6 * k, in), py(6 * k, in), px(6 * k, R - 6), py(6 * k, R - 6));
    }
    for (k = 1; k <= 12 && R >= 90; k++) {              /* the hours, each number centred on its place */
        sprintf(s, "%d", k);
        gtext(px(30 * k, R - hour - 20) - gcellw * (k > 9 ? 2 : 1) / 2, py(30 * k, R - hour - 20) - gcellh / 2, s);
    }
    gfcircle(CX, CY, R >= 60 ? 3 : 2);
}

/* a hand: a thin four-cornered outline from behind the centre to its tip */
static void hand(int a, int len, int wide)
{
    int p[8];
    p[0] = px(a, len);          p[1] = py(a, len);
    p[2] = px(a + 90, wide);    p[3] = py(a + 90, wide);
    p[4] = px(a + 180, R / 8);  p[5] = py(a + 180, R / 8);
    p[6] = px(a - 90, wide);    p[7] = py(a - 90, wide);
    gpoly(p, 4);
}

static void hands(const struct tm *t)
{
    hand(30 * (t->tm_hour % 12) + t->tm_min / 2, R * 50 / 100, R / 16 < 2 ? 2 : R / 16);
    hand(6 * t->tm_min + t->tm_sec / 10, R * 78 / 100, R / 24 < 2 ? 2 : R / 24);
    gline(px(6 * t->tm_sec + 180, R / 6), py(6 * t->tm_sec + 180, R / 6), px(6 * t->tm_sec, R * 82 / 100), py(6 * t->tm_sec, R * 82 / 100));
}

static void fit(void)                   /* the face, as big as what there is to draw on has room for */
{
    room = gheight >= 100 && gwidth >= 8 * gcellw ? gcellh + 4 : 0;     /* a line of text under the face, if it fits */
    wide = gwidth >= 19 * gcellw;       /* ... the day as well as the time */
    R = (gwidth < gheight - room ? gwidth : gheight - room) / 2 - 3;
    if (R > 200) R = 200;
    CX = gwidth / 2; CY = room + (gheight - room) / 2;
    gink(G_WHITE);
    face();
}

int main(int argc, char **argv)
{
    int left = argc > 1 ? atoi(argv[1]) : -1, k, sized;
    time_t now;
    struct tm t;
    char s[32];

    if (argc > 2 || (argc > 1 && left <= 0)) { fprintf(stderr, "usage: clock [seconds]\n"); return 2; }
    gsize(200, 220);                    /* (the window to ask for, under wm) */
    if (gopen() < 0) { perror("clock: /dev/video"); return 1; }
    signal(SIGINT, sig); signal(SIGTERM, sig); signal(SIGHUP, sig);
    gmaysize(48, 48, 4, 4);             /* (under wm: the window's corner makes the clock bigger or smaller) */
    fit();
    while (!stop && left) {
        now = time(0);
        t = *localtime(&now);
        if (room) {
            strftime(s, sizeof s, wide ? "%a %b %e %H:%M:%S" : "%H:%M:%S", &t);
            gink(G_WHITE);
            glook(G_OPAQUE);            /* (each character's whole cell: the old text is written over) */
            gtext(CX - (wide ? 19 : 8) * gcellw / 2, 2, s);
            glook(0);
        }
        gink(G_XOR);
        hands(&t);
        gflush();
        sized = 0;
        while (!stop && !sized && time(0) == now) {     /* until the second is over: a key is looked for meanwhile */
            k = ginput(0, 0, 0, 10);
            if (k == 'q' || k == 'Q' || k < 0) stop = 1;
            if (k == G_SIZE) { fit(); sized = 1; }      /* another size: a new picture, and nothing in it to take away */
        }
        if (sized) continue;
        hands(&t);                      /* the same hands again: gone */
        if (left > 0) left--;
    }
    gclose();
    return 0;
}
