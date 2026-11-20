/* finder -- the files of a directory as pictures in a window of the window manager: to open, and to keep in
 * order (finder(1), D144). It is P8X's finder brought here, with one difference that is this machine's: there
 * the finder is the whole screen and becomes the program it starts; here it is a window among the others, and
 * what it starts has windows of its own and runs beside it.
 *
 *   finder [directory]
 *
 * Each file is a picture with its name under it: a folder for a directory, a window for a program, a framed
 * picture for a PBM picture, a page with lines for text and a page with a mark for anything else. A press of
 * the mouse picks one, and its particulars are told on the line at the foot; a press on the one that is picked
 * opens it. A directory is gone into; a program is started; any other file is shown by view; all in windows
 * of their own (win). The bar at the right goes a page back or on.
 *
 * The words on the top line are menus, let down while the button is held and chosen from by letting it go:
 * File has Open, Edit (vi), Rename, Duplicate, Move, New folder and Remove; Apps has the programs of
 * /etc/apps, a line each (its name, a colon, the command that starts it). The keys do the same: h j k l (or
 * the arrows) pick, Return opens, e edits, R renames, D duplicates, M moves, N makes a folder, X removes,
 * u goes to the directory above, space and b turn the page, r reads the directory again, q ends.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
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
#define CW   96                         /* a file's place: across, */
#define CH   56                         /* ... and down: its picture, 32 dots by 24, and its name under it */
#define TOP  16                         /* the line at the top: the menus' words and the directory's name */
#define FOOT 18                         /* the line at the foot, and the rule over it */
#define NAME 11                         /* characters of a name that show under its picture */
#define NENT 512                        /* files of a directory, at most (the first that many are shown) */
#define APPS "/etc/apps"
#define NAPP 8                          /* its lines, at most: a menu must fit the smallest window */

enum { DIR_, PROG, PICT, TEXT, OTHER };

static struct ent { char name[30]; struct stat st; char ok, kind; } *ent;      /* (ok: its particulars could be had) */
static int nent, first, sel, rows, cols;
static char cwd[256], note[80];         /* note: what is told on the last line in place of a file's particulars */
static volatile int stop;
static int resized;                     /* the picture has another size: all of it is to be drawn again */
static void sig(int n) { (void)n; stop = 1; }

/* ---------------------------------------------------------------- the five pictures */
static const char *const art[5][24] = {
    {   /* folder */
        "................................",
        "................................",
        "................................",
        "...#########....................",
        "..#.........#...................",
        "..#..........################...",
        "..#..........................#..",
        "..#..........................#..",
        "..############################..",
        "..#..........................#..",
        "..#..........................#..",
        "..#..........................#..",
        "..#..........................#..",
        "..#..........................#..",
        "..#..........................#..",
        "..#..........................#..",
        "..#..........................#..",
        "..#..........................#..",
        "..#..........................#..",
        "..#..........................#..",
        "..#..........................#..",
        "...##########################...",
        "................................",
        "................................",
    },
    {   /* program */
        "................................",
        "................................",
        "..############################..",
        "..############################..",
        "..###...######################..",
        "..###...######################..",
        "..############################..",
        "..#..........................#..",
        "..#..........................#..",
        "..#..........................#..",
        "..#....#.....................#..",
        "..#.....#....................#..",
        "..#......#...................#..",
        "..#.......#..................#..",
        "..#.......#..................#..",
        "..#......#...................#..",
        "..#.....#....................#..",
        "..#....#......#######........#..",
        "..#..........................#..",
        "..#..........................#..",
        "..#..........................#..",
        "..############################..",
        "................................",
        "................................",
    },
    {   /* picture */
        "................................",
        "................................",
        "..############################..",
        "..#..........................#..",
        "..#..........................#..",
        "..#...................###....#..",
        "..#...................###....#..",
        "..#...................###....#..",
        "..#..........................#..",
        "..#..........................#..",
        "..#..........................#..",
        "..#..........#...............#..",
        "..#.........#.#..............#..",
        "..#........#...#.............#..",
        "..#.......#.....#....#.......#..",
        "..#......#.......#..#.#......#..",
        "..#.....#.........##...#.....#..",
        "..#....#..........##....#....#..",
        "..#...#..........#..#....#...#..",
        "..#..#..........#....#....#..#..",
        "..#..........................#..",
        "..############################..",
        "................................",
        "................................",
    },
    {   /* text */
        "................................",
        "......###############...........",
        "......#.............##..........",
        "......#.............#.#.........",
        "......#.............#..#........",
        "......#.............#...#.......",
        "......#.............######......",
        "......#..................#......",
        "......#..................#......",
        "......#..##############..#......",
        "......#..................#......",
        "......#..................#......",
        "......#..##############..#......",
        "......#..................#......",
        "......#..................#......",
        "......#..##############..#......",
        "......#..................#......",
        "......#..................#......",
        "......#..##############..#......",
        "......#..................#......",
        "......#..................#......",
        "......#..................#......",
        "......####################......",
        "................................",
    },
    {   /* other */
        "................................",
        "......###############...........",
        "......#.............##..........",
        "......#.............#.#.........",
        "......#.............#..#........",
        "......#.............#...#.......",
        "......#.............######......",
        "......#..................#......",
        "......#..................#......",
        "......#..................#......",
        "......#..................#......",
        "......#..................#......",
        "......#.......####.......#......",
        "......#.......####.......#......",
        "......#.......####.......#......",
        "......#.......####.......#......",
        "......#..................#......",
        "......#..................#......",
        "......#..................#......",
        "......#..................#......",
        "......#..................#......",
        "......#..................#......",
        "......####################......",
        "................................",
    },
};
static unsigned short icon[5][48];      /* ... as the library writes them: a line's 32 dots in two words */

