/* wmgfx_test.c -- a program's picture in a window of wm (D123), on the machine the system is built on. The
   graphics library is here twice over, as it is on P32W in two programs: once as the program in a window has
   it, where every call becomes a request written to the program's terminal (libgfx/gwin.c), and once as wm has
   it, drawing. What the first writes is put through wm's terminal (wmterm.c), which hands the requests to
   wmgfx.c; the answers come back as if typed. Each figure asked for this way must be, dot for dot, the figure
   the same calls draw straight into a window of the same size.
   It prints "wmgfx_test: N checks", or what failed. */
#include <stdint.h>
#include <gfx.h>
#include "wmwin.h"
#include "wmterm.h"
#include "wmgfx.h"
#include "../../software/libgfx/gfxint.h"

static int checks, fails, verbose;
static void ok(int c, const char *what) { checks++; if (!c) { fails++; printf("FAIL: %s\n", what); } else if (verbose) printf("ok: %s\n", what); }
static struct term term;
static struct gx gx;
void trequest(struct term *t, char *req) { (void)t; gx_request(&gx, req); }

/* the program's side: its calls go out as requests, and wm does them at once (as its turn comes on P32W) */
static void wire(const char *s, int n)
{
    int was = _gwm;
    _gwm = 0;                           /* (wm's own library draws) */
    tput(&term, (const unsigned char *)s, n);
    gx_show(&gx); gx_done();
    _gwm = was;
}
#define ASKED(...) do { _gwm = 1; __VA_ARGS__; gflush(); _gwm = 0; } while (0)

static unsigned short pa[300 * 25], pb[300 * 25];
static int same(int ref)                /* the picture's window and the window drawn straight into: the same dots? */
{
    wdraw(gx.win); gread(0, 0, 399, 299, pa);
    wdraw(ref); gread(0, 0, 399, 299, pb);
    return !memcmp(pa, pb, sizeof pa);
}

static const unsigned short pat[16] = { 0x8001, 0x4002, 0x2004, 0x1008, 0x0810, 0x0420, 0x0240, 0x0180,
                                        0x0180, 0x0240, 0x0420, 0x0810, 0x1008, 0x2004, 0x4002, 0xFFFF };
static const int star[] = { 200, 280, 150, 20, 380, 180, 20, 180, 250, 20 };

static void figures(void)               /* a picture that uses every kind of call */
{
    gclear();
    gline(0, 0, 399, 299); gbox(10, 10, 100, 60); gfbox(120, 10, 180, 40);
    gcircle(300, 200, 60); gfcircle(300, 200, 20); gellipse(100, 200, 80, 30); gfellipse(100, 200, 30, 10);
    garc(200, 150, 100, 60, 30, 200); gpoly(star, 5);
    gmove(5, 290); gdraw(50, 250); gdraw(90, 290); gdraw(5, 290);
    gpoint(395, 5);
    gstyle(0xF0F0); gline(0, 150, 399, 150); gbox(240, 20, 380, 100); gstyle(0xFFFF);
    gpattern(pat); gfbox(250, 30, 370, 90); gfpoly(star, 5); gpattern(0);
    gtext(16, 100, "Text in a window"); glook(G_BOLD | G_UNDER); gtext(16, 120, "bold"); glook(0);
    gink(G_BLACK); gfbox(130, 15, 170, 35); gink(G_XOR); gfbox(0, 0, 60, 60); gink(G_WHITE);
    gclip(300, 0, 399, 60); gfcircle(350, 30, 50); gclip(0, 0, 399, 299);
    gcopy(10, 10, 100, 60, 150, 220);
    gtext(200, 270, "two\nlines");
}

