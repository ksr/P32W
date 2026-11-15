/* files -- the files of a directory in a window of the window manager, to look through and to open
 * (files(1), D125).
 *
 *   files [directory]
 *
 * The directory's files are listed by name, a line each: a directory with / after its name, a program with
 * *, and each one's size. A press of the mouse on a line picks it, and its particulars are told on the line
 * at the foot; a press on the one that is picked opens it. A directory is gone into; a program is started;
 * any other file is shown by view; all in windows of their own (win). The bar at the right goes a page back
 * or on. The keys do the same: j and k (or the arrows) pick the next and the one before, Return opens,
 * u goes up to the directory above, e has the file edited in vi, r reads the directory again, q ends.
 * The window may be made bigger or smaller: the list is as long and as wide as it has room for.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <gfx.h>

#define SB   12                         /* the scroll bar's width */
#define NENT 512                        /* files of a directory, at most (the first that many are shown) */

static struct ent { char name[30]; struct stat st; int ok; } *ent;      /* (ok: its particulars could be had) */
static int nent, first, sel, rows, cols;
static char cwd[256], note[80];         /* note: what is told on the last line in place of a file's particulars */
static volatile int stop;
static void sig(int n) { (void)n; stop = 1; }

static int isdir(const struct ent *e) { return e->ok && S_ISDIR(e->st.st_mode); }

static int order(const void *a, const void *b) { return strcmp(((const struct ent *)a)->name, ((const struct ent *)b)->name); }

static void load(void)              /* the directory the program is in, read: its names in order, .. first */
{
    DIR *d;
    struct dirent *de;
    nent = first = 0; sel = -1;
    if (!getcwd(cwd, sizeof cwd)) strcpy(cwd, "?");
    if (!(d = opendir("."))) { sprintf(note, "cannot be read: %.40s", strerror(errno)); return; }
    while (nent < NENT && (de = readdir(d)) != 0) {
        struct ent *e = &ent[nent];
        if (!de->d_ino || !strcmp(de->d_name, ".")) continue;
        if (!strcmp(de->d_name, "..") && !strcmp(cwd, "/")) continue;
        strncpy(e->name, de->d_name, sizeof e->name - 1); e->name[sizeof e->name - 1] = 0;
        e->ok = stat(e->name, &e->st) == 0;
        nent++;
    }
    closedir(d);
    qsort(ent, nent, sizeof *ent, order);       /* (.. sorts before every name a file is likely to have) */
    sprintf(note, nent == NENT ? "the first %d files" : nent == 1 ? "%d file" : "%d files", nent);
}

/* ---------------------------------------------------------------- the picture */
static int rowy(int r) { return gheight - 16 * (r + 2); }       /* the bottom dot of the list's line r */

static void head(void)                  /* the directory's name, on a white line at the top */
{
    char s[100];
    int n = strlen(cwd), room = gwidth / 8 - 1;
    gink(G_WHITE); gfbox(0, gheight - 16, gwidth - 1, gheight - 1);
    sprintf(s, "%.*s", room > 99 ? 99 : room, n > room ? cwd + n - room : cwd);    /* (too long: its end) */
    gink(G_BLACK); gtext(4, gheight - 16, s);
    gink(G_WHITE);
}