static void icons(void)
{
    int k, y, x;
    for (k = 0; k < 5; k++)
        for (y = 0; y < 24; y++)
            for (x = 0; x < 32; x++)
                if (art[k][y][x] == '#') icon[k][2 * y + (x >> 4)] |= 0x8000 >> (x & 15);
}

/* ---------------------------------------------------------------- the directory */
static int what(const struct ent *e)    /* which picture is a file's: a regular file that is no program is looked into */
{
    unsigned char b[64];
    int fd, n, k;
    if (!e->ok) return OTHER;
    if (S_ISDIR(e->st.st_mode)) return DIR_;
    if (!S_ISREG(e->st.st_mode)) return OTHER;
    if (e->st.st_mode & 0111) return PROG;
    if ((fd = open(e->name, O_RDONLY)) < 0) return OTHER;
    n = read(fd, b, sizeof b);
    close(fd);
    if (n >= 3 && b[0] == 'P' && b[1] == '4' && (b[2] == '\n' || b[2] == ' ')) return PICT;
    for (k = 0; k < n; k++)
        if (b[k] >= 127 || (b[k] < 32 && b[k] != '\n' && b[k] != '\t' && b[k] != '\r' && b[k] != '\f')) return OTHER;
    return TEXT;
}

static int order(const void *a, const void *b) { return strcmp(((const struct ent *)a)->name, ((const struct ent *)b)->name); }

static void load(void)                  /* the directory the program is in, read: its names in order, .. first; */
                                        /* the names that begin with a dot are left out, but for .. */
{
    DIR *d;
    struct dirent *de;
    nent = first = 0; sel = -1;
    if (!getcwd(cwd, sizeof cwd)) strcpy(cwd, "?");
    if (!(d = opendir("."))) { sprintf(note, "cannot be read: %.40s", strerror(errno)); return; }
    while (nent < NENT && (de = readdir(d)) != 0) {
        struct ent *e = &ent[nent];
        if (!de->d_ino || (de->d_name[0] == '.' && strcmp(de->d_name, ".."))) continue;    /* (as ls: no .names) */
        if (!strcmp(de->d_name, "..") && !strcmp(cwd, "/")) continue;
        strncpy(e->name, de->d_name, sizeof e->name - 1); e->name[sizeof e->name - 1] = 0;
        e->ok = stat(e->name, &e->st) == 0;
        e->kind = what(e);
        nent++;
    }
    closedir(d);
    qsort(ent, nent, sizeof *ent, order);       /* (.. sorts before every name a file is likely to have) */
    sprintf(note, nent == NENT ? "the first %d files" : nent == 1 ? "%d file" : "%d files", nent);
}

