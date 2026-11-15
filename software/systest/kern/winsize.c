/* winsize.c -- a terminal's size in the kernel, and SIGWINCH (D126), on a pseudo-terminal of the test's own:
   what its master says is what the programs on it are told; they hear of a change, if they listen. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sgtty.h>
#include <sys/wait.h>

static volatile int got;
static void winch(int sig) { got++; signal(sig, winch); }

static int m, up[2], down[2];           /* the master; the child's word to the parent, and the parent's to it */
static char dev[20];

static void set(int fd, int cols, int rows)
{
    struct winsize w;
    w.ws_col = (unsigned short)cols; w.ws_row = (unsigned short)rows; w.ws_xpixel = w.ws_ypixel = 0;
    if (ioctl(fd, TIOCSWINSZ, &w) < 0) printf("TIOCSWINSZ: %s\n", strerror(errno));
}

static void said(void)                  /* the child: what it had to say is said; it waits to be told to go on */
{
    char c;
    fflush(stdout);
    write(up[1], "r", 1);
    while (read(down[0], &c, 1) != 1)   /* (a signal cuts the read short: the word is still to come) */
        ;
}

static void go(void)                    /* the parent: the child goes on, and is waited for */
{
    char c;
    write(down[1], "w", 1);
    read(up[0], &c, 1);
}

static int ended(int pid)               /* the child's end waited for: its status */
{
    int st = -1, k;
    while ((k = wait(&st)) != pid && k >= 0)
        ;
    return st;
}

static int child(int listen)            /* a program on the terminal: the first of a session, so it is its own */
{
    struct winsize w;
    int pid, s, n;
    fflush(stdout);
    if ((pid = fork()) != 0) { char c; read(up[0], &c, 1); return pid; }
    setsid();
    s = open(dev, O_RDWR);
    if (s < 0) { printf("%s: %s\n", dev, strerror(errno)); exit(1); }
    if (listen) signal(SIGWINCH, winch);
    ioctl(s, TIOCGWINSZ, &w);
    printf("a program on it is told %d by %d\n", w.ws_col, w.ws_row);
    said();
    if (!listen) { printf("a change it does not listen for: nothing happens to it\n"); fflush(stdout); exit(0); }
    ioctl(s, TIOCGWINSZ, &w);
    printf("a change: SIGWINCH %d time(s), and it is told %d by %d\n", got, w.ws_col, w.ws_row);
    n = got;
    said();
    printf("the same size set again: %d more\n", got - n);
    n = got;
    set(s, 100, 30);                    /* (from the terminal's own side, as stty does) */
    ioctl(s, TIOCGWINSZ, &w);
    printf("set from the terminal itself: %d more, %d by %d\n", got - n, w.ws_col, w.ws_row);
    fflush(stdout);
    exit(0);
}

int main(void)
{
    struct winsize w;
    int k, a, b, c, r;
    for (m = -1, k = 4; k < 16 && m < 0; k++) { sprintf(dev, "/dev/ptyp%d", k); m = open(dev, O_RDWR); }
    if (m < 0) { printf("no pseudo-terminal: %s\n", strerror(errno)); return 1; }
    sprintf(dev, "/dev/ttyp%d", k - 1);
    pipe(up); pipe(down);
    if (ioctl(m, TIOCGWINSZ, &w) < 0) printf("TIOCGWINSZ: %s\n", strerror(errno));
    printf("a new terminal: %d by %d\n", w.ws_col, w.ws_row);
    set(m, 80, 24);
    a = child(1);
    set(m, 132, 50); go();
    set(m, 132, 50);
    write(down[1], "w", 1);             /* (it says the rest and ends) */
    printf("it ended with status %d\n", ended(a));
    b = child(0);
    set(m, 20, 5);
    write(down[1], "w", 1);
    printf("it ended with status %d\n", ended(b));
    ioctl(m, TIOCGWINSZ, &w);
    printf("the master is told %d by %d\n", w.ws_col, w.ws_row);
    unsetenv("COLUMNS"); unsetenv("LINES");
    k = ttysize(m, &c, &r); printf("ttysize of the terminal: %d, %d by %d\n", k, c, r);
    set(m, 0, 0);
    k = ttysize(m, &c, &r); printf("... its size not known: %d, %d by %d\n", k, c, r);
    setenv("COLUMNS", "132", 1); setenv("LINES", "43", 1);
    k = ttysize(m, &c, &r); printf("... with COLUMNS and LINES: %d, %d by %d\n", k, c, r);
    k = ttysize(up[0], &c, 0); printf("ttysize of a pipe: %d, %d\n", k, c);
    printf("kill(SIGWINCH) to itself, not listening: %d\n", kill(getpid(), SIGWINCH));
    return 0;
}
