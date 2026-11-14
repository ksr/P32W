/* clock.c -- a clock on the video card's screen, drawn with the graphics library (man gfx).
 *     cc -o clock clock.c -lgfx
 *     ./clock            (it goes for 10 seconds;  ./clock 60  for a minute)
 * The face is drawn once. The hands are drawn in the ink G_XOR, which turns dots over: drawing the same
 * hands again takes them away and leaves the face as it was. When the program ends the console's text is
 * back on the screen. Only root and group staff may use the screen this way (/dev/video).
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <gfx.h>

static int CX, CY, R;                   /* the centre, and the face's radius: from the size of what is drawn on */

/* the dot d away from the centre in the direction a: degrees as a clock has them, 0 at twelve, clockwise */
static int px(int a, int d) { return CX + d * gsin(a) / G_ONE; }
static int py(int a, int d) { return CY + d * gcos(a) / G_ONE; }

static void face(void)
{
    char s[4];
    int k, in;
    gcircle(CX, CY, R + 8);
    gcircle(CX, CY, R + 5);
    for (k = 0; k < 60; k++) {                      /* a mark for each minute, a longer one for each hour */
        in = k % 5 ? R - 6 : R - 16;
        gline(px(6 * k, in), py(6 * k, in), px(6 * k, R), py(6 * k, R));
    }
    for (k = 1; k <= 12 && R >= 90; k++) {          /* the hours, each number centred on its place (on a face with room) */
        sprintf(s, "%d", k);
        gtext(px(30 * k, R - 34) - gcellw * (k > 9 ? 2 : 1) / 2, py(30 * k, R - 34) - gcellh / 2, s);
    }
    if (R >= 120) {
        glook(G_BOLD);
        gtext(CX - 2 * gcellw, CY + R * 6 / 19, "P32W");
        glook(0);
    }
    gfcircle(CX, CY, 4);
}

/* a hand: a thin four-cornered outline from behind the centre to its tip */
static void hand(int a, int len, int wide)
{
    int p[8];
    p[0] = px(a, len);       p[1] = py(a, len);
    p[2] = px(a + 90, wide); p[3] = py(a + 90, wide);
    p[4] = px(a + 180, 20);  p[5] = py(a + 180, 20);
    p[6] = px(a - 90, wide); p[7] = py(a - 90, wide);
    gpoly(p, 4);
}

static void hands(struct tm *t)
{
    hand(30 * (t->tm_hour % 12) + t->tm_min / 2, R * 55 / 100, 9);
    hand(6 * t->tm_min + t->tm_sec / 10, R * 85 / 100, 6);
    gline(px(6 * t->tm_sec + 180, 30), py(6 * t->tm_sec + 180, 30), px(6 * t->tm_sec, R - 20), py(6 * t->tm_sec, R - 20));
}

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 10;
    time_t now;
    struct tm t;
    char s[32];

    if (gopen() < 0) { perror("clock: /dev/video"); return 1; }
    R = ((gwidth < gheight ? gwidth : gheight) - 100) / 2;      /* (190 on the whole screen; less in a window of wm) */
    CX = gwidth / 2; CY = gheight / 2 + 15;
    face();
    while (n-- > 0) {
        now = time(0);
        t = *localtime(&now);
        strftime(s, sizeof s, "%a %b %e %H:%M:%S %Y", &t);
        gink(G_WHITE);
        glook(G_OPAQUE);                            /* (each character's whole cell: the old text is written over) */
        gtext(CX - 12 * gcellw, 16, s);
        glook(0);
        gink(G_XOR);
        hands(&t);
        gflush();                                   /* nothing shows until this: the library gathers what it sends */
        sleep(1);
        hands(&t);                                  /* the same hands again: gone */
    }
    gclose();                                       /* the console has its screen again */
    return 0;
}
