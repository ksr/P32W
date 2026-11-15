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
 * A terminal's size is told to the kernel (TIOCSWINSZ on the master), where its programs ask for it; when
 * its window's corner is dragged it has another, the kernel is told, and the programs hear (SIGWINCH: D126).
 *
 * The Desk menu starts terminals and the desk's own programs (files, clock: programs like any other, each on
 * a terminal of its own, as win starts them), and saves the screen as a picture file. The Windows menu has
 * every window by name, to bring one to the front (wmwin.c).
 *
 * -s plays a hand from a file, for tests and for showing: lines of
 *      key TEXT        the keys of TEXT, typed to the window in front (\n Return, \e ESC, \\ a backslash)
 *      mouse X Y       the pointer to the screen's dot X, Y
 *      press, release  the left button
 *      wait TEXT       until the line the front window's cursor is on ends with TEXT (a prompt; or, with the
 *                      cursor at the start of a line, the line above does), after something has been written
 *                      there since the last key. When the window in front is the picture of a program whose
 *                      terminal has no window, it is the terminal the keys last went to that is looked at
 *      pause N         N 50ths of a second in which nothing is typed or moved
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
#include <sgtty.h>
#include <gfx.h>
#include "wmwin.h"
#include "wmterm.h"
#include "wmgfx.h"

#define NTERM 12                        /* terminals at once */
#define PTY0  4                         /* the pseudo-terminals that are wm's: 4 to 15 (0 to 3 are for logins from */
#define NPTY  16                        /* the network, which change their owners; kernel/tty.c, root.manifest) */

static struct tw {
    int used, win, fd, fresh;           /* win: its window, or 0: a program of win's that has written nothing yet */
    int own;                            /* started by win: its picture's box ends it */
    int px, py;                         /* where its window is to be opened */
    char name[24];
    struct term t;
    struct gx g;                        /* its program's picture */
} tw[NTERM];
static int dcols = 64, drows = 16;      /* a new terminal's size */
static int opened;                      /* terminals opened so far (their names, and where they are put) */
static FILE *script;
static char waitfor[80];
static char *dumped;                    /* what "dump" gathered */
static int dumplen;

static struct tw *of(int win)           /* the terminal that window is, or whose program's picture it is */
{
    int k;
    for (k = 0; win && k < NTERM; k++) if (tw[k].used && (tw[k].win == win || tw[k].g.win == win)) return &tw[k];
    return 0;
}

static int newterm(int cols, int rows, const char *cmd, const char *dir, const char *title, int quiet);

/* win's request: W cols;rows;quiet : name ^_ directory ^_ command -- a program in a window of its own */
static void winreq(char *s)
{
    char *name, *dir, *cmd;
    int v[3], n = 0;
    v[0] = v[1] = v[2] = 0;
    for (s++; *s && *s != ':'; s++) {
        if (*s >= '0' && *s <= '9') v[n] = v[n] * 10 + (*s - '0');
        else if (*s == ';' && n < 2) n++;
    }
    if (!*s) return;
    name = s + 1;
    if (!(dir = strchr(name, 0x1F))) return;
    *dir++ = 0;
    if (!(cmd = strchr(dir, 0x1F))) return;
    *cmd++ = 0;
    if (v[0] < 20 || v[0] > TCOLS) v[0] = 64;
    if (v[1] < 4 || v[1] > 28) v[1] = 16;
    newterm(v[0], v[1], cmd, dir, name, v[2]);
}

void trequest(struct term *t, char *req)        /* (from wmterm.c) a program on that terminal asks for something */
{
    int k;
    if (req[0] == 'W') { winreq(req); return; }
    for (k = 0; k < NTERM; k++) if (tw[k].used && &tw[k].t == t) gx_request(&tw[k].g, req);
}

/* a shell on pseudo-terminal pty's slave: the first program of a session of its own, with that terminal */
/* A program on pseudo-terminal pty's slave: the first of a session of its own, with that terminal. cmd 0: a
   shell to type at; else that command (as sh takes it), in the directory dir. */
static int shell(int pty, int cols, int rows, const char *cmd, const char *dir)
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
        unsetenv("LINES"); unsetenv("COLUMNS");         /* (wm's own terminal's, if they were set: the kernel knows */
        setenv("WM", "1", 1);                           /* this one's size, and that it may change) */
        signal(SIGINT, SIG_DFL); signal(SIGQUIT, SIG_DFL); signal(SIGHUP, SIG_DFL);
        if (!sh || !*sh) sh = "/bin/sh";
        if (cmd) {
            if (dir && *dir) chdir(dir);
            execl("/bin/sh", "sh", "-c", cmd, (char *)0);
        } else execl(sh, sh, (char *)0);
        _exit(127);
    }
    while ((k = wait(&st)) != pid && k >= 0)
        ;
    return 0;
}