static int named(const char *name)      /* the file of that name: which one it is, or -1 */
{
    int k;
    for (k = 0; k < nent; k++) if (!strcmp(ent[k].name, name)) return k;
    return -1;
}

/* ---------------------------------------------------------------- the picture */
static void head(void)                  /* the menus' words and the directory's name, on a white line at the top */
{
    char s[100];
    int n = strlen(cwd), room = (gwidth - 104) / 8 - 1;
    gink(G_WHITE); gfbox(0, gheight - TOP, gwidth - 1, gheight - 1);
    gink(G_BLACK); gtext(8, gheight - TOP, "File"); gtext(56, gheight - TOP, "Apps");
    if (room > 0) {
        sprintf(s, "%.*s", room > 99 ? 99 : room, n > room ? cwd + n - room : cwd);        /* (too long: its end) */
        gtext(104, gheight - TOP, s);
    }
    gink(G_WHITE);
}

static void line(const char *t)         /* the line at the foot says t */
{
    char s[120];
    int k, room = (gwidth - 4) / 8;
    if (room > (int)sizeof s - 1) room = sizeof s - 1;
    for (k = 0; k < room && t[k]; k++) s[k] = t[k];
    for (; k < room; k++) s[k] = ' ';
    s[room] = 0;
    glook(G_OPAQUE); gtext(4, 0, s); glook(0);
    gline(0, FOOT - 1, gwidth - 1, FOOT - 1);
}

static void foot(void)                  /* the picked file's name and particulars, or the note */
{
    char s[160], when[20];
    struct ent *e = sel >= 0 ? &ent[sel] : 0;
    time_t t, now = time(0);
    int k;
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
        if (pw && gr) sprintf(s, "%s  %s %.8s %.8s %d %s", e->name, mode, pw->pw_name, gr->gr_name, (int)e->st.st_size, when);
        else sprintf(s, "%s  %s %d %d %d %s", e->name, mode, e->st.st_uid, e->st.st_gid, (int)e->st.st_size, when);
    } else if (e) sprintf(s, "%.29s: cannot be looked at", e->name);
    else strcpy(s, note);
    line(s);
}

static void bar(void)                   /* the bar at the right: its box is as much of the files as shows, where it is */
{
    int x = gwidth - SB, y0 = FOOT + 1, y1 = gheight - TOP - 2, h = y1 - y0 - 1, top, len, page = rows * cols;
    gink(G_BLACK); gfbox(x, y0, gwidth - 1, y1);
    gink(G_WHITE); gbox(x, y0, gwidth - 1, y1);
    if (nent <= page) { top = 0; len = h; }
    else { len = h * page / nent; if (len < 6) len = 6; top = (h - len) * first / (nent - 1); }
    gfbox(x + 2, y1 - 1 - top - len + 1, gwidth - 3, y1 - 1 - top);
}

/* where file k's place is, if it shows: its left-hand dot and the dot over its top; 0 if it does not show */
static int place(int k, int *x, int *y)
{
    if (k < first || k >= first + rows * cols || k >= nent) return 0;
    *x = (k - first) % cols * CW;
    *y = gheight - TOP - 2 - (k - first) / cols * CH;
    return 1;
}

static void flip(int k)                 /* file k's place turned over, if it shows: picked, or not any longer */
{
    int x, y;
    if (!place(k, &x, &y)) return;
    gink(G_XOR); gfbox(x + 2, y - CH + 3, x + CW - 3, y - 2); gink(G_WHITE);
}

