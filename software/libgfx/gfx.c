/* gfx.c -- libgfx (cc ... -lgfx; gfx(3)): drawing on the video card's screen. This file: the device, the ink,
 * dots, lines and rectangles. Circles are in gell.c, text in gtext.c.
 *
 * The card's chip (an HD63484) draws from commands, 16-bit words written to /dev/video (video(4)). The
 * library gathers them and writes a buffer at a time, so nothing shows until the buffer is full or gflush
 * is called.
 *
 * A line may have a style (gstyle) and a filled figure a pattern (gpattern). Both are the chip's pattern RAM, 16
 * rows of 16 bits: a drawing command takes a bit of a row for each dot. A line walks row 0, from its first dot;
 * a filled figure is drawn a line at a time with the pointer set for each, to the row and the bit of that place on
 * the screen: so a pattern lies the same way whatever is filled with it, and nothing rests on how the chip goes
 * from one line of a block to the next. Only the 1s are drawn (VM_ONES). Whatever has no style or pattern is
 * drawn with both colour registers the ink, so that it does not matter what the pattern RAM holds.
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
static int chainop;                     /* what that chain's lines carry (_gop, and VM_ONES with a style) */
static unsigned style = 0xFFFF;         /* the line style as the chip takes it: bit 0 the first dot's */
static unsigned short pat[16];          /* the fill pattern as the chip takes it: row 0 the top, bit 0 the left-hand dot */
static int patted;                      /* there is one */
static unsigned ram0 = 0xFFFF;          /* what row 0 of the chip's pattern RAM holds */

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
    if (chain) { _gw(VC_DOT | chainop); chain = 0; }
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

static const unsigned char rev[256] = { /* a byte's bits the other way round */
    0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0, 0x10, 0x90, 0x50, 0xD0, 0x30, 0xB0, 0x70, 0xF0,
    0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8, 0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8,
    0x04, 0x84, 0x44, 0xC4, 0x24, 0xA4, 0x64, 0xE4, 0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4,
    0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC, 0x1C, 0x9C, 0x5C, 0xDC, 0x3C, 0xBC, 0x7C, 0xFC,
    0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2, 0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2,
    0x0A, 0x8A, 0x4A, 0xCA, 0x2A, 0xAA, 0x6A, 0xEA, 0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA,
    0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6, 0x16, 0x96, 0x56, 0xD6, 0x36, 0xB6, 0x76, 0xF6,
    0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE, 0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE,
    0x01, 0x81, 0x41, 0xC1, 0x21, 0xA1, 0x61, 0xE1, 0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1,
    0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9, 0x19, 0x99, 0x59, 0xD9, 0x39, 0xB9, 0x79, 0xF9,
    0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5, 0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5,
    0x0D, 0x8D, 0x4D, 0xCD, 0x2D, 0xAD, 0x6D, 0xED, 0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD,
    0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3, 0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3,
    0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB, 0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB,
    0x07, 0x87, 0x47, 0xC7, 0x27, 0xA7, 0x67, 0xE7, 0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7,
    0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF, 0x1F, 0x9F, 0x5F, 0xDF, 0x3F, 0xBF, 0x7F, 0xFF
};

unsigned _grev(unsigned w) { return (unsigned)rev[w & 0xFF] << 8 | rev[w >> 8 & 0xFF]; }

static void row0(unsigned v)            /* row 0 of the pattern RAM: a line's style, or the fill pattern's own again */
{
    if (ram0 == v) return;
    _gw(VC_WPTN); _gw(1); _gw(v);
    ram0 = v;
}

void _gclip(int *x0, int *y0, int *x1, int *y1) { *x0 = kx0; *y0 = ky0; *x1 = kx1; *y1 = ky1; }

/* A block of a filled figure. With a pattern it is drawn a line at a time, from the top, the pattern's pointer
   set for each line to where that line begins on the screen. */
