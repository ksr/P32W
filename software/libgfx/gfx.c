/* gfx.c -- libgfx (cc ... -lgfx; gfx(3)): drawing on the video card's screen. This file: the device, the ink,
 * dots, lines and rectangles. Circles are in gell.c, text in gtext.c.
 *
 * The card's chip (an HD63484) draws from commands, 16-bit words written to /dev/video (video(4)). The
 * library gathers them and writes a buffer at a time, so nothing shows until the buffer is full or gflush
 * is called.
 *
 * Each dot of a figure is drawn once. The inks set a dot, clear it or turn it over (G_XOR); in G_XOR a dot
 * drawn twice would be as it was, so no line, box or circle here draws one twice, and a figure drawn again in
 * G_XOR is gone. (Two lines may still share dots, as where two sides of an outline run into a sharp corner.)
 *
 * What the chip is asked for: a dot (DOT), a line (ALINE, which stops one dot short of its end: that dot is
 * a DOT of its own), a rectangle's outline (ARCT), a block of dots (AFRCT), each with the area mode that
 * draws only inside the clipping rectangle (registers 8 to 0x0B): the chip clips. Blocks are cut to that
 * rectangle here as well, which saves the chip's time and the words of those that are wholly outside.
 * Nothing here rests on which dots the chip picks for a slanted line except gdraw, gline and gpoly themselves.
 */
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sgtty.h>
#include <gfx.h>
#include "gfxint.h"

#define NQ 256

int gwidth, gheight, gcellw, gcellh;
int _gfd = -1;
int _gink;
int _gop;
int _glook;
struct vidinfo _gvi;

static unsigned short q[NQ];            /* the words on their way */
static int nq;
static int bad;                         /* a write failed */
static int penx, peny;                  /* the pen */
static int chain;                       /* the chip has drawn a line up to the pen: the dot there is still to draw */
static int cx0, cy0, gone;              /* ... where that chain of lines began, and whether it has left there */
static int kx0, ky0, kx1, ky1;          /* the clipping rectangle, as the chip has it */

static void send(void)
{
    char *p = (char *)q;
    int n = 2 * nq, k;
    nq = 0;
    while (n > 0) {
        k = write(_gfd, p, n);
        if (k <= 0) {
            if (k < 0 && errno == EINTR) continue;      /* (a signal that was caught: the rest again) */
            bad = 1;
            return;
        }
        p += k; n -= k;
    }
}

void _gw(int w)
{
    if (nq == NQ) send();
    q[nq++] = (unsigned short)w;
}

void _gend(void)
{
    if (chain) { _gw(VC_DOT | _gop); chain = 0; }
}

/* A block is cut to the clipping rectangle here, and not sent at all if it is wholly outside: the chip would
   draw the same dots, but only after stepping over every one of the others, and a figure far off the screen
   would be thousands of words for nothing. */
void _grun(int x0, int y0, int x1, int y1)
{
    int t;
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    if (x1 < kx0 || x0 > kx1 || y1 < ky0 || y0 > ky1) return;
    if (x0 < kx0) x0 = kx0;
    if (y0 < ky0) y0 = ky0;
    if (x1 > kx1) x1 = kx1;
    if (y1 > ky1) y1 = ky1;
    _gw(VC_AMOVE); _gw(x0); _gw(y0);
    if (x0 == x1 && y0 == y1) _gw(VC_DOT | _gop);
    else { _gw(VC_AFRCT | _gop); _gw(x1); _gw(y1); }
}

static void wpr(int r, int v) { _gw(VC_WPR | r); _gw(v); }

int gopen(void)
{
    if (_gfd >= 0) return 0;
    _gfd = open("/dev/video", O_RDWR);
    if (_gfd < 0) return -1;
    if (ioctl(_gfd, VIOCINFO, &_gvi) < 0) { close(_gfd); _gfd = -1; return -1; }
    gwidth = _gvi.width; gheight = _gvi.height;
    gcellw = _gvi.cellw; gcellh = _gvi.cellh;
    nq = bad = chain = 0;
    penx = peny = 0;
    _gink = -1;
    gink(G_WHITE);
    glook(0);
    gclip(0, 0, gwidth - 1, gheight - 1);
    gclear();
    return 0;
}

void gclose(void)
{
    if (_gfd < 0) return;
    close(_gfd);                        /* (what is still on its way is not wanted: the console draws its text again) */
    _gfd = -1;
    nq = chain = 0;
}

int gflush(void)
{
    int r;
    if (_gfd < 0) return -1;
    _gend();
    if (nq) send();
    r = bad ? -1 : 0;
    bad = 0;
    return r;
}