static void termsize(int fd, int cols, int rows)        /* the kernel told a terminal's size: its programs ask it */
{
    struct winsize ws;
    ws.ws_col = (unsigned short)cols; ws.ws_row = (unsigned short)rows;
    ws.ws_xpixel = (unsigned short)(8 * cols); ws.ws_ypixel = (unsigned short)(16 * rows);
    ioctl(fd, TIOCSWINSZ, &ws);
}

static void termwin(struct tw *w)       /* the terminal's window opened */
{
    wfit(&w->px, &w->py, 8 * w->t.cols + 2 * TM, 16 * w->t.rows + 2 * TM);
    w->win = wopen(w->px, w->py, 8 * w->t.cols + 2 * TM, 16 * w->t.rows + 2 * TM, w->name);
    if (w->win < 0) w->win = 0;
    w->t.win = w->win;                  /* any number of whole characters, 20 by 4 to 80 by 28 */
    if (w->win) wsizes(w->win, 2 * TM, 2 * TM, 8, 16, 8 * 20 + 2 * TM, 16 * 4 + 2 * TM, 8 * TCOLS + 2 * TM, 16 * 28 + 2 * TM);
}

static void resized(struct tw *w, int dw, int dh)       /* its window's corner was dragged: dw by dh dots wanted */
{
    int cols = (dw - 2 * TM) / 8, rows = (dh - 2 * TM) / 16;
    if (cols < 20 || cols > TCOLS || rows < 4 || rows > 28) return;
    if (wresize(w->win, 8 * cols + 2 * TM, 16 * rows + 2 * TM) < 0) {   /* no room for its picture: the size it had, */
        tresize(&w->t, w->t.cols, w->t.rows);                           /* drawn again (the picture is a new one) */
        tshow(&w->t);
        return;
    }
    tresize(&w->t, cols, rows);
    tshow(&w->t);
    termsize(w->fd, cols, rows);        /* (last: the programs draw again in a window that is ready) */
}

/* A terminal. cmd 0: with a shell, and its window at once. Else with that command (win), and, if quiet, no
   window until the command writes something: a program that only draws has its picture's window alone. */
static int newterm(int cols, int rows, const char *cmd, const char *dir, const char *title, int quiet)
{
    char dev[20];
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
    memset(w, 0, sizeof *w);
    termsize(fd, cols, rows);
    if (title && *title) { strncpy(w->name, title, sizeof w->name - 1); }
    else sprintf(w->name, "Terminal %d", opened + 1);
    w->px = 24 + 24 * (opened % 5); w->py = 430 - 16 * rows - 22 * (opened % 5);
    w->own = cmd != 0;
    tinit(&w->t, 0, cols, rows);
    if (!quiet) termwin(w);
    if ((!quiet && !w->win) || shell(pty, cols, rows, cmd, dir) < 0) {
        if (w->win) wclose(w->win);
        close(fd);
        return -1;
    }
    opened++;
    w->used = 1; w->fd = fd;
    w->g.fd = fd;
    if (cmd) w->g.title = w->name;      /* (its picture has the program's name) */
    if (w->win) tshow(&w->t);
    return k;
}

static void endterm(struct tw *w)       /* the window gone, and its programs hung up */
{
    gx_close(&w->g);
    close(w->fd);
    if (w->win) wclose(w->win);
    w->used = 0;
}

static void say(const char *line)       /* a line of what "dump" tells */
{
    char *p = realloc(dumped, dumplen + strlen(line) + 2);
    if (!p) return;
    dumped = p;
    strcpy(dumped + dumplen, line); dumplen += strlen(line);
    dumped[dumplen++] = '\n'; dumped[dumplen] = 0;
}

static unsigned short bits[480 * 40];   /* the screen's dots, or a picture's */

/* The screen as it is, saved as a picture file in the directory wm was started in: screen1.pbm, or the first
   of screen2.pbm, ... that is not there yet. A PBM file (P4): a 1 is a black dot, so a lit dot is a 0. */
