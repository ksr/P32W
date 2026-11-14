/* wm.c -- wm, the window manager: windows on the video card's screen, each a terminal with programs of its
 * own running in it (wm(1), D122).
 *
 *   wm [-g COLSxROWS] [-s SCRIPT]
 *
 * wm has the screen, the mouse and the keyboard. Every window is a pseudo-terminal: wm holds its master, and
 * on its slave is a shell, the leader of a session of its own, as on any terminal. What the programs there
 * write is put on the window's terminal (wmterm.c) and drawn into the window's picture; the keys typed go to
 * the window in front. So programs in different windows run at once, and one that is covered goes on writing
 * into its picture, which is shown when the window is uncovered (wmwin.c).
 *
 * Closing a window closes its master: the kernel hangs up the programs on it (SIGHUP). A window whose last
 * program has ended is closed here. Quit closes them all.
 *
 * -s plays a hand from a file, for tests and for showing: lines of
 *      key TEXT        the keys of TEXT, typed to the window in front (\n Return, \e ESC, \\ a backslash)
 *      mouse X Y       the pointer to the screen's dot X, Y
 *      press, release  the left button
 *      wait TEXT       until the line the front window's cursor is on ends with TEXT (a prompt), after something
 *                      has been written there since the last key
 *      dump            the windows and their terminals' text, to standard output when wm ends
 *      quit
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <gfx.h>
#include "wmwin.h"
#include "wmterm.h"

#define NTERM 12                        /* terminals at once */
#define PTY0  4                         /* the pseudo-terminals that are wm's: 4 to 15 (0 to 3 are for logins from */
#define NPTY  16                        /* the network, which change their owners; kernel/tty.c, root.manifest) */

static struct tw { int used, win, fd, fresh; struct term t; } tw[NTERM];
static int dcols = 64, drows = 16;      /* a new terminal's size */
static int opened;                      /* terminals opened so far (their names, and where they are put) */
static FILE *script;
static char waitfor[80];
static char *dumped;                    /* what "dump" gathered */
static int dumplen;

static struct tw *of(int win)
{
    int k;
    for (k = 0; k < NTERM; k++) if (tw[k].used && tw[k].win == win) return &tw[k];
    return 0;
}

/* a shell on pseudo-terminal pty's slave: the first program of a session of its own, with that terminal */
static int shell(int pty, int cols, int rows)
{
    char dev[20], n[12];
    const char *sh = getenv("SHELL");
    int pid, st, k;
    pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        if (fork() != 0) _exit(0);      /* (its child goes on alone: wm has no children to wait for) */
        setsid();
        for (k = 0; k < 20; k++) close(k);
        sprintf(dev, "/dev/ttyp%d", pty);
        if (open(dev, O_RDWR) != 0) _exit(1);           /* (the session's first terminal: its own) */
        dup(0); dup(0);
        sprintf(n, "%d", rows); setenv("LINES", n, 1);
        sprintf(n, "%d", cols); setenv("COLUMNS", n, 1);
        setenv("WM", "1", 1);
        signal(SIGINT, SIG_DFL); signal(SIGQUIT, SIG_DFL); signal(SIGHUP, SIG_DFL);
        if (!sh || !*sh) sh = "/bin/sh";
        execl(sh, sh, (char *)0);
        _exit(127);
    }
    while ((k = wait(&st)) != pid && k >= 0)
        ;
    return 0;
}

static int newterm(int cols, int rows)
{
    char dev[20], name[24];
    struct tw *w;
    int k, pty, fd = -1;
    for (k = 0; k < NTERM && tw[k].used; k++)
        ;
    if (k == NTERM) return -1;
    for (pty = PTY0; pty < NPTY; pty++) {
        sprintf(dev, "/dev/ptyp%d", pty);
        if ((fd = open(dev, O_RDWR)) >= 0) break;
    }
    if (fd < 0) return -1;
    w = &tw[k];
    sprintf(name, "Terminal %d", opened + 1);
    w->win = wopen(24 + 24 * (opened % 5), 430 - 16 * rows - 22 * (opened % 5), 8 * cols + 2 * TM, 16 * rows + 2 * TM, name);
    if (w->win < 0 || shell(pty, cols, rows) < 0) {
        if (w->win >= 0) wclose(w->win);
        close(fd);
        return -1;
    }
    opened++;
    w->used = 1; w->fd = fd; w->fresh = 0;
    tinit(&w->t, w->win, cols, rows);
    tshow(&w->t);
    return k;
}

static void endterm(struct tw *w)       /* the window gone, and its programs hung up */
{
    close(w->fd);
    wclose(w->win);
    w->used = 0;
}

static void dump(void)                  /* the windows and what their terminals show, kept for the end */
{
    char line[TCOLS + 40], *p;
    int k, r, last, x, y;
    for (k = 0; k < NTERM; k++) {
        struct tw *w = &tw[k];
        if (!w->used) continue;
        wplace(w->win, &x, &y, 0, 0);
        for (last = w->t.rows - 1; last >= 0; last--) { ttext(&w->t, last, line); if (line[0]) break; }
        for (r = -1; r <= last; r++) {
            if (r < 0) sprintf(line, "window %d%s at %d, %d: %d by %d", w->win, w->win == wtop() ? " (in front)" : "", x, y, w->t.cols, w->t.rows);
            else { line[0] = '|'; line[1] = ' '; ttext(&w->t, r, line + 2); }
            p = realloc(dumped, dumplen + strlen(line) + 2);
            if (!p) return;
            dumped = p;
            strcpy(dumped + dumplen, line); dumplen += strlen(line);
            dumped[dumplen++] = '\n'; dumped[dumplen] = 0;
        }
    }
}

