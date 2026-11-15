/* gmouse.c -- libgfx: the mouse (/dev/mouse, mouse(4)), as a pointer on the screen.
 *
 * The mouse says how far it has moved and which buttons are down, in packets. gmouse adds the movements up
 * into a place on the screen, which starts at its middle and stops at its edges, and tells the buttons. A
 * packet in which a button goes down or comes up is never passed over: gmouse comes back with it, and the
 * packets after it wait for the next call. So a click is seen however slowly a program asks, and a movement
 * is as many packets as have come.
 *
 * Drawing the pointer is the program's own business: a kept picture put down with G_FLIP, or a cross of
 * lines in G_XOR, taken away and drawn again when gmouse says it has moved.
 */
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/mouse.h>
#ifdef __P32__
#include <sgtty.h>
#endif
#include <gfx.h>
#include "gfxint.h"

static int fd = -1;
static int mx, my, mb;                  /* the pointer and the buttons */
static unsigned char buf[48];           /* packets read and not yet looked at */
static int nbuf, at;

static int wmouse;                      /* in a window of wm: the mouse is asked of wm */

int gmopen(void)
{
    if (_gwm) { wmouse = 1; return 0; }
    if (fd >= 0) return 0;
    fd = open("/dev/mouse", O_RDONLY);
    if (fd < 0) return -1;
    mx = _gvi.width / 2; my = _gvi.height / 2; mb = 0;
    nbuf = at = 0;
    return 0;
}

void gmclose(void)
{
    wmouse = 0;
    if (fd >= 0) close(fd);
    fd = -1;
}

int gmfd(void) { return fd; }

void gmat(int x, int y) { mx = x; my = y; }

/* the packets that have come, up to one that changes the buttons: 1 if anything is different */
static int take(void)
{
    int changed = 0, b;
    while (at < nbuf) {
        unsigned char *p = buf + at;
        at += 3;
        b = p[0] & (MS_LEFT | MS_RIGHT | MS_MIDDLE);
        if (p[1] || p[2]) {
            mx += MS_DX(p); my += MS_DY(p);
            if (mx < 0) mx = 0;
            if (my < 0) my = 0;
            if (_gvi.width && mx > _gvi.width - 1) mx = _gvi.width - 1;
            if (_gvi.height && my > _gvi.height - 1) my = _gvi.height - 1;
            changed = 1;
        }
        if (b != mb) { mb = b; return 1; }
    }
    return changed;
}

int gmouse(int *x, int *y, int *buttons, int wait)
{
    struct timeval tv, *tp = &tv;
    fd_set r;
    int changed = 0, n;
    if (wmouse && _gwm) {               /* wm says: where in the window, which buttons, and whether anything changed */
        int v[4];
        _gwn('M', 1, wait, 0, 0, 0, 0, 0);
        if (_gwa(v, 4) != 4) return -1;
        if (x) *x = v[0];
        if (y) *y = v[1];
        if (buttons) *buttons = v[2];
        return v[3];
    }
    if (fd < 0) { errno = EBADF; return -1; }
    if (at < nbuf) changed = take();    /* (what the last call left) */
    while (!changed) {
        FD_ZERO(&r); FD_SET(fd, &r);
        tv.tv_sec = wait > 0 ? wait / 50 : 0;
        tv.tv_usec = wait > 0 ? wait % 50 * 20000 : 0;
        if (wait < 0) tp = 0;
        n = select(fd + 1, &r, 0, 0, tp);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;              /* (nothing in that time) */
        n = read(fd, buf, sizeof buf);
        if (n < 0 && errno == EINTR) continue;
        if (n < 3) return -1;
        nbuf = n; at = 0;
        changed = take();
        if (wait >= 0) break;           /* (packets that changed nothing: the time is not begun again) */
    }
    if (x) *x = mx;
    if (y) *y = my;
    if (buttons) *buttons = mb;
    return changed;
}

/* ---------------------------------------------------------------- ginput: the mouse or a key
 * A program that is worked with the mouse and the keyboard waits for both at once. The keys are standard
 * input's, which is set to give each as it is typed, unechoed, from the first call until gclose (or the
 * program's end). In a window of wm both come through the program's terminal (gwin.c).
 */
#ifdef __P32__
static struct sgttyb kwas;
#endif
static int kraw, kend, karmed;          /* kend: standard input is at its end; no key will come */

static void kback(void)
{
#ifdef __P32__
    if (kraw) stty(0, &kwas);
#endif
    kraw = 0;
}

int ginput(int *x, int *y, int *buttons, int wait)
{
    struct timeval tv, *tp = &tv;
    fd_set r;
    unsigned char c;
    int n, max;
    if (_gwm) return _gwinput(x, y, buttons, wait, wmouse);
    if (!kraw) {
#ifdef __P32__
        struct sgttyb t;
        if (gtty(0, &kwas) == 0) { t = kwas; t.sg_flags = (t.sg_flags | CBREAK) & ~ECHO; stty(0, &t); kraw = 1; }
#endif
        _gatclose = kback;
        if (!karmed) { karmed = 1; atexit(kback); }
    }
    gflush();                           /* (what has been drawn shows while the program waits) */
    for (;;) {
        if (fd >= 0 && at < nbuf && take()) break;      /* (what the last call left of the mouse) */
        FD_ZERO(&r);
        max = -1;
        if (!kend) { FD_SET(0, &r); max = 0; }
        if (fd >= 0) { FD_SET(fd, &r); if (fd > max) max = fd; }
        tv.tv_sec = wait > 0 ? wait / 50 : 0;
        tv.tv_usec = wait > 0 ? wait % 50 * 20000 : 0;
        if (wait < 0) { if (max < 0) return -1; tp = 0; }       /* (nothing to wait for, for ever: not that) */
        n = select(max + 1, &r, 0, 0, tp);
        if (n <= 0) return 0;           /* (nothing in that time, or a signal the program catches came first) */
        if (!kend && FD_ISSET(0, &r)) {
            n = read(0, &c, 1);
            if (n == 1 && c) return c;
            if (n < 0 && errno == EINTR) return 0;
            if (n <= 0) kend = 1;
            if (kend && fd < 0) return wait < 0 ? -1 : 0;       /* (no key will come, and there is no mouse) */
            continue;
        }
        if (fd >= 0 && FD_ISSET(fd, &r)) {
            n = read(fd, buf, sizeof buf);
            if (n < 0 && errno == EINTR) return 0;
            if (n < 3) return -1;
            nbuf = n; at = 0;
            if (take()) break;
            if (wait >= 0) return 0;    /* (packets that changed nothing: the time is not begun again) */
        }
    }
    if (x) *x = mx;
    if (y) *y = my;
    if (buttons) *buttons = mb;
    return G_MOUSE;
}