static void snapshot(void)
{
    char name[24];
    unsigned char row[80];
    int n, fd = -1, y, k, wpl;
    for (n = 1; n < 1000; n++) {
        sprintf(name, "screen%d.pbm", n);
        if ((fd = open(name, O_WRONLY | O_CREAT | O_EXCL, 0644)) >= 0 || errno != EEXIST) break;
    }
    if (fd < 0) return;
    wpointer(0);
    wdraw(0);                           /* (the screen itself is what is read) */
    wpl = (gwidth + 15) / 16;
    if (wpl <= 40 && gheight <= 480 && gread(0, 0, gwidth - 1, gheight - 1, bits) == 0) {
        n = sprintf(name, "P4\n%d %d\n", gwidth, gheight);
        write(fd, name, n);
        for (y = 0; y < gheight; y++) { /* (the top line is the first, in the file as in what was read) */
            for (k = 0; k < wpl; k++) { row[2 * k] = (unsigned char)~(bits[y * wpl + k] >> 8); row[2 * k + 1] = (unsigned char)~bits[y * wpl + k]; }
            write(fd, row, (gwidth + 7) / 8);
        }
    }
    close(fd);
    wpointer(1);
    gx_done();
}

static void dump(void)                  /* the windows and what their terminals show, kept for the end */
{
    char line[TCOLS + 40];
    int k, r, last, x, y, h, n;
    unsigned wd;
    for (k = 0; k < NTERM; k++) {
        struct tw *w = &tw[k];
        if (!w->used) continue;
        if (w->win) wplace(w->win, &x, &y, 0, 0);
        for (last = w->t.rows - 1; last >= 0; last--) { ttext(&w->t, last, line); if (line[0]) break; }
        for (r = w->win ? -1 : last + 1; r <= last; r++) {
            if (r < 0) sprintf(line, "window %d%s at %d, %d: %d by %d", w->win, w->win == wtop() ? " (in front)" : "", x, y, w->t.cols, w->t.rows);
            else { line[0] = '|'; line[1] = ' '; ttext(&w->t, r, line + 2); }
            say(line);
        }
        if (!w->g.win) continue;        /* its program's picture: where, how big, and how many of its dots are lit */
        wplace(w->g.win, &x, &y, &r, &last);
        wdraw(w->g.win);
        gread(0, 0, r - 1, last - 1, bits);
        for (n = 0, h = 0; h < last * ((r + 15) / 16); h++) for (wd = bits[h]; wd; wd &= wd - 1) n++;
        sprintf(line, "window %d%s at %d, %d: a picture %d by %d, %d dots lit", w->g.win, w->g.win == wtop() ? " (in front)" : "", x, y, r, last, n);
        say(line);
        gx_done();
    }
}

/* ---------------------------------------------------------------- the hand a script plays */
static int sx, sy, sb, quit, pausing;
static struct tw *keyed;                /* the terminal the keys last went to: a picture opened since may be in front */