static void cell(int k)                 /* file k's picture and its name, in its place */
{
    char s[NAME + 1];
    int x, y, n;
    struct ent *e = &ent[k];
    if (!place(k, &x, &y)) return;
    gwrite(x + (CW - 32) / 2, y - 6 - 23, x + (CW - 32) / 2 + 31, y - 6, icon[(int)e->kind], G_COPY);
    n = strlen(e->name);
    if (n > NAME) { memcpy(s, e->name, NAME - 1); s[NAME - 1] = '~'; n = NAME; }       /* (too long: its beginning) */
    else memcpy(s, e->name, n);
    s[n] = 0;
    gtext(x + (CW - 8 * n) / 2, y - 6 - 24 - 4 - 16, s);
}

static void grid(void)                  /* the files from `first`, as many as there are places for */
{
    int k;
    gink(G_BLACK); gfbox(0, FOOT, gwidth - SB - 1, gheight - TOP - 1); gink(G_WHITE);
    for (k = first; k < first + rows * cols && k < nent; k++) cell(k);
    if (sel >= 0) flip(sel);
    bar();
}

static void all(void) { resized = 0; gclear(); head(); grid(); foot(); gflush(); }

static int fit(void)                    /* the places across and down, for the size the picture is: 0 if too small */
{
    cols = (gwidth - SB) / CW; rows = (gheight - TOP - FOOT - 2) / CH;
    if (cols < 1 || rows < 1) return 0;
    first -= first % cols;
    if (sel >= 0) { if (sel < first) first = sel - sel % cols; while (sel >= first + rows * cols) first += cols; }
    while (first > 0 && first >= nent) first -= cols;
    return 1;
}

static void pick(int k)                 /* file k is the one picked (-1: none); the files moved so that it shows */
{
    int was = first;
    if (k >= nent) k = nent - 1;
    if (k < -1) k = -1;
    if (k >= 0 && k < first) first = k - k % cols;
    while (k >= first + rows * cols) first += cols;
    if (first != was) { sel = k; grid(); }
    else { if (sel >= 0) flip(sel); sel = k; if (sel >= 0) flip(sel); }
    foot();
    gflush();
}

static void turn(int by)                /* a page back or on (by: rows) */
{
    int to = first + by * cols;
    while (to > 0 && to >= nent) to -= cols;
    if (to < 0) to = 0;
    if (to == first) return;
    first = to;
    grid();
    gflush();
}

static void say(const char *fmt, const char *a, const char *b)  /* something told at the foot, until a file is picked */
{
    sprintf(note, fmt, a, b);
    if (sel >= 0) flip(sel);
    sel = -1;
    foot(); gflush();
}

/* ---------------------------------------------------------------- menus, and a name asked for */
/* a menu let down under the top line, its left side at x: n items in a box, the one the pointer is on turned over
   while the button is held. The item it is let go on, or -1. What was under it is kept and put back */
static int menu(int x, char **item, int n)
{
    int w = 0, k, x0, y0, y1 = gheight - TOP - 1, kept, on = -1, mx, my, b, e;
    for (k = 0; k < n; k++) if ((int)strlen(item[k]) > w) w = strlen(item[k]);
    w = 8 * w + 16;
    x0 = x + w > gwidth ? gwidth - w : x;
    if (x0 < 0) x0 = 0;
    y0 = y1 - 16 * n - 3;
    kept = gkeep(x0, y0, x0 + w - 1, y1);
    gink(G_BLACK); gfbox(x0, y0, x0 + w - 1, y1);
    gink(G_WHITE); gbox(x0, y0, x0 + w - 1, y1);
    for (k = 0; k < n; k++) gtext(x0 + 8, y1 - 2 - 16 * (k + 1), item[k]);
    gflush();
    for (;;) {
        e = ginput(&mx, &my, &b, -1);
        if (e == G_SIZE) { resized = 1; on = -1; break; }
        if (e != G_MOUSE) { on = -1; break; }           /* (a key, or the end: nothing is chosen) */
        k = mx > x0 && mx < x0 + w - 1 && my < y1 - 1 && my >= y1 - 2 - 16 * n ? (y1 - 2 - my) / 16 : -1;
        if (k != on) {
            gink(G_XOR);
            if (on >= 0) gfbox(x0 + 2, y1 - 2 - 16 * (on + 1), x0 + w - 3, y1 - 3 - 16 * on);
            if (k >= 0) gfbox(x0 + 2, y1 - 2 - 16 * (k + 1), x0 + w - 3, y1 - 3 - 16 * k);
            gink(G_WHITE);
            on = k;
            gflush();
        }
        if (!(b & 1)) break;
    }
    if (kept >= 0) { gstamp(kept, x0, y0, G_COPY); gdrop(kept); gflush(); }
    else resized = 1;                   /* (no room to keep it: all drawn again) */
    return on;
}

