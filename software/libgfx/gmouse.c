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
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/mouse.h>
#include <gfx.h>
#include "gfxint.h"

static int fd = -1;
static int mx, my, mb;                  /* the pointer and the buttons */
static unsigned char buf[48];           /* packets read and not yet looked at */
static int nbuf, at;

int gmopen(void)
{
    if (fd >= 0) return 0;
    fd = open("/dev/mouse", O_RDONLY);
    if (fd < 0) return -1;
    mx = _gvi.width / 2; my = _gvi.height / 2; mb = 0;
    nbuf = at = 0;
    return 0;
}

void gmclose(void)
{
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
