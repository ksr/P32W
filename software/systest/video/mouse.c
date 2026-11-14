/* mouse.c -- /dev/mouse (mouse(4), D120) and the library's calls over it (gfx(3)): the rules that need no
   hand on the mouse. Whether it moves the pointer is for a person to see (/usr/examples/mouse.c); the
   emulators play a hand in the host's make test. Needs the video card:  sh /usr/tests/run video */
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/mouse.h>
#include <gfx.h>

static int checks, failed;
static void ok(int cond, const char *what)
{
    checks++;
    if (!cond) { failed++; printf("FAIL: %s\n", what); }
}

int main(void)
{
    unsigned char p[3] = { MS_XNEG | MS_YNEG | 8, 0xFB, 0xFF };
    struct timeval tv;
    fd_set r;
    int fd, x = -1, y = -1, b = -1, n;
    fd = open("/dev/mouse", O_RDONLY);
    ok(fd >= 0, "/dev/mouse opens");
    errno = 0;
    ok(open("/dev/mouse", O_RDONLY) < 0 && errno == EBUSY, "a second open: EBUSY");
    errno = 0;
    ok(read(fd, p, 2) < 0 && errno == EINVAL, "a read of less than a packet: EINVAL");
    ok(write(fd, p, 3) < 0, "it cannot be written");
    FD_ZERO(&r); FD_SET(fd, &r); tv.tv_sec = 0; tv.tv_usec = 0;
    n = select(fd + 1, &r, 0, 0, &tv);
    ok(n == 0 || n == 1, "select does not wait for it");
    errno = 0;
    ok(gmopen() < 0 && errno == EBUSY && gmfd() == -1, "gmopen while it is open: EBUSY");
    ok(close(fd) == 0 && gmopen() == 0 && gmfd() >= 0, "closed, and opened by gmopen");
    n = gmouse(&x, &y, &b, 0);
    ok((n == 0 || n == 1) && x >= 0 && y >= 0 && b >= 0 && b < 8, "gmouse with no wait: at once");
    gmat(100, 50); n = gmouse(&x, &y, &b, 1);
    ok(n >= 0 && (n == 1 || (x == 100 && y == 50)), "gmat puts the pointer somewhere; gmouse waits a 50th of a second");
    gmclose();
    errno = 0;
    ok(gmfd() == -1 && gmouse(&x, &y, &b, 0) < 0 && errno == EBADF, "gmclose");
    p[0] = MS_XNEG | MS_YNEG | 8; p[1] = 0xFB; p[2] = 0xFF;
    ok(MS_DX(p) == -5 && MS_DY(p) == -1 && !(p[0] & (MS_LEFT | MS_RIGHT | MS_MIDDLE)), "a packet's movements have their signs in its first byte");
    printf("mouse: %d checks, %d failed\n", checks, failed);
    return failed != 0;
}