static void foot(void)                  /* the picked file's particulars, or the note */
{
    char s[120], when[20];
    struct ent *e = sel >= 0 ? &ent[sel] : 0;
    time_t t, now = time(0);
    int k, room = (gwidth - 4) / 8;
    if (e && e->ok) {
        struct passwd *pw = getpwuid(e->st.st_uid);
        struct group *gr = getgrgid(e->st.st_gid);
        unsigned m = e->st.st_mode;
        char mode[11];
        strcpy(mode, "----------");
        mode[0] = S_ISDIR(m) ? 'd' : S_ISCHR(m) ? 'c' : S_ISBLK(m) ? 'b' : S_ISFIFO(m) ? 'p' : '-';
        for (k = 0; k < 9; k++) if (m & (0400 >> k)) mode[1 + k] = "rwx"[k % 3];
        if (m & S_ISUID) mode[3] = m & 0100 ? 's' : 'S';
        if (m & S_ISGID) mode[6] = m & 0010 ? 's' : 'S';
        t = e->st.st_mtime;             /* (as ls -l has it: the time of day, or the year of one long ago) */
        strftime(when, sizeof when, t > now || now - t > 180 * 86400 ? "%b %e  %Y" : "%b %e %H:%M", localtime(&t));
        if (pw && gr) sprintf(s, "%s %.8s %.8s %s", mode, pw->pw_name, gr->gr_name, when);
        else sprintf(s, "%s %d %d %s", mode, e->st.st_uid, e->st.st_gid, when);
    } else if (e) sprintf(s, "%.29s: cannot be looked at", e->name);
    else strcpy(s, note);
    if (room > (int)sizeof s - 1) room = sizeof s - 1;
    for (k = strlen(s); k < room; k++) s[k] = ' ';
    s[room] = 0;
    glook(G_OPAQUE); gtext(4, 0, s); glook(0);
    gline(0, 17, gwidth - 1, 17);
}

static void bar(void)                   /* the bar at the right: its box is as much of the list as shows, where it is */
{
    int x = gwidth - SB, y0 = 19, y1 = gheight - 18, h = y1 - y0 - 1, top, len;
    gink(G_BLACK); gfbox(x, y0, gwidth - 1, y1);
    gink(G_WHITE); gbox(x, y0, gwidth - 1, y1);
    if (nent <= rows) { top = 0; len = h; }
    else { len = h * rows / nent; if (len < 6) len = 6; top = (h - len) * first / (nent - rows); }
    gfbox(x + 2, y1 - 1 - top - len + 1, gwidth - 3, y1 - 1 - top);
}

static void flip(int k)                 /* file k's line turned over, if it shows: picked, or not any longer */
{
    if (k < first || k >= first + rows) return;
    gink(G_XOR); gfbox(0, rowy(k - first), gwidth - SB - 2, rowy(k - first) + 15); gink(G_WHITE);
}

static void list(void)                  /* the lines from `first` */
{
    char s[100], part[40];
    int r, n;
    glook(G_OPAQUE);
    for (r = 0; r < rows; r++) {
        struct ent *e = first + r < nent ? &ent[first + r] : 0;
        memset(s, ' ', cols); s[cols] = 0;
        if (e) {
            n = sprintf(part, "%.*s%s", cols - 12 > 29 ? 29 : cols - 12, e->name, isdir(e) ? "/" : e->ok && S_ISREG(e->st.st_mode) && (e->st.st_mode & 0111) ? "*" : "");
            memcpy(s, part, n);
            if (e->ok && !isdir(e)) { n = sprintf(part, "%d", e->st.st_size); memcpy(s + cols - n, part, n); }
        }
        gtext(4, rowy(r), s);
    }
    glook(0);
    if (sel >= 0) flip(sel);
    bar();
}

static void all(void) { gclear(); head(); list(); foot(); gflush(); }

static int fit(void)                    /* the list's lines and columns, for the size the picture is: 0 if too small */
{
    cols = (gwidth - SB - 8) / 8; rows = (gheight - 36) / 16;
    if (cols > 96) cols = 96;
    if (first > nent - rows) first = nent - rows;       /* (as much of the list as there is room for) */
    if (first < 0) first = 0;
    if (sel >= first + rows) first = sel - rows + 1;    /* (and the one that is picked) */
    return cols >= 16 && rows >= 1;
}

static void pick(int k)                 /* file k is the one picked (-1: none); the list moved so that it shows */
{
    int was = first;
    if (k >= nent) k = nent - 1;
    if (k < -1) k = -1;
    if (k >= 0 && k < first) first = k;
    if (k >= first + rows) first = k - rows + 1;
    if (first != was) { sel = k; list(); }
    else { if (sel >= 0) flip(sel); sel = k; if (sel >= 0) flip(sel); }
    foot();
    gflush();
}

static void turn(int by)                /* the list a page back or on */
{
    int to = first + by;
    if (to > nent - rows) to = nent - rows;
    if (to < 0) to = 0;
    if (to == first) return;
    first = to;
    list();
    gflush();
}