/* ---------------------------------------------------------------- the hand a script plays */
static int sx, sy, sb, quit;

static int waiting(void)                /* is the script still waiting for its text? */
{
    char line[TCOLS + 1];
    struct tw *w = of(wtop());
    int n, m = strlen(waitfor);
    if (!m) return 0;
    if (!w || !w->fresh) return 1;
    memcpy(line, w->t.ch[w->t.row], w->t.col); line[n = w->t.col] = 0;
    if (n < m || strcmp(line + n - m, waitfor)) return 1;
    waitfor[0] = 0;
    return 0;
}

static void step(void)                  /* the script's next line */
{
    char line[200], *a;
    struct tw *w;
    int n;
    if (!fgets(line, sizeof line, script)) { quit = 1; return; }
    n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
    a = strchr(line, ' ');
    if (a) *a++ = 0; else a = line + n;
    if (!strcmp(line, "key")) {
        char out[200];
        for (n = 0; *a; a++) {
            if (*a == '\\' && a[1]) { a++; out[n++] = *a == 'n' ? '\n' : *a == 'e' ? 27 : *a; }
            else out[n++] = *a;
        }
        if ((w = of(wtop())) != 0) { w->fresh = 0; write(w->fd, out, n); }
    } else if (!strcmp(line, "mouse")) { sscanf(a, "%d %d", &sx, &sy); wfeed(sx, sy, sb); }
    else if (!strcmp(line, "press")) wfeed(sx, sy, sb = 1);
    else if (!strcmp(line, "release")) wfeed(sx, sy, sb = 0);
    else if (!strcmp(line, "wait")) { strncpy(waitfor, a, sizeof waitfor - 1); waitfor[sizeof waitfor - 1] = 0; }
    else if (!strcmp(line, "dump")) dump();
    else if (!strcmp(line, "quit")) quit = 1;
}

/* ---------------------------------------------------------------- the manager */
int main(int argc, char **argv)
{
    unsigned char buf[512];
    struct wevent e;
    struct timeval tv;
    struct tw *w;
    fd_set r;
    int k, n, max, mdesk;
    for (k = 1; k < argc; k++) {
        if (!strcmp(argv[k], "-g") && k + 1 < argc && sscanf(argv[k + 1], "%dx%d", &dcols, &drows) == 2) k++;
        else if (!strcmp(argv[k], "-s") && k + 1 < argc) {
            if (!(script = fopen(argv[++k], "r"))) { perror(argv[k]); return 1; }
        } else { fprintf(stderr, "usage: wm [-g COLSxROWS] [-s SCRIPT]\n"); return 2; }
    }
    if (dcols < 20 || dcols > TCOLS || drows < 4 || drows > 28) { fprintf(stderr, "wm: a terminal is 20 to %d columns by 4 to 28 rows\n", TCOLS); return 2; }
    if (wstart() < 0) { perror("wm"); return 1; }
    signal(SIGHUP, SIG_IGN);            /* (a terminal of its own that goes is not wm's end) */
    mdesk = wmenu("Desk", "New terminal\nBig terminal\nRepaint\nQuit");
    newterm(dcols, drows);
    while (!quit) {
        FD_ZERO(&r);
        max = gmfd();
        FD_SET(max, &r);
        if (!script) FD_SET(0, &r);
        for (k = 0; k < NTERM; k++) if (tw[k].used) { FD_SET(tw[k].fd, &r); if (tw[k].fd > max) max = tw[k].fd; }
        tv.tv_sec = 0; tv.tv_usec = 20000;
        if (select(max + 1, &r, 0, 0, script ? &tv : 0) < 0 && errno != EINTR) break;
        while (wevent(&e, 0)) {         /* what the mouse did, and the keys */
            if (e.type == W_KEY && (w = of(e.win)) != 0) { unsigned char c = (unsigned char)e.key; write(w->fd, &c, 1); }
            if (e.type == W_CLOSE && (w = of(e.win)) != 0) endterm(w);
            if (e.type == W_MENU && e.id == mdesk) {
                if (e.item == 0) newterm(dcols, drows);
                if (e.item == 1) newterm(80, 24);
                if (e.item == 2) wrepaint();
                if (e.item == 3) quit = 1;
            }
        }
        for (k = 0; k < NTERM; k++) {   /* what the programs wrote */
            w = &tw[k];
            if (!w->used || !FD_ISSET(w->fd, &r)) continue;
            n = read(w->fd, buf, sizeof buf);
            if (n <= 0) { if (n < 0 && errno == EINTR) continue; endterm(w); continue; }    /* (its last program has ended) */
            tput(&w->t, buf, n);
            tshow(&w->t);
            w->fresh = 1;
        }
        if (script && !quit && !waiting()) step();
    }
    for (k = 0; k < NTERM; k++) if (tw[k].used) close(tw[k].fd);
    wend();
    if (dumped) fputs(dumped, stdout);
    return 0;
}