/* a name asked for on the line at the foot: what is typed shows there. Return ends it: 1. ESC, or a press of the
   mouse, gives it up: 0. slash: it may have / in it (a directory to move to) */
static int ask(const char *what, char *s, int max, int slash)
{
    char t[160];
    int k, n = strlen(s), b;
    for (;;) {
        sprintf(t, "%s: %s_", what, s);
        line(t); gflush();
        k = ginput(0, 0, &b, -1);
        if (k == G_SIZE) { resized = 1; return 0; }
        if (k == G_MOUSE) { if (b & 1) return 0; continue; }
        if (k <= 0 || stop) return 0;
        if (k == 27) { while (ginput(0, 0, 0, 5) > 0) ; return 0; }     /* (an arrow's tail goes with it) */
        if (k == '\n' || k == '\r') return n > 0;
        if (k == 8 || k == 127) { if (n > 0) s[--n] = 0; continue; }
        if (k == 21) { s[n = 0] = 0; continue; }                        /* (^U: all of it) */
        if (k > 32 && k < 127 && n < max && (slash || k != '/')) { s[n++] = k; s[n] = 0; }
    }
}

static int yes(const char *what, const char *name)     /* asked at the foot: y for yes */
{
    char t[160];
    int k;
    sprintf(t, "%s %.40s? y to do it", what, name);
    line(t); gflush();
    do k = ginput(0, 0, 0, -1); while (k == G_MOUSE);
    if (k == G_SIZE) resized = 1;
    return k == 'y' || k == 'Y';
}

/* ---------------------------------------------------------------- opening, and starting programs */
static void start(char **argv)          /* a program started and left to itself (win is over at once: it only asks wm) */
{
    int pid = fork(), st, k;
    if (pid == 0) {
        if (fork() == 0) { execv(argv[0], argv); _exit(127); }
        _exit(0);
    }
    if (pid > 0) while ((k = wait(&st)) != pid && k >= 0) ;
}

static void go(const char *dir)         /* into another directory */
{
    if (chdir(dir) < 0) { say("%.29s: %.40s", dir, strerror(errno)); return; }
    load();
    all();
}

static void act(int k, int edit)        /* file k opened: gone into, started, or shown (edit: in vi) */
{
    char path[40], *argv[8];
    struct ent *e;
    int n = 0;
    if (k < 0 || k >= nent) return;
    e = &ent[k];
    if (e->kind == DIR_) { go(e->name); return; }
    if (!e->ok || !S_ISREG(e->st.st_mode)) return;
    sprintf(path, "./%s", e->name);
    argv[n++] = "/bin/win";
    argv[n++] = "-n"; argv[n++] = e->name;
    if (edit) { argv[n++] = "-s"; argv[n++] = "80x24"; argv[n++] = "vi"; }
    else if (e->kind != PROG) argv[n++] = "view";
    argv[n++] = path;
    argv[n] = 0;
    start(argv);
}

