/* win_test.c -- wm's windows (software/cmd/wmwin.c, D122) against the emulators' model of the video card's
   chip, on the machine the system is built on: wm's own source with /dev/video, /dev/mouse and
   the keyboard replaced (gfxhost/), a hand that moves the mouse and presses its button, and the screen looked
   at in the model's memory. After every change the screen must be what a whole repaint makes of it: what shows
   of each window is worked out twice that way, once for the part that changed and once for all of it.
   It prints "win_test: N checks", or what failed;  ./win_test -v  names each check as it passes. */
#include <stdint.h>
#include <gfx.h>
#include "wmwin.h"

#define MW 40
static const uint16_t *mem;
static int lit(int x, int y) { return mem[(479 - y) * MW + (x >> 4)] >> (x & 15) & 1; }
static int area(int x0, int y0, int x1, int y1) { int n = 0, x, y; for (y = y0; y <= y1; y++) for (x = x0; x <= x1; x++) n += lit(x, y); return n; }
static uint16_t snap[480 * MW];
static int checks, fails, verbose;
static void ok(int c, const char *what) { checks++; if (!c) { fails++; printf("FAIL: %s\n", what); } else if (verbose) printf("ok: %s\n", what); }
#define OK(c, ...) do { char _b[200]; snprintf(_b, sizeof _b, __VA_ARGS__); ok(c, _b); } while (0)
#define GREY(x, y) ((((479 - (y)) & 3) == 0 ? 0x8888 : ((479 - (y)) & 3) == 2 ? 0x2222 : 0) >> (15 - ((x) & 15)) & 1)

/* the hand: where it has the pointer, and whether the left button is held */
static int hx = 320, hy = 240, hb;
static struct wevent ev[32];
static int nev;
static void run(void) { struct wevent e; while (nev < 32 && wevent(&e, 0)) ev[nev++] = e; }
static void go(int x, int y)
{
    while (hx != x || hy != y) {
        int dx = x - hx, dy = y - hy;
        if (dx > 100) dx = 100; if (dx < -100) dx = -100;
        if (dy > 100) dy = 100; if (dy < -100) dy = -100;
        dev_mouse(8 | hb | (dx < 0 ? 0x10 : 0) | (dy < 0 ? 0x20 : 0), dx & 0xFF, dy & 0xFF);
        hx += dx; hy += dy;
    }
    run();
}
static void press(void) { hb = 1; dev_mouse(9, 0, 0); run(); }
static void release(void) { hb = 0; dev_mouse(8, 0, 0); run(); }

/* the screen as it is must be the screen made again from nothing (the pointer off for both) */
static int whole(void)
{
    int same;
    wpointer(0);
    gflush();
    memcpy(snap, mem, sizeof snap);
    wrepaint();
    gflush();
    same = !memcmp(snap, mem, sizeof snap);
    wpointer(1);
    return same;
}