int gsync(void)
{
    int r = gflush();
    while (ioctl(_gfd, VIOCSYNC, 0) < 0)
        if (errno != EINTR) return -1;
    return r;
}

int gfd(void) { return _gfd; }

void glook(int look) { _glook = look; }

void gwords(const unsigned short *w, int n)
{
    _gend();
    while (n-- > 0) _gw(*w++);
}

/* The inks. The chip sets a dot it draws from its colour register 1 (the pattern RAM is all ones, as a
   program finds it): white, or 0 for G_BLACK. G_XOR is the chip's EOR with white. */
void gink(int ink)
{
    if (ink != G_BLACK && ink != G_XOR) ink = G_WHITE;
    if (ink == _gink) return;
    _gend();                            /* (the pen's last dot in the ink its line had) */
    if (_gink < 0 || (ink == G_BLACK) != (_gink == G_BLACK)) wpr(1, ink == G_BLACK ? 0 : 0xFFFF);
    _gink = ink;
    _gop = VA_INSIDE | (ink == G_XOR ? VO_XOR : VO_REPLACE);
}

void gclip(int x0, int y0, int x1, int y1)
{
    int t;
    _gend();
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    if (x0 < 0) x0 = 0;                 /* (never past the screen: under it are the font's pictures, and a line */
    if (y0 < 0) y0 = 0;                 /* that leaves it sideways would come back on the next one) */
    if (x1 > gwidth - 1) x1 = gwidth - 1;
    if (y1 > gheight - 1) y1 = gheight - 1;
    kx0 = x0; ky0 = y0; kx1 = x1; ky1 = y1;
    wpr(0x08, x0); wpr(0x09, y0); wpr(0x0A, x1); wpr(0x0B, y1);
}

/* the read / write pointer at the word that has the dot x, y: line n from the top starts at word n * memw */
static void rwp(int x, int y)
{
    unsigned a = (unsigned)(gheight - 1 - y) * _gvi.memw + (x >> 4);
    wpr(0x0C, 0x4000 | a >> 12); wpr(0x0D, (a << 4) & 0xFFFF);
}

void gclear(void)
{
    _gend();
    rwp(0, gheight - 1);
    _gw(VC_CLR); _gw(0); _gw(_gvi.memw - 1); _gw(-(gheight - 1));
}

int gget(int x, int y)
{
    unsigned short w;
    int k;
    if (_gfd < 0 || x < 0 || y < 0 || x >= gwidth || y >= gheight) return 0;
    _gend();
    rwp(x, y);
    _gw(VC_RD);
    send();
    while ((k = read(_gfd, &w, 2)) < 0 && errno == EINTR)
        ;
    if (k != 2) { bad = 1; return 0; }
    return w >> (x & 15) & 1;           /* (a word's bit 0 is its left-hand dot) */
}

void gpoint(int x, int y)
{
    _gend();
    _grun(x, y, x, y);
}

void gmove(int x, int y)
{
    _gend();
    penx = x; peny = y;
}

/* A line from the pen. The chip's line stops a dot short of its end, where the next one begins: so lines
   drawn one after another share no dot, and the last dot of the last one is drawn (_gend) when anything
   else is asked for. A chain that comes back to where it began is closed: that dot was its first. */
void gdraw(int x, int y)
{
    if (!chain) {
        _gw(VC_AMOVE); _gw(penx); _gw(peny);
        cx0 = penx; cy0 = peny; gone = 0;
    }
    _gw(VC_ALINE | _gop); _gw(x); _gw(y);
    if (x != penx || y != peny) gone = 1;
    penx = x; peny = y;
    chain = !(gone && x == cx0 && y == cy0);
}

void gline(int x0, int y0, int x1, int y1)
{
    gmove(x0, y0);
    gdraw(x1, y1);
    _gend();
}

void gpoly(const int *xy, int n)
{
    int k;
    if (n <= 0) return;
    gmove(xy[0], xy[1]);
    for (k = 1; k < n; k++) gdraw(xy[2 * k], xy[2 * k + 1]);
    gdraw(xy[0], xy[1]);
    _gend();
}

void gbox(int x0, int y0, int x1, int y1)
{
    _gend();
    if (x0 == x1 || y0 == y1) { _grun(x0, y0, x1, y1); return; }   /* (no inside: the chip would go there and back) */
    _gw(VC_AMOVE); _gw(x0); _gw(y0);
    _gw(VC_ARCT | _gop); _gw(x1); _gw(y1);
}

void gfbox(int x0, int y0, int x1, int y1)
{
    _gend();
    _grun(x0, y0, x1, y1);
}