static int waiting(void)                /* is the script still waiting for its text? */
{
    char line[TCOLS + 1];
    struct tw *w = of(wtop());
    int n, m = strlen(waitfor);
    if (!m) return 0;
    if (w && !w->win && keyed && keyed->used) w = keyed;        /* (a picture alone, of win's program: no text there) */
    if (!w || !w->fresh) return 1;
    if (w->t.col || !w->t.row) { memcpy(line, w->t.ch[w->t.row], w->t.col); line[n = w->t.col] = 0; }
    else { ttext(&w->t, w->t.row - 1, line); n = strlen(line); }       /* (a prompt on a line of its own: BASIC's Ok) */
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
        if ((w = of(wtop())) != 0) { w->fresh = 0; write(w->fd, out, n); keyed = w; }
    } else if (!strcmp(line, "mouse")) { sscanf(a, "%d %d", &sx, &sy); wfeed(sx, sy, sb); }
    else if (!strcmp(line, "press")) wfeed(sx, sy, sb = 1);
    else if (!strcmp(line, "release")) wfeed(sx, sy, sb = 0);
    else if (!strcmp(line, "wait")) { strncpy(waitfor, a, sizeof waitfor - 1); waitfor[sizeof waitfor - 1] = 0; }
    else if (!strcmp(line, "pause")) pausing = atoi(a);
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
    int k, n, max, mdesk, x, y, wx, wy, tick;
    for (k = 1; k < argc; k++) {
        if (!strcmp(argv[k], "-g") && k + 1 < argc && sscanf(argv[k + 1], "%dx%d", &dcols, &drows) == 2) k++;
        else if (!strcmp(argv[k], "-s") && k + 1 < argc) {
            if (!(script = fopen(argv[++k], "r"))) { perror(argv[k]); return 1; }
        } else { fprintf(stderr, "usage: wm [-g COLSxROWS] [-s SCRIPT]\n"); return 2; }
    }
    if (dcols < 20 || dcols > TCOLS || drows < 4 || drows > 28) { fprintf(stderr, "wm: a terminal is 20 to %d columns by 4 to 28 rows\n", TCOLS); return 2; }
    if (wstart() < 0) { perror("wm"); return 1; }
    signal(SIGHUP, SIG_IGN);            /* (a terminal of its own that goes is not wm's end) */
    mdesk = wmenu("Desk", "New terminal\nBig terminal\nFiles\nClock\nSnapshot\nRepaint\nQuit");
    wwinmenu("Windows");
    newterm(dcols, drows, 0, 0, 0, 0);
    while (!quit) {
        FD_ZERO(&r);
        max = gmfd();
        FD_SET(max, &r);
        if (!script) FD_SET(0, &r);
        for (k = 0; k < NTERM; k++) if (tw[k].used) { FD_SET(tw[k].fd, &r); if (tw[k].fd > max) max = tw[k].fd; }
        for (tick = script != 0, k = 0; k < NTERM; k++) if (tw[k].used && tw[k].g.mwait) tick = 1;
        tv.tv_sec = 0; tv.tv_usec = 20000;      /* (a 50th at a time while a hand is played or a program waits for the mouse) */
        n = select(max + 1, &r, 0, 0, tick ? &tv : 0);
        if (n < 0) { if (errno != EINTR) break; FD_ZERO(&r); }
        if (n == 0) {
            for (k = 0; k < NTERM; k++) if (tw[k].used) gx_tick(&tw[k].g);
            if (pausing) pausing--;
        }
        while (wevent(&e, 0)) {         /* what the mouse did, and the keys */
            if (e.type == W_KEY && (w = of(e.win)) != 0) { unsigned char c = (unsigned char)e.key; write(w->fd, &c, 1); }
            if (e.type == W_SIZED && (w = of(e.win)) != 0 && w->win == e.win) resized(w, e.x, e.y);
            if (e.type == W_CLOSE && (w = of(e.win)) != 0) {    /* (a picture's box: the picture; of win's program, all) */
                if (w->g.win == e.win && !w->own) gx_close(&w->g); else endterm(w);
            }
            if ((e.type == W_DOWN || e.type == W_DRAG || e.type == W_UP) && (w = of(e.win)) != 0 && w->g.win == e.win)
                gx_mouse(&w->g, e.x, e.y, e.type == W_UP ? 0 : 1);
            if (e.type == W_MENU && e.id == mdesk) {
                if (e.item == 0) newterm(dcols, drows, 0, 0, 0, 0);
                if (e.item == 1) newterm(80, 24, 0, 0, 0, 0);
                if (e.item == 2) newterm(dcols, drows, "exec files", "", "Files", 1);   /* (the desk's programs: as win */
                if (e.item == 3) newterm(dcols, drows, "exec clock", "", "Clock", 1);   /* starts one, in wm's directory) */
                if (e.item == 4) snapshot();
                if (e.item == 5) wrepaint();
                if (e.item == 6) quit = 1;
            }
        }
        for (k = 0; k < NTERM; k++) {   /* what the programs wrote */
            w = &tw[k];
            if (!w->used || !FD_ISSET(w->fd, &r)) continue;
            n = read(w->fd, buf, sizeof buf);
            if (n <= 0) { if (n < 0 && errno == EINTR) continue; endterm(w); continue; }    /* (its last program has ended) */
            tput(&w->t, buf, n);
            if (!w->used) continue;     /* (it asked for itself to be closed) */
            gx_show(&w->g);
            gx_done();
            if (!w->win && w->t.wrote) {                /* win's program has written something: its terminal's window */
                termwin(w);
                for (x = 0; x < w->t.rows; x++) { w->t.d0[x] = 0; w->t.d1[x] = (short)(w->t.cols - 1); }
            }
            if (w->win) tshow(&w->t);
            if (w->t.wrote) { w->t.wrote = 0; w->fresh = 1; }  /* (text: what a played hand waits for) */
        }
        if ((k = wwhere(&x, &y)) != 0 && (w = of(k)) != 0 && w->g.win == k && w->g.mwait && !w->g.mb) {
            wplace(k, &wx, &wy, 0, 0);  /* the pointer over a picture whose program waits to hear of it */
            gx_mouse(&w->g, x - wx, y - wy, 0);
        }
        if (script && !quit && !pausing && !waiting()) step();
    }
    for (k = 0; k < NTERM; k++) if (tw[k].used) close(tw[k].fd);
    wend();
    if (dumped) fputs(dumped, stdout);
    return 0;
}