int main(int argc, char **argv)
{
    int w1, w2, w3, b1, m0, m1, x, y, n, k, bad, wx, wy, ww, wh;
    struct wevent e;
    (void)argv;
    verbose = argc > 1;
    mem = dev_mem();
    OK(wstart() == 0, "wstart");
    wpointer(0); gflush();
    for (bad = 0, y = 0; y < 462; y++) for (x = 0; x < 640; x++) if (lit(x, y) != GREY(x, y)) bad = 1;
    OK(!bad && area(0, 463, 639, 479) == 640 * 17 && area(0, 462, 639, 462) == 0, "the desk is grey, the bar at the top white with a line under it");
    memcpy(snap, mem, sizeof snap);
    wpointer(1); gflush();
    for (n = 0, bad = 0, y = 0; y < 480; y++) for (x = 0; x < 640; x++)
        if (lit(x, y) != (snap[(479 - y) * MW + (x >> 4)] >> (x & 15) & 1)) { n++; if (x < 320 || x > 331 || y > 240 || y < 225) bad = 1; }
    OK(n > 30 && !bad, "the pointer: an arrow turned over on the desk, its tip where the mouse is (%d dots)", n);

    w1 = wopen(100, 100, 200, 120, "One");
    wpointer(0); gflush();
    OK(w1 == 1 && area(100, 100, 299, 219) == 0, "wopen: the inside is black");
    OK(area(99, 99, 99, 238) == 140 && area(300, 99, 300, 238) == 140 && area(99, 99, 300, 99) == 202 && area(99, 238, 300, 238) == 202 && area(100, 220, 299, 220) == 200,
       "... a frame round it and a line under its bar");
    OK(lit(290, 229) && lit(120, 229) && !lit(103, 224) && area(100, 221, 299, 237) < 200 * 17, "... the front window's bar white, with a close box and its name in black");
    wpointer(1);
    wdraw(w1); gfbox(0, 0, 199, 119); gflush();
    OK(gwidth == 200 && gheight == 120, "wdraw: the window's inside is what is drawn into");
    wpointer(0); gflush(); OK(area(100, 100, 299, 219) == 0, "... and nothing shows before wshow"); wpointer(1);
    wshow(w1);
    wpointer(0); gflush(); OK(area(100, 100, 299, 219) == 200 * 120, "wshow: the window's picture on the screen"); wpointer(1);

    w2 = wopen(200, 150, 200, 120, "Two");
    wpointer(0); gflush();
    OK(w2 == 2 && area(200, 150, 399, 269) == 0 && lit(150, 150) && !lit(250, 180), "a second window over the first: black inside, the first white where it shows");
    OK(!lit(150, 229) && lit(390, 279), "... the first one's bar black now, the second's white");
    wpointer(1);
    OK(whole(), "the screen is what a whole repaint makes");
    wfront(w1);
    wpointer(0); gflush(); OK(lit(250, 180) && lit(150, 229) && !lit(390, 279), "wfront: the first over the second"); wpointer(1);
    OK(whole(), "... and the screen is whole");
    wmove(w1, 50, 60);
    wpointer(0); gflush();
    OK(lit(60, 70) && !lit(290, 200) && lit(280, 110) == GREY(280, 110) && lit(260, 100) == GREY(260, 100) && lit(281, 111) == GREY(281, 111),
       "wmove: the window there, the second and the desk where it was");
    wpointer(1);
    OK(whole() && wplace(w1, &wx, &wy, &ww, &wh) == 0 && wx == 50 && wy == 60 && ww == 200 && wh == 120, "... the screen whole; wplace");

    /* the mouse */
    go(350, 250); nev = 0; press();
    OK(nev == 1 && ev[0].type == W_DOWN && ev[0].win == w2 && ev[0].x == 150 && ev[0].y == 100, "the button down in a window behind: W_DOWN with the dot inside it");
    wpointer(0); gflush(); OK(!lit(230, 170) && lit(390, 279), "... and the window comes to the front"); wpointer(1);
    nev = 0; go(360, 255);
    OK(nev == 1 && ev[0].type == W_DRAG && ev[0].win == w2 && ev[0].x == 160 && ev[0].y == 105, "the pointer moved with the button down: W_DRAG");
    nev = 0; release();
    OK(nev == 1 && ev[0].type == W_UP && ev[0].win == w2 && ev[0].x == 160 && ev[0].y == 105, "the button up: W_UP");
    nev = 0; go(370, 260);
    OK(nev == 0, "the pointer moved with the button up: nothing");
    go(360, 255);
    OK(whole(), "the screen is whole");

    b1 = wbutton(w2, 20, 20, 80, 24, "Push");
    wpointer(0); gflush(); OK(b1 >= 0 && area(220, 170, 299, 193) > 2 * 80 + 2 * 22 && !lit(222, 172), "wbutton: a box with its name"); wpointer(1);
    go(230, 180); nev = 0; press();
    wpointer(0); gflush(); OK(nev == 0 && lit(222, 172), "a button held down: white"); wpointer(1);
    go(500, 400);
    wpointer(0); gflush(); OK(!lit(222, 172), "... the pointer off it: as it was"); wpointer(1);
    go(232, 182);
    wpointer(0); gflush(); OK(lit(222, 172), "... and on it again: white"); wpointer(1);
    nev = 0; release();
    wpointer(0); gflush(); OK(nev == 1 && ev[0].type == W_BUTTON && ev[0].id == b1 && ev[0].win == w2 && !lit(222, 172), "let go on it: W_BUTTON"); wpointer(1);
    nev = 0; press(); go(500, 400); release();
    OK(nev == 0 && whole(), "let go off it: nothing");

    go(300, 279); nev = 0; press();
    go(340, 249);
    wpointer(0); gflush(); OK(nev == 0 && lit(300, 119) != GREY(300, 119) && lit(440, 130) != GREY(440, 130) && lit(301, 120) == GREY(301, 120), "a window's bar dragged: its outline follows"); wpointer(1);
    release();
    OK(nev == 1 && ev[0].type == W_MOVED && ev[0].win == w2 && ev[0].x == 240 && ev[0].y == 120 && wplace(w2, &wx, &wy, 0, 0) == 0 && wx == 240 && wy == 120, "let go: the window is there, W_MOVED");
    OK(whole(), "... the screen whole, the outline gone");

    m0 = wmenu("File", "New\nQuit"); m1 = wmenu("Edit", "Cut\nCopy\nPaste");
    wpointer(0); gflush(); OK(m0 == 0 && m1 == 1 && area(8, 463, 39, 478) < 32 * 16 && area(56, 463, 87, 478) < 32 * 16 && area(100, 463, 199, 478) == 100 * 16, "wmenu: two names on the bar"); wpointer(1);
    go(60, 472); nev = 0; press();
    wpointer(0); gflush(); OK(nev == 0 && lit(50, 420) && lit(100, 420) && area(48, 412, 105, 461) > 58 * 50 / 2, "a menu's name pressed: the menu let down, white"); wpointer(1);
    go(70, 420);
    wpointer(0); gflush(); OK(!lit(50, 420) && lit(50, 440), "the pointer on an item: that row turned over"); wpointer(1);
    release();
    OK(nev == 1 && ev[0].type == W_MENU && ev[0].id == m1 && ev[0].item == 2, "let go on it: W_MENU with the menu and the item");
    OK(whole(), "... the menu gone, the screen whole");
    go(20, 472); nev = 0; press(); go(60, 472);
    wpointer(0); gflush(); OK(lit(50, 420), "from one menu's name to the next: that menu instead"); wpointer(1);
    go(300, 300); release();
    OK(nev == 0 && whole(), "let go off the menu: nothing, and the screen whole");

    w3 = wopen(20, 300, 100, 60, "Three");
    wplace(w3, &wx, &wy, &ww, &wh);
    go(wx + 8, wy + wh + 9); nev = 0; press(); release();
    OK(nev == 1 && ev[0].type == W_CLOSE && ev[0].win == w3, "the close box: W_CLOSE");
    wclose(w3);
    OK(wplace(w3, 0, 0, 0, 0) < 0 && whole(), "wclose: the window gone, the screen whole");
    wpointer(0); gflush(); OK(lit(30, 310) == GREY(30, 310) && lit(60, 330) == GREY(60, 330), "... the desk where it was"); wpointer(1);

    dev_key('q'); nev = 0; run();
    OK(nev == 1 && ev[0].type == W_KEY && ev[0].key == 'q', "a key: W_KEY");
    OK(wevent(&e, 0) == 0 && wevent(&e, 3) == 0, "nothing more: wevent says so");

    /* many windows, moved about by the hand: the screen stays whole */
    for (k = 0; k < 6; k++) { n = wopen(30 + 70 * k, 40 + 45 * k, 150, 90, "W"); wdraw(n); gcircle(75, 45, 10 + 5 * k); gtext(4, 4, "abc"); wshow(n); }
    OK(whole(), "six more windows, each drawn in: the screen whole");
    for (bad = 0, k = 0; k < 12 && !bad; k++) {
        n = 3 + (k * 5) % 6;
        if (wplace(n, &wx, &wy, &ww, &wh) < 0) { bad = 1; break; }
        go(wx + 60, wy + wh + 9); press(); go(wx + 60 + (k % 3 - 1) * 90, wy + wh + 9 + (k % 4 - 2) * 40); release();
        if (!whole()) bad = 1;
    }
    OK(!bad, "twelve drags, the windows in front of and behind one another: whole after each");
    wdraw(1); gink(G_BLACK); gfcircle(100, 60, 30); wshow(1);
    OK(whole(), "a window behind others drawn in and shown: only where it shows");
    n = wopen(400, 20, 100, 80, "Part"); wdraw(n); gfbox(0, 0, 99, 79); wshow(n);
    wdraw(n); gink(G_BLACK); gfbox(10, 10, 30, 30); gfbox(60, 50, 90, 70); wpart(n, 10, 10, 30, 30);
    wpointer(0); gflush(); k = area(410, 30, 430, 50) == 0 && area(460, 70, 490, 90) == 31 * 21; wpointer(1);
    OK(k, "wpart: that rectangle of the window is shown, the rest of what was drawn not yet");
    wpart(n, 90, 70, 60, 50);
    wpointer(0); gflush(); k = area(460, 70, 490, 90) == 0 && area(400, 20, 499, 99) == 100 * 80 - 21 * 21 - 31 * 21; wpointer(1);
    OK(k && whole(), "... and then the other: the screen is whole");
    wpointer(0); gflush(); memcpy(snap, mem, 18 * MW * sizeof snap[0]); wpointer(1);
    wmove(1, 300, 400); wdraw(1); gfbox(0, 0, 199, 119); wshow(1);
    wpointer(0); gflush(); n = !memcmp(snap, mem, 18 * MW * sizeof snap[0]); wpointer(1);
    OK(n && whole(), "a window moved up under the menu bar: the bar is not drawn on");
    OK(!dev_bad(), "the chip had no command it does not know and lost no word");
    wend();
    OK(gfd() == -1 && gmfd() == -1, "wend");
    if (fails) printf("%d of %d checks failed\n", fails, checks); else printf("win_test: %d checks\n", checks);
    return fails != 0;
}
