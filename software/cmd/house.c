/* house -- a house of lines, turned and looked at from near and far (house(1), D148). P8X's house is its
 * graphics card's show: a house turned once, then left behind and come back to. This is that show with a house
 * of this program's own (four walls, a roof with a chimney, a door and two windows), drawn with the library
 * for three dimensions in lines (g3d(3)).
 *
 *   house [frames]
 *
 * One turn in 72 frames, then 24 going away and 24 coming back, and again, until q is typed or (with a number)
 * for that many frames. With a number it says at the end how long the frames took.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/times.h>
#include <g3d.h>

#define NL 96
static short was[NL][4], now[NL][4];
static int nwas, far0;
static volatile int stop;
static void sig(int n) { (void)n; stop = 1; }

static void rect(int x0, int y0, int x1, int y1, int z)        /* a rectangle on the front wall or the back */
{
    g3line(x0, y0, z, x1, y0, z); g3line(x1, y0, z, x1, y1, z);
    g3line(x1, y1, z, x0, y1, z); g3line(x0, y1, z, x0, y0, z);
}

static void build(void)                 /* the house, its middle where the numbers begin */
{
    g3clear();
    g3box(-70, -50, -45, 70, 20, 45);                   /* the walls */
    g3line(-70, 20, -45, -70, 60, 0); g3line(-70, 60, 0, -70, 20, 45);  /* the gables, */
    g3line(70, 20, -45, 70, 60, 0); g3line(70, 60, 0, 70, 20, 45);
    g3line(-70, 60, 0, 70, 60, 0);                      /* ... and the ridge between them */
    g3line(-12, -50, -45, -12, -8, -45); g3line(-12, -8, -45, 12, -8, -45);     /* the door (the wall's foot is */
    g3line(12, -8, -45, 12, -50, -45);                  /* its sill: a line twice over would be no line), */
    rect(-55, -25, -30, 0, -45); rect(30, -25, 55, 0, -45);    /* a window each side of it, */
    g3line(-42, -25, -45, -42, 0, -45); g3line(-55, -12, -45, -30, -12, -45);   /* with their bars */
    g3line(42, -25, -45, 42, 0, -45); g3line(30, -12, -45, 55, -12, -45);
    rect(-20, -20, 20, 5, 45);                          /* a window at the back */
    g3line(35, 44, -18, 35, 72, -18); g3line(50, 44, -18, 50, 72, -18);        /* the chimney */
    g3line(35, 53, -8, 35, 72, -8); g3line(50, 53, -8, 50, 72, -8);  /* (its feet where the roof is) */
    g3line(35, 72, -18, 50, 72, -18); g3line(50, 72, -18, 50, 72, -8);
    g3line(50, 72, -8, 35, 72, -8); g3line(35, 72, -8, 35, 72, -18);
}

static void frame(int f)                /* frame f of the show, in place of the last frame's */
{
    int k, n, g = f % 120, z = far0;
    if (g >= 72) z += (g < 96 ? g - 72 : 120 - g) * far0 / 12;         /* going away, and coming back */
    g3turn(-14, g < 72 ? 5 * g : 0, 0);                 /* (turned, and looked at from a little above) */
    g3put(0, 0, z);
    n = g3seen(now, NL);
    gink(G_XOR);
    for (k = 0; k < n || k < nwas; k++) {
        if (k < n && k < nwas && !memcmp(now[k], was[k], sizeof now[k])) continue;      /* (it has not moved) */
        if (k < nwas) gline(was[k][0], was[k][1], was[k][2], was[k][3]);
        if (k < n) gline(now[k][0], now[k][1], now[k][2], now[k][3]);
    }
    memcpy(was, now, sizeof was);
    nwas = n;
    gflush();
}

static void fit(void)
{
    int d = gwidth < gheight ? gwidth : gheight;
    g3view(0, 0, gwidth - 1, gheight - 1, d);
    far0 = 330;                         /* (the house is 100 from its middle at most: 100 * d / 230 dots) */
    nwas = 0;
    gclear();
}

int main(int argc, char **argv)
{
    struct tms tm;
    long t0;
    int frames = argc > 1 ? atoi(argv[1]) : 0, f, k;
    if (argc > 2 || (argc == 2 && frames < 1)) { fprintf(stderr, "usage: house [frames]\n"); return 2; }
    gsize(320, 280);
    if (gopen() < 0) { perror("house: the screen"); return 1; }
    gmaysize(96, 96, 16, 16);
    signal(SIGINT, sig); signal(SIGTERM, sig); signal(SIGHUP, sig);
    build();
    fit();
    t0 = times(&tm);
    for (f = 0; !stop && (!frames || f < frames); f++) {
        frame(f);
        k = ginput(0, 0, 0, frames ? 0 : 2);
        if (k == 'q' || k == 'Q' || k < 0) break;
        if (k == G_SIZE) fit();
    }
    gsync();
    t0 = times(&tm) - t0;
    gclose();
    if (frames && t0 > 0) printf("house: %d frames in %ld.%02ld s: %ld.%ld a second\n", f, t0 / 50, t0 % 50 * 2, f * 500L / t0 / 10, f * 500L / t0 % 10);
    return 0;
}
