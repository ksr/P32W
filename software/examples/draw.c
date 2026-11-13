/* draw.c -- drawing on the video card's screen: /dev/video, video(4).
 *     cc -o draw draw.c
 *     ./draw            (the picture stays 5 seconds;  ./draw 20  for 20)
 * The video card's chip (an HD63484) draws from commands, 16-bit words
 * written to /dev/video. While this program has the device open the screen
 * is its own, 640 by 480 dots, x 0 y 0 the bottom-left one. When it closes
 * the device, or ends, the console's text is back as if nothing had been
 * drawn. Only root and group staff may open /dev/video. */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sgtty.h>
#include <sys/video.h>

static int fd;
static struct vidinfo vi;
static unsigned short q[256];           /* the words on their way: sent a buffer at a time */
static int nq;

static void flush(void)
{
    if (nq && write(fd, q, 2 * nq) != 2 * nq) { perror("/dev/video"); exit(1); }
    nq = 0;
}
static void w(int x)
{
    if (nq == 256) flush();
    q[nq++] = (unsigned short)x;
}

static void move(int x, int y) { w(VC_AMOVE); w(x); w(y); }
static void line(int x0, int y0, int x1, int y1)    /* (a line stops a dot short of its end: draw that one too) */
{
    move(x0, y0); w(VC_ALINE); w(x1); w(y1); w(VC_DOT);
}
static void box(int x0, int y0, int x1, int y1) { move(x0, y0); w(VC_ARCT); w(x1); w(y1); }
static void fill(int x0, int y0, int x1, int y1, int how) { move(x0, y0); w(VC_AFRCT | how); w(x1); w(y1); }

/* text: each character a copy from the font's picture under the screen. x, y is the first one's top-left dot;
   look: 0 plain, + 1 inverse, + 2 underlined, + 4 bold */
static void text(int x, int y, const char *s, int look)
{
    move(x, y);
    for (; *s; s++) {
        int c = *s < 0x20 || *s > 0x7E ? '?' : *s;
        w(VC_AGCPY);
        w((c & 31) * vi.cellw);
        w(vi.fonty - look * vi.fontstep - vi.cellh * ((c - 0x20) >> 5));
        w(vi.cellw - 1);
        w(-(vi.cellh - 1));
    }
}

int main(int argc, char **argv)
{
    int k, secs = argc > 1 ? atoi(argv[1]) : 5;

    fd = open("/dev/video", O_RDWR);
    if (fd < 0) { perror("/dev/video"); return 1; }
    ioctl(fd, VIOCINFO, &vi);

    /* the screen black: its memory set to 0, from the top line's first word down */
    w(VC_WPR | 0x0C); w(0x4000); w(VC_WPR | 0x0D); w(0);
    w(VC_CLR); w(0); w(vi.memw - 1); w(-(vi.height - 1));

    box(0, 0, vi.width - 1, vi.height - 1);
    text(16, vi.height - 16, "P32W", 4);
    text(16 + 5 * vi.cellw, vi.height - 16, "drawn by a program through /dev/video", 0);
    for (k = 0; k <= 16; k++)                       /* a fan of lines */
        line(40, 40, 40 + 20 * k, 360 - 20 * k);
    for (k = 0; k < 6; k++)                         /* boxes inside one another */
        box(420 + 12 * k, 60 + 12 * k, 600 - 12 * k, 240 - 12 * k);
    fill(420, 280, 600, 400, VO_REPLACE);           /* a white block, and a hole cut in it by drawing it again in XOR */
    fill(460, 310, 560, 370, VO_XOR);
    text(490, 348, " XOR ", 1);
    flush();
    ioctl(fd, VIOCSYNC, 0);                         /* wait until the chip has drawn it all */

    sleep(secs);
    close(fd);                                      /* the console has its screen again */
    return 0;
}