static void apps(void)                  /* the Apps menu: the lines of /etc/apps, a name and a command each */
{
    static char text[NAPP][100];
    char *item[NAPP], *cmd[NAPP], *argv[4], *p;
    FILE *f = fopen(APPS, "r");
    int n = 0, k;
    while (f && n < NAPP && fgets(text[n], sizeof text[n], f)) {
        if (text[n][0] == '#' || !(p = strchr(text[n], ':'))) continue;
        *p++ = 0;
        p[strcspn(p, "\n")] = 0;
        if (!text[n][0] || !*p) continue;
        item[n] = text[n]; cmd[n] = p;
        n++;
    }
    if (f) fclose(f);
    if (!n) { say("no programs in %s", APPS, ""); return; }
    if ((k = menu(52, item, n)) < 0) return;
    argv[0] = "/bin/sh"; argv[1] = "-c"; argv[2] = cmd[k]; argv[3] = 0;
    start(argv);
}

/* ---------------------------------------------------------------- keeping files in order */
static void after(const char *name)     /* the directory read again, and the file of that name picked if it is there */
{
    char was[30];
    int k;
    if (name) { strncpy(was, name, sizeof was - 1); was[sizeof was - 1] = 0; }  /* (it may be one of ent's own) */
    load();
    k = name ? named(was) : -1;
    if (k >= 0) { sel = k; first = 0; fit(); }
    all();
}

static int copy(const char *from, const char *to, int mode)     /* a file's bytes into a new one: 0, or -1 (errno) */
{
    static char buf[4096];
    int in = open(from, O_RDONLY), out, n = 0, e;
    if (in < 0) return -1;
    if ((out = open(to, O_WRONLY | O_CREAT | O_EXCL, mode & 0777)) < 0) { e = errno; close(in); errno = e; return -1; }
    while ((n = read(in, buf, sizeof buf)) > 0)
        if (write(out, buf, n) != n) { n = -1; break; }
    e = errno;
    close(in);
    if (close(out) < 0 && n >= 0) { n = -1; e = errno; }
    if (n < 0) { unlink(to); errno = e; return -1; }
    return 0;
}

static void file(int op)                /* the File menu's item op, or its key, for the file that is picked */
{
    char name[130], to[170];
    struct ent *e = sel >= 0 ? &ent[sel] : 0;
    struct stat st;
    name[0] = 0;
    if (op == 0) { act(sel, 0); return; }
    if (op == 1) { act(sel, 1); return; }
    if (op == 5) {                      /* a new folder */
        if (!ask("New folder", name, 28, 0)) { foot(); gflush(); return; }
        if (mkdir(name, 0777) < 0) { say("%.29s: %.40s", name, strerror(errno)); return; }
        after(name);
        return;
    }
    if (!e || !strcmp(e->name, "..")) { say("%s", "pick a file first", ""); return; }
    if (op == 2 || op == 3) {           /* another name: for it, or for a copy of it */
        if (!ask(op == 2 ? "Rename to" : "Duplicate as", name, 28, 0) || !strcmp(name, e->name)) { foot(); gflush(); return; }
        if (stat(name, &st) == 0) { say("there is a %.29s already", name, ""); return; }
        if (op == 3 && e->kind == DIR_) { say("%.29s is a directory: not copied", e->name, ""); return; }
        if ((op == 2 ? rename(e->name, name) : copy(e->name, name, e->st.st_mode)) < 0) { say("%.29s: %.40s", name, strerror(errno)); return; }
        after(name);
    } else if (op == 4) {               /* into another directory, under its own name */
        if (!ask("Move to the directory", name, 120, 1)) { foot(); gflush(); return; }
        if (stat(name, &st) < 0 || !S_ISDIR(st.st_mode)) { say("%.40s is not a directory", name, ""); return; }
        sprintf(to, "%s/%s", name, e->name);
        if (stat(to, &st) == 0) { say("there is a %.29s there already", e->name, ""); return; }
        if (rename(e->name, to) < 0) {
            if (errno != EXDEV || e->kind == DIR_ || copy(e->name, to, e->st.st_mode) < 0 || unlink(e->name) < 0) {
                say("%.29s: %.40s", e->name, strerror(errno));          /* (another disk: a file is copied there) */
                return;
            }
        }
        after(0);
    } else if (op == 6) {
        strcpy(name, e->name);
        if (!yes("Remove", name)) { foot(); gflush(); return; }
        if ((e->kind == DIR_ ? rmdir(name) : unlink(name)) < 0) { say("%.29s: %.40s", name, strerror(errno)); return; }
        after(0);
    }
}