/* ---------------------------------------------------------------- opening */
static void run(char **argv)            /* a command of win's, and its end waited for (it only asks wm, and is over) */
{
    int pid = fork(), st, k;
    if (pid == 0) { execv("/bin/win", argv); _exit(127); }
    if (pid > 0) while ((k = wait(&st)) != pid && k >= 0) ;
}

static void go(const char *dir)         /* into another directory */
{
    if (chdir(dir) < 0) {               /* (not allowed, or gone: said at the foot, and nothing is picked) */
        sprintf(note, "%.29s: %.40s", dir, strerror(errno));
        if (sel >= 0) flip(sel);
        sel = -1;
        foot(); gflush();
        return;
    }
    load();
    all();
}

static void act(int k, int edit)      /* file k opened: gone into, started, or shown (edit: in vi) */
{
    char path[40], *argv[8];
    struct ent *e;
    int n = 0;
    if (k < 0 || k >= nent) return;
    e = &ent[k];
    if (isdir(e)) { go(e->name); return; }
    if (!e->ok || !S_ISREG(e->st.st_mode)) return;
    sprintf(path, "./%s", e->name);
    argv[n++] = "win";
    argv[n++] = "-n"; argv[n++] = e->name;
    if (edit) { argv[n++] = "-s"; argv[n++] = "80x24"; argv[n++] = "vi"; }
    else if (!(e->st.st_mode & 0111)) argv[n++] = "view";
    argv[n++] = path;
    argv[n] = 0;
    run(argv);
}

int main(int argc, char **argv)
{
    int x, y, b, was = 0, k, r;
    if (argc > 2) { fprintf(stderr, "usage: files [directory]\n"); return 2; }
    if (!getenv("WM")) { fprintf(stderr, "files: not in a window of wm\n"); return 1; }
    if (argc > 1 && chdir(argv[1]) < 0) { perror(argv[1]); return 1; }
    if (!(ent = malloc(NENT * sizeof *ent))) { fprintf(stderr, "files: no room\n"); return 1; }
    gsize(8 * 42 + 8 + SB, 16 * 16 + 36);       /* (42 columns, 16 files) */
    if (gopen() < 0 || gmopen() < 0) { perror("files"); return 1; }
    signal(SIGINT, sig); signal(SIGTERM, sig); signal(SIGHUP, sig);
    if (!fit()) { gclose(); fprintf(stderr, "files: the window is too small\n"); return 1; }
    gmaysize(8 * 16 + 8 + SB, 16 * 3 + 36, 8, 16);      /* (its corner: 16 columns and 3 files or more) */
    load();
    all();
    while (!stop) {
        k = ginput(&x, &y, &b, -1);
        if (k < 0 || k == 'q' || k == 'Q') break;
        if (k == G_SIZE) { if (fit()) all(); continue; }        /* (another size: a new picture, drawn again) */
        if (k == G_MOUSE) {
            if ((b & 1) && !was) {      /* a press: on the bar, or on a file's line */
                if (x >= gwidth - SB) turn(y >= gheight / 2 ? -(rows - 1) : rows - 1);
                else if (y >= rowy(rows - 1) && y < gheight - 16) {
                    r = first + (gheight - 17 - y) / 16;
                    if (r < nent) { if (r == sel) act(r, 0); else pick(r); }
                }
            }
            was = b & 1;
            continue;
        }
        if (k == 27) {                  /* an arrow: ESC [ A or B */
            if ((k = ginput(0, 0, 0, 5)) == '[') k = ginput(0, 0, 0, 5); else if (k != G_SIZE) k = 0;
            if (k == G_SIZE) { if (fit()) all(); continue; }
            k = k == 'A' ? 'k' : k == 'B' ? 'j' : 0;
        }
        if (k == 'j') pick(sel + 1);
        if (k == 'k') pick(sel > 0 ? sel - 1 : 0);
        if (k == ' ' || k == 6) turn(rows - 1);
        if (k == 'b' || k == 2) turn(-(rows - 1));
        if (k == '\n' || k == '\r') act(sel, 0);
        if (k == 'e') act(sel, 1);
        if (k == 'u' || k == 8 || k == 127) go("..");
        if (k == 'r') { load(); all(); }
    }
    gclose();
    return 0;
}
