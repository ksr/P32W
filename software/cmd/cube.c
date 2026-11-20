/* cube -- a cube of twelve lines, turning (cube(1), D148). It is P8X's cube brought here: the first program of
 * the library for three dimensions in lines (g3d(3)), and the measure of it. Under the window manager it has a
 * window of its own; with none it takes the whole of the video card's screen.
 *
 *   cube [frames]
 *
 * It turns five degrees about its upright axis in every frame, and is tipped half as fast about the one from
 * side to side, until q is typed or (with a number) for that many frames: 72 are one whole turn. Each frame the lines
 * are taken away where they were and drawn where they are, in the ink that turns dots over. With a number it
 * says at the end how long the frames took.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/times.h>
#include <g3d.h>

static short was[16][4], now[16][4];
static int nwas;
static volatile int stop;
static void sig(int n) { (void)n; stop = 1; }

static void frame(int f)                /* the cube as it is in frame f, in place of the last frame's */
{
    int k, n;
    g3turn(5 * f / 2, 5 * f, 0);
    n = g3seen(now, 16);
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

static void fit(void)                   /* the view: the whole picture, the cube filling two thirds of it */
{
    int d = gwidth < gheight ? gwidth : gheight;
    g3view(0, 0, gwidth - 1, gheight - 1, d);
    g3put(0, 0, 4 * 90 * 3 / 2);       /* (its corners are 156 from its middle: 156 * d / (540 - 156) dots at most) */
    nwas = 0;
    gclear();
}

int main(int argc, char **argv)
{
    struct tms tm;
    long t0;
    int frames = argc > 1 ? atoi(argv[1]) : 0, f, k;
    if (argc > 2 || (argc == 2 && frames < 1)) { fprintf(stderr, "usage: cube [frames]\n"); return 2; }
    gsize(240, 240);
    if (gopen() < 0) { perror("cube: the screen"); return 1; }
    gmaysize(64, 64, 16, 16);
    signal(SIGINT, sig); signal(SIGTERM, sig); signal(SIGHUP, sig);
    g3clear();
    g3box(-90, -90, -90, 90, 90, 90);
    fit();
    t0 = times(&tm);
    for (f = 0; !stop && (!frames || f < frames); f++) {
        frame(f);
        k = ginput(0, 0, 0, frames ? 0 : 2);            /* (a 25th of a second between frames, unless they are counted) */
        if (k == 'q' || k == 'Q' || k < 0) break;
        if (k == G_SIZE) fit();
    }
    gsync();
    t0 = times(&tm) - t0;
    gclose();
    if (frames && t0 > 0) printf("cube: %d frames in %ld.%02ld s: %ld.%ld a second\n", f, t0 / 50, t0 % 50 * 2, f * 500L / t0 / 10, f * 500L / t0 % 10);
    return 0;
}