int main(int argc, char **argv)
{
    static char *filem[] = { "Open", "Edit", "Rename      R", "Duplicate   D", "Move        M", "New folder  N", "Remove      X" };
    int x, y, b, was = 0, k, r;
    if (argc > 2) { fprintf(stderr, "usage: finder [directory]\n"); return 2; }
    if (!getenv("WM")) { fprintf(stderr, "finder: not in a window of wm\n"); return 1; }
    if (argc > 1 && chdir(argv[1]) < 0) { perror(argv[1]); return 1; }
    if (!(ent = malloc(NENT * sizeof *ent))) { fprintf(stderr, "finder: no room\n"); return 1; }
    icons();
    gsize(5 * CW + SB, 4 * CH + TOP + FOOT + 2);        /* (five across, four down) */
    if (gopen() < 0 || gmopen() < 0) { perror("finder"); return 1; }
    signal(SIGINT, sig); signal(SIGTERM, sig); signal(SIGHUP, sig);
    load();
    if (!fit()) { gclose(); fprintf(stderr, "finder: the window is too small\n"); return 1; }
    gmaysize(2 * CW + SB, 2 * CH + TOP + FOOT + 2, CW, CH);     /* (its corner: two across and two down or more) */
    all();
    while (!stop) {
        if (resized) { if (fit()) all(); else resized = 0; }
        k = ginput(&x, &y, &b, -1);
        if (k < 0 || k == 'q' || k == 'Q') break;
        if (k == G_SIZE) { resized = 1; continue; }     /* (another size: a new picture, drawn again) */
        if (k == G_MOUSE) {
            if ((b & 1) && !was) {      /* a press: on a menu's word, on the bar, or on a file */
                was = 1;
                if (y >= gheight - TOP) {
                    if (x >= 4 && x < 44) { if ((r = menu(4, filem, 7)) >= 0) file(r); was = 0; }
                    else if (x >= 52 && x < 92) { apps(); was = 0; }
                } else if (x >= gwidth - SB) turn(y >= gheight / 2 ? -(rows > 1 ? rows - 1 : 1) : rows > 1 ? rows - 1 : 1);
                else if (y >= FOOT && x < cols * CW) {
                    r = first + (gheight - TOP - 2 - y) / CH * cols + x / CW;
                    if ((gheight - TOP - 2 - y) / CH < rows && r < nent) { if (r == sel) act(r, 0); else pick(r); }
                }
                continue;
            }
            was = b & 1;
            continue;
        }
        if (k == 27) {                  /* an arrow: ESC [ and A, B, C or D */
            if ((k = ginput(0, 0, 0, 5)) == '[') k = ginput(0, 0, 0, 5); else if (k != G_SIZE) k = 0;
            if (k == G_SIZE) { resized = 1; continue; }
            k = k == 'A' ? 'k' : k == 'B' ? 'j' : k == 'C' ? 'l' : k == 'D' ? 'h' : 0;
        }
        if (k == 'l') pick(sel + 1);
        if (k == 'h') pick(sel > 0 ? sel - 1 : 0);
        if (k == 'j') pick(sel < 0 ? 0 : sel + cols < nent ? sel + cols : sel);
        if (k == 'k') pick(sel < 0 ? 0 : sel >= cols ? sel - cols : sel);
        if (k == ' ' || k == 6) turn(rows > 1 ? rows - 1 : 1);
        if (k == 'b' || k == 2) turn(-(rows > 1 ? rows - 1 : 1));
        if (k == '\n' || k == '\r') act(sel, 0);
        if (k == 'e') act(sel, 1);
        if (k == 'R') file(2);
        if (k == 'D') file(3);
        if (k == 'M') file(4);
        if (k == 'N') file(5);
        if (k == 'X') file(6);
        if (k == 'u' || k == 8 || k == 127) go("..");
        if (k == 'r') after(sel >= 0 ? ent[sel].name : 0);
    }
    gclose();
    return 0;
}