int main(int argc, char **argv)
{
    int ref, k, x, y, b, r;
    (void)argv; verbose = argc > 1;
    ok(wstart() == 0, "wstart");
    dev_tty = wire;                     /* (after wm has the screen: wm itself is in no window) */
    setenv("WM", "1", 1);
    tinit(&term, wopen(8, 8, 164, 68, "t"), 20, 4);
    memset(&gx, 0, sizeof gx);
    gx.fd = 20;                         /* (the terminal's master, where wm writes its answers: gfxhost/dev.c) */
    ref = wopen(220, 100, 400, 300, "straight");
    /* the program opens its picture: the library in it is not wm's, so it is turned to the window's way by hand */
    ok(_gwopen() == 1 && gwidth == 400 && gheight == 300, "gopen in a window: a picture of 400 by 300");
    _gwm = 0; gwidth = 640; gheight = 480;
    ASKED((void)0);
    ok(gx.win > 0 && wplace(gx.win, 0, 0, &x, &y) == 0 && x == 400 && y == 300, "wm has opened a window for it");
    ASKED(figures());
    wdraw(ref); figures(); wshow(ref);
    ok(same(ref), "every kind of figure, asked of wm: the dots that the same calls draw");
    ASKED(gink(G_XOR); gcircle(200, 150, 100); gink(G_WHITE));
    wdraw(ref); gink(G_XOR); gcircle(200, 150, 100); gink(G_WHITE);
    ok(same(ref), "the ink a program set holds from one lot of requests to the next");
    _gwm = 1; k = gget(0, 0); r = gget(399, 0); _gwm = 0;
    wdraw(ref); x = gget(0, 0); y = gget(399, 0);
    ok(k == x && r == y, "gget is answered");
    _gwm = 1; gclear(); gcircle(200, 150, 90); r = gfill(200, 150); gflush(); _gwm = 0;
    wdraw(ref); gclear(); gcircle(200, 150, 90); gfill(200, 150);
    ok(r == 0 && same(ref), "gfill is done, and answered");
    _gwm = 1; k = gkeep(100, 60, 300, 240); gclear(); gstamp(k, 0, 0, G_COPY); gstamp(k, 150, 100, G_FLIP); gdrop(k); gstamp(k, 20, 20, G_COPY); gflush(); _gwm = 0;
    wdraw(ref); r = gkeep(100, 60, 300, 240); gclear(); gstamp(r, 0, 0, G_COPY); gstamp(r, 150, 100, G_FLIP); gdrop(r);
    ok(k >= 0 && same(ref), "a picture kept and put down; a dropped one is not");
    _gwm = 1; gstamp(0, 5, 5, G_COPY); gstamp(1, 5, 5, G_COPY); gflush(); _gwm = 0;
    ok(same(ref), "a kept picture that is not the program's own is not put down");
    _gwm = 1; r = gsync(); _gwm = 0;
    ok(r == 0, "gsync is answered");
    gx_mouse(&gx, 123, 45, 1);
    _gwm = 1; gmopen(); r = gmouse(&x, &y, &b, 0); k = gmouse(0, 0, 0, 0); _gwm = 0;
    ok(r == 1 && x == 123 && y == 45 && b == 1 && k == 0, "gmouse: where the mouse is in the window, and that it is news once");
    /* gwrite: a picture's dots handed over, in as many requests as its lines need */
    {
        static unsigned short pic[60 * 25];
        for (k = 0; k < 60 * 25; k++) pic[k] = (unsigned short)(k * 2654435761u >> 13);
        ASKED(gclear(); gwrite(37, 50, 136, 89, pic, G_COPY); gwrite(0, 200, 399, 259, pic, G_COPY); gwrite(30, 60, 300, 100, pic, G_FLIP));
        wdraw(ref); gclear(); gwrite(37, 50, 136, 89, pic, G_COPY); gwrite(0, 200, 399, 259, pic, G_COPY); gwrite(30, 60, 300, 100, pic, G_FLIP);
        ok(same(ref), "gwrite: the dots written straight, whatever the picture's width, place and way");
    }
    /* ginput: the keys typed at the terminal and the mouse, whichever is first; nothing left on its way after */
    {
        char c;
        dev_ttyin("a", 1);
        _gwm = 1; k = ginput(&x, &y, &b, -1); _gwm = 0;
        ok(k == 'a' && gx.mwait == 0 && read(9, &c, 1) == 0, "ginput: a key typed; wm waits for the mouse no longer, and nothing is on its way");
        gx_mouse(&gx, 50, 60, 1);
        _gwm = 1; k = ginput(&x, &y, &b, -1); _gwm = 0;
        ok(k == G_MOUSE && x == 50 && y == 60 && b == 1, "ginput: the mouse");
        dev_ttyin("z", 1); gx_mouse(&gx, 70, 80, 0);
        _gwm = 1; k = ginput(&x, &y, &b, -1); r = ginput(&x, &y, &b, -1); _gwm = 0;
        ok(k == 'z' && r == G_MOUSE && x == 70 && y == 80 && b == 0 && read(9, &c, 1) == 0, "... a key, and the mouse's news that came with it is the next");
        dev_ttyin("\033[A\033", 4);
        _gwm = 1; k = ginput(0, 0, 0, -1); r = ginput(0, 0, 0, -1); x = ginput(0, 0, 0, -1); y = ginput(0, 0, 0, -1); _gwm = 0;
        ok(k == 27 && r == '[' && x == 'A' && y == 27 && read(9, &c, 1) == 0, "... ESC is a key when no answer follows it: an arrow's three characters, and ESC alone");
        dev_ttyin("k", 1);
        _gwm = 1; r = gget(0, 0); k = ginput(0, 0, 0, -1); _gwm = 0;
        ok(k == 'k', "a key typed while an answer was waited for is kept for ginput");
        _gwm = 1; gmclose(); k = ginput(0, 0, 0, 5); dev_ttyin("q", 1); r = ginput(0, 0, 0, 5); gmopen(); _gwm = 0;
        ok(k == 0 && r == 'q', "without the mouse: the keys alone, and nothing when the time is up");
    }
    ASKED(gclose());
    ok(gx.win == 0, "gclose: the window is gone");
    gsize(320, 200);
    ok(_gwopen() == 1 && gwidth == 320 && gheight == 200, "gsize: the picture a program asks for");
    _gwm = 0; gwidth = 640; gheight = 480;
    ASKED((void)0);
    ok(gx.win > 0 && wplace(gx.win, 0, 0, &x, &y) == 0 && x == 320 && y == 200, "... and wm's window for it");
    ASKED(gclose());
    ASKED(gline(0, 0, 10, 10));
    _gwm = 1; r = gget(0, 0); _gwm = 0;
    ok(r == 0 && gx.win == 0, "with no picture, a call is nothing and an answer still comes");
    ok(!dev_bad(), "the chip had no command it does not know and lost no word");
    wend();
    if (fails) printf("%d of %d checks failed\n", fails, checks); else printf("wmgfx_test: %d checks\n", checks);
    return fails != 0;
}