void _gfrun(int x0, int y0, int x1, int y1)
{
    int t, y;
    if (!patted) { _grun(x0, y0, x1, y1); return; }
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    if (x1 < kx0 || x0 > kx1 || y1 < ky0 || y0 > ky1) return;
    if (x0 < kx0) x0 = kx0;
    if (y0 < ky0) y0 = ky0;
    if (x1 > kx1) x1 = kx1;
    if (y1 > ky1) y1 = ky1;
    row0(pat[0]);
    for (y = y1; y >= y0; y--) {
        wpr(0x05, ((gheight - 1 - y) & 15) << 12 | (x0 & 15) << 4);
        _gw(VC_AMOVE); _gw(x0); _gw(y);
        if (x0 == x1) _gw(VC_DOT | _gop | VM_ONES);
        else { _gw(VC_AFRCT | _gop | VM_ONES); _gw(x1); _gw(y); }
    }
}

void gstyle(unsigned dashes)
{
    _gend();
    style = _grev(dashes & 0xFFFF);
}

void gpattern(const unsigned short *rows)
{
    int k;
    _gend();
    patted = 0;
    if (!rows) return;
    for (k = 0; k < 16; k++) { pat[k] = (unsigned short)_grev(rows[k]); if (pat[k] != 0xFFFF) patted = 1; }
    if (!patted) return;
    _gw(VC_WPTN); _gw(16);
    for (k = 0; k < 16; k++) _gw(pat[k]);
    ram0 = pat[0];
}

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
    style = ram0 = 0xFFFF;              /* (the pattern RAM is all 1s when a program gets the chip) */
    patted = 0;
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

/* The inks. The chip sets a dot it draws from one of its two colour registers, as the pattern has a 1 or a 0
   for it: both are the ink here, white, or 0 for G_BLACK. G_XOR is the chip's EOR with white. */
void gink(int ink)
{
    if (ink != G_BLACK && ink != G_XOR) ink = G_WHITE;
    if (ink == _gink) return;
    _gend();                            /* (the pen's last dot in the ink its line had) */
    if (_gink < 0 || (ink == G_BLACK) != (_gink == G_BLACK)) {
        wpr(0, ink == G_BLACK ? 0 : 0xFFFF);
        wpr(1, ink == G_BLACK ? 0 : 0xFFFF);
    }
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

int _gread(int y, int w, int n, unsigned short *buf)
{
    int k, m, r;
    _gend();
    rwp(w << 4, y);
    while (n > 0) {
        m = n < 8 ? n : 8;              /* (the chip's read FIFO holds 8 words: so many are asked for at a time) */
        for (k = 0; k < m; k++) _gw(VC_RD);
        send();
        for (k = 0; k < 2 * m; k += r) {
            r = read(_gfd, (char *)buf + k, 2 * m - k);
            if (r < 0 && errno == EINTR) { r = 0; continue; }
            if (r <= 0) { bad = 1; return -1; }
        }
        buf += m; n -= m;
    }
    return 0;
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
        chainop = _gop;
        if (style != 0xFFFF) { row0(style); wpr(0x05, 0); chainop |= VM_ONES; }     /* (the style from its first dot) */
    }
    _gw(VC_ALINE | chainop); _gw(x); _gw(y);
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
    if (x0 == x1 || y0 == y1) {         /* (no inside: the chip would go there and back) */
        if (style != 0xFFFF) gline(x0, y0, x1, y1); else _grun(x0, y0, x1, y1);
        return;
    }
    _gw(VC_AMOVE); _gw(x0); _gw(y0);
    if (style != 0xFFFF) {
        row0(style); wpr(0x05, 0);
        _gw(VC_ARCT | _gop | VM_ONES);
    } else _gw(VC_ARCT | _gop);
    _gw(x1); _gw(y1);
}

void gfbox(int x0, int y0, int x1, int y1)
{
    _gend();
    _gfrun(x0, y0, x1, y1);
}
