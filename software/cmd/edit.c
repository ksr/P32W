/* edit -- a text file in a window, typed into and changed, with the mouse to say where (edit(1), D145). It is
 * P8X's write brought here: there write is the editor; on this machine write(1) is the command that writes to
 * another user's terminal, so the editor has another name. Under the window manager it has a window of its
 * own; with none it takes the whole of the video card's screen, and the keys alone work it.
 *
 *   edit [file]
 *
 * What is typed goes in where the cursor is. A press of the mouse puts the cursor there; a press on the bar at
 * the right goes a page back or on. The words on the top line are menus, let down while the button is held:
 * File has Save, Save as and Close; Edit has Cut line, Paste, Find and Find again. The keys: the arrows move,
 * ^A and ^E go to a line's ends, ^Y and ^V a page back and on, Backspace takes the character before the cursor,
 * ^D the one under it, ^K cuts the line (several, one after another, are kept together), ^U puts what was cut
 * back in, ^W finds, ^G finds again, ^O saves, ^X ends (it asks first if the text was changed).
 *
 * The text is in one piece of memory with a gap in it where the cursor is: typing fills the gap, and going
 * somewhere else moves it there.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <gfx.h>

#define SB   12                         /* the scroll bar's width */
#define TOP  16                         /* the line at the top: the menus' words and the file's name */
#define FOOT 18                         /* the line at the foot, and the rule over it */
#define LM   4                          /* dots left of the text */

static char *buf;                       /* the text, with a gap in it: buf[gs] to buf[ge - 1] */
static int size, gs, ge;                /* (the cursor is at the gap: place gs of the text) */
static int ln = 1;                      /* the line the cursor is on, counted from 1 */
static int top, left;                   /* the place the window's first line begins at; the first column that shows */
static int rows, cols, want = -1;       /* want: the column kept to, going up and down (-1: where it is) */
static char name[128], note[80], what[40];
static char *cut;                       /* the lines last cut */
static int ncut, cutting;               /* cutting: the last thing done was a cut (the next one is added to it) */
static int changed, shown, cx, cy;      /* shown: the cursor is on the screen, at cx, cy */
static int resized, mouse;
static volatile int stop;
static void sig(int n) { (void)n; stop = 1; }

/* ---------------------------------------------------------------- the text */
static int len(void) { return size - (ge - gs); }
static int at(int p) { return (unsigned char)(p < gs ? buf[p] : buf[p + ge - gs]); }

static void go(int p)                   /* the cursor, and the gap with it, to place p */
{
    int k;
    if (p < gs) {
        for (k = p; k < gs; k++) if (buf[k] == '\n') ln--;
        memmove(buf + ge - (gs - p), buf + p, gs - p);
        ge -= gs - p; gs = p;
    } else if (p > gs) {
        for (k = ge; k < ge + p - gs; k++) if (buf[k] == '\n') ln++;
        memmove(buf + gs, buf + ge, p - gs);
        ge += p - gs; gs = p;
    }
}

static int room(int n)                  /* a gap of n characters or more: 0 if there is no memory for it */
{
    char *b;
    int more = size + n + 4096, tail = size - ge;
    if (ge - gs >= n) return 1;
    if (!(b = realloc(buf, more))) return 0;
    buf = b;
    memmove(buf + more - tail, buf + ge, tail);
    ge = more - tail; size = more;
    return 1;
}

static int bol(int p) { while (p > 0 && at(p - 1) != '\n') p--; return p; }    /* the beginning of p's line, */
static int eol(int p) { int n = len(); while (p < n && at(p) != '\n') p++; return p; }  /* ... and its end */

static int colof(int p)                 /* the column place p shows in (a tab goes on to the next multiple of 8) */
{
    int q = bol(p), c = 0;
    for (; q < p; q++) c = at(q) == '\t' ? (c | 7) + 1 : c + 1;
    return c;
}

static int tocol(int b, int col)        /* in the line that begins at b: the place that shows in column col, or its end */
{
    int c = 0, n = len(), w;
    while (b < n && at(b) != '\n') {
        w = at(b) == '\t' ? (c | 7) + 1 - c : 1;
        if (c + w > col) break;
        c += w; b++;
    }
    return b;
}

static int put(const char *s, int n)    /* n characters in at the cursor: 0 if there is no room */
{
    int k;
    if (!room(n)) { strcpy(note, "no memory for more"); return 0; }
    for (k = 0; k < n; k++) if (s[k] == '\n') ln++;
    memcpy(buf + gs, s, n);
    gs += n;
    changed = 1;
    return 1;
}

static void load(void)                  /* the file read in, the cursor at its beginning */
{
    int fd = name[0] ? open(name, O_RDONLY) : -1, n;
    if (fd < 0) { if (name[0]) sprintf(note, errno == ENOENT ? "a new file" : "cannot be read: %.40s", strerror(errno)); return; }
    while (room(4096) && (n = read(fd, buf + gs, ge - gs)) > 0) gs += n;
    close(fd);
    go(0);
    ln = 1;
    sprintf(note, "%d characters", len());
}

static int save(void)                   /* the text written to its file: 0, or -1 (said in the note) */
{
    int fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0666), bad;
    if (fd < 0) { sprintf(note, "%.40s: %.30s", name, strerror(errno)); return -1; }
    bad = write(fd, buf, gs) != gs || write(fd, buf + ge, size - ge) != size - ge;
    if (close(fd) < 0 || bad) { sprintf(note, "%.40s: not all written", name); return -1; }
    changed = 0;
    sprintf(note, "%d characters written", len());
    return 0;
}

/* ---------------------------------------------------------------- the picture */
static int ry(int r) { return gheight - TOP - 16 * (r + 1); }  /* the bottom dot of the window's line r */

static void head(void)                  /* the menus' words, the file's name and a mark if it was changed */
{
    char s[100];
    int room_ = (gwidth - 104) / 8 - 1, n = strlen(name);
    gink(G_WHITE); gfbox(0, gheight - TOP, gwidth - 1, gheight - 1);
    gink(G_BLACK); gtext(8, gheight - TOP, "File"); gtext(56, gheight - TOP, "Edit");
    if (room_ > 2) {
        if (room_ > 90) room_ = 90;
        sprintf(s, "%.*s%s", room_ - 2, !n ? "(no name)" : n > room_ - 2 ? name + n - (room_ - 2) : name, changed ? " *" : "");
        gtext(104, gheight - TOP, s);
    }
    gink(G_WHITE);
}

static void line(const char *t)         /* the line at the foot says t */
{
    char s[120];
    int k, room_ = (gwidth - 4) / 8;
    if (room_ > (int)sizeof s - 1) room_ = sizeof s - 1;
    for (k = 0; k < room_ && t[k]; k++) s[k] = t[k];
    for (; k < room_; k++) s[k] = ' ';
    s[room_] = 0;
    glook(G_OPAQUE); gtext(4, 0, s); glook(0);
    gline(0, FOOT - 1, gwidth - 1, FOOT - 1);
}

static void foot(void)                  /* where the cursor is, and the note if there is one */
{
    char s[160];
    sprintf(s, "line %d, column %d%s%s", ln, colof(gs) + 1, note[0] ? "   " : "", note);
    line(s);
}

static void bar(void)                   /* the bar at the right: its box is where in the text the window is */
{
    int x = gwidth - SB, y0 = FOOT + 1, y1 = gheight - TOP - 2, h = y1 - y0 - 1, n = len(), topd, lend, p, r;
    for (p = top, r = 0; r < rows && p < n; r++) p = eol(p) + 1;        /* (p: the place after the last line shown) */
    if (p > n) p = n;
    gink(G_BLACK); gfbox(x, y0, gwidth - 1, y1);
    gink(G_WHITE); gbox(x, y0, gwidth - 1, y1);
    if (n == 0 || (top == 0 && p == n)) { topd = 0; lend = h; }
    else {
        lend = h * (p - top) / n;
        if (lend < 6) lend = 6;
        topd = n > p - top ? (h - lend) * top / (n - (p - top)) : 0;
    }
    if (topd > h - lend) topd = h - lend;
    gfbox(x + 2, y1 - 1 - topd - lend + 1, gwidth - 3, y1 - 1 - topd);
}

static void curs(int on)                /* the cursor taken off the screen, and (on) put where it is now */
{
    int r, p;
    if (shown) { gink(G_XOR); gfbox(cx, cy, cx + 7, cy + 15); gink(G_WHITE); shown = 0; }
    if (!on) return;
    for (r = 0, p = top; p < gs; p++) if (at(p) == '\n') r++;
    cx = LM + (colof(gs) - left) * 8; cy = ry(r);
    if (r >= rows || cx < LM || cx > LM + (cols - 1) * 8) return;
    gink(G_XOR); gfbox(cx, cy, cx + 7, cy + 15); gink(G_WHITE);
    shown = 1;
}

static void draw(int r, int p)          /* the window's line r: the text's line that begins at p (-1: none) */
{
    char s[100];
    int c = 0, n = len(), ch;
    memset(s, ' ', cols); s[cols] = 0;
    while (p >= 0 && p < n && (ch = at(p)) != '\n' && c < left + cols) {
        p++;
        if (ch == '\t') { c = (c | 7) + 1; continue; }
        if (c >= left) s[c - left] = ch < 32 || ch > 126 ? '?' : ch;
        c++;
    }
    glook(G_OPAQUE); gtext(LM, ry(r), s); glook(0);
}

static void text(int from)              /* the window's lines from line `from` down */
{
    int r, p = top, n = len();
    for (r = 0; r < rows; r++) {
        if (r >= from) draw(r, p <= n ? p : -1);
        p = p < n ? eol(p) + 1 : n + 1;
    }
}

static int row(void)                    /* the window's line the cursor is on (past the last, if it does not show) */
{
    int r = 0, p;
    for (p = top; p < gs; p++) if (at(p) == '\n') r++;
    return r;
}

static int fit(void)                    /* the window's lines and columns, for the size the picture is: 0 if too small */
{
    cols = (gwidth - SB - LM) / 8; rows = (gheight - TOP - FOOT - 1) / 16;
    if (cols > 96) cols = 96;
    return cols >= 16 && rows >= 2;
}

/* the cursor made to show: the window's first line and first column moved if they must be. 1 if they were */
static int see(void)
{
    int wt = top, wl = left, r, c = colof(gs);
    if (gs < top) top = bol(gs);
    for (r = row(); r >= rows; r--) top = eol(top) + 1;
    if (c < left) left = c > 16 ? (c - 16) & ~7 : 0;
    if (c >= left + cols) left = (c - cols + 16) & ~7;
    return top != wt || left != wl;
}

/* the screen put right after something was done. how: 0 the cursor alone moved, 1 its line changed, 2 the lines
   from its line down, 3 everything */
static void show(int how)
{
    static int wasch = -1;
    curs(0);
    if (see()) how = 3;
    if (how == 3) { text(0); bar(); }
    else if (how == 2) { text(row()); bar(); }
    else if (how == 1) draw(row(), bol(gs));
    if (changed != wasch || how == 3) { head(); wasch = changed; }
    foot();
    note[0] = 0;
    curs(1);
    gflush();
}

static void all(void) { resized = 0; shown = 0; gclear(); head(); show(3); }

/* ---------------------------------------------------------------- menus, and things asked for (as the finder's) */
static int menu(int x, char **item, int n)
{
    int w = 0, k, x0, y0, y1 = gheight - TOP - 1, kept, on = -1, mx, my, b, e;
    for (k = 0; k < n; k++) if ((int)strlen(item[k]) > w) w = strlen(item[k]);
    w = 8 * w + 16;
    x0 = x + w > gwidth ? gwidth - w : x;
    if (x0 < 0) x0 = 0;
    y0 = y1 - 16 * n - 3;
    curs(0);
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
    if (kept >= 0) { gstamp(kept, x0, y0, G_COPY); gdrop(kept); }
    else resized = 1;                   /* (no room to keep it: all drawn again) */
    return on;
}

/* something asked for on the line at the foot: what is typed shows there. Return ends it: 1. ESC, or a press of
   the mouse, gives it up: 0 */
static int ask(const char *q, char *s, int max)
{
    char t[200];
    int k, n = strlen(s), b;
    curs(0);
    for (;;) {
        sprintf(t, "%s: %s_", q, s);
        line(t); gflush();
        k = ginput(0, 0, &b, -1);
        if (k == G_SIZE) { resized = 1; return 0; }
        if (k == G_MOUSE) { if (b & 1) return 0; continue; }
        if (k <= 0 || stop) return 0;
        if (k == 27) { while (ginput(0, 0, 0, 5) > 0) ; return 0; }     /* (an arrow's tail goes with it) */
        if (k == '\n' || k == '\r') return n > 0;
        if (k == 8 || k == 127) { if (n > 0) s[--n] = 0; continue; }
        if (k == 21) { s[n = 0] = 0; continue; }                        /* (^U: all of it) */
        if (k >= 32 && k < 127 && n < max) { s[n++] = k; s[n] = 0; }
    }
}

static int key1(const char *q)          /* a question at the foot, answered by one key: the key (0: given up) */
{
    int k;
    curs(0);
    line(q); gflush();
    do k = ginput(0, 0, 0, -1); while (k == G_MOUSE);
    if (k == G_SIZE) { resized = 1; return 0; }
    if (k == 27) { while (ginput(0, 0, 0, 5) > 0) ; return 0; }
    return k > 0 ? k : 0;
}

/* ---------------------------------------------------------------- what is done to the text */
static int writeout(int as)             /* saved: under another name if there is none yet, or (as) one is wanted */
{
    char s[128];
    if (as || !name[0]) {
        strcpy(s, name);
        if (!ask("Save as", s, 120)) return -1;
        strcpy(name, s);
    }
    return save();
}

static void find(int again)             /* the cursor to the next place the text asked for is, from the top again at the end */
{
    int n = len(), m, p, k, tried;
    if (!again || !what[0]) { if (!ask("Find", what, 38)) return; }
    m = strlen(what);
    for (p = gs + 1, tried = 0; tried <= n; p++, tried++) {
        if (p + m > n) { p = -1; continue; }            /* (the end: on from the beginning) */
        for (k = 0; k < m && at(p + k) == (unsigned char)what[k]; k++) ;
        if (k == m) { go(p); return; }
    }
    sprintf(note, "no %.38s", what);
}

static void cutline(void)               /* the cursor's line taken out and kept; added to the last, if that was cut just now */
{
    int b = bol(gs), e = eol(gs), n = len(), k;
    char *c;
    if (e < n) e++;
    if (e == b) return;
    if (!cutting) ncut = 0;
    if (!(c = realloc(cut, ncut + e - b + 1))) { strcpy(note, "no memory to keep it"); return; }
    cut = c;
    go(b);
    for (k = 0; k < e - b; k++) cut[ncut++] = buf[ge + k];
    ge += e - b;                        /* (the gap takes the line in) */
    changed = 1;
}

static void place(int x, int y)         /* the cursor to where the mouse was pressed */
{
    int r = (gheight - TOP - 1 - y) / 16, p = top, n = len();
    while (r-- > 0 && p < n) { p = eol(p); if (p < n) p++; else break; }
    go(tocol(bol(p), (x < LM ? 0 : x - LM) / 8 + left));
}

static void updown(int by)              /* by lines up (below 0) or down, keeping to a column */
{
    int p = gs;
    if (want < 0) want = colof(gs);
    for (; by < 0; by++) { p = bol(p); if (p == 0) break; p--; }
    for (; by > 0; by--) { p = eol(p); if (p >= len()) break; p++; }
    go(tocol(bol(p), want));
}

int main(int argc, char **argv)
{
    static char *filem[] = { "Save        ^O", "Save as", "Close       ^X" };
    static char *editm[] = { "Cut line    ^K", "Paste       ^U", "Find        ^W", "Find again  ^G" };
    int x, y, b, was = 0, k, how, keep, c;
    char ch;
    if (argc > 2) { fprintf(stderr, "usage: edit [file]\n"); return 2; }
    if (argc > 1) { strncpy(name, argv[1], sizeof name - 1); }
    if (!room(4096)) { fprintf(stderr, "edit: no memory\n"); return 1; }
    load();
    gsize(8 * 72 + LM + SB, 16 * 22 + TOP + FOOT + 1);  /* (72 columns, 22 lines) */
    if (gopen() < 0) { perror("edit: the screen"); return 1; }
    mouse = getenv("WM") && gmopen() == 0;      /* (on the screen itself there is no pointer to see: the keys) */
    signal(SIGINT, sig); signal(SIGTERM, sig); signal(SIGHUP, sig);
    if (!fit()) { gclose(); fprintf(stderr, "edit: the window is too small\n"); return 1; }
    gmaysize(8 * 20 + LM + SB, 16 * 4 + TOP + FOOT + 1, 8, 16); /* (its corner: 20 columns and 4 lines or more) */
    all();
    while (!stop) {
        if (resized) { if (fit()) all(); else resized = 0; }
        k = ginput(&x, &y, &b, -1);
        if (k < 0) break;
        if (k == G_SIZE) { resized = 1; continue; }
        how = 0; keep = 0; c = 0;       /* (keep: the column kept to and the cutting go on; c: a command, as its key) */
        if (k == G_MOUSE) {
            if (!(b & 1) || was) { was = b & 1; continue; }
            was = 1;
            if (y >= gheight - TOP) {
                if (x >= 4 && x < 44) { k = menu(4, filem, 3); c = k == 0 ? 15 : k == 1 ? 1000 : k == 2 ? 24 : 0; was = 0; }
                else if (x >= 52 && x < 92) { k = menu(52, editm, 4); c = k == 0 ? 11 : k == 1 ? 21 : k == 2 ? 23 : k == 3 ? 7 : 0; was = 0; }
                if (!c) { if (!resized) { curs(1); gflush(); } continue; }
            } else if (x >= gwidth - SB) c = y >= gheight / 2 ? 25 : 22;
            else if (y >= FOOT) { place(x, y); want = -1; cutting = 0; show(0); continue; }
            else continue;
        } else if (k == 27) {           /* an arrow or a key of the same kind: ESC [ and a letter, or digits and ~ */
            if ((k = ginput(0, 0, 0, 5)) != '[') continue;
            k = ginput(0, 0, 0, 5);
            if (k >= '0' && k <= '9') { c = k; k = ginput(0, 0, 0, 5); c = c == '3' ? 4 : c == '5' ? 25 : c == '6' ? 22 : c == '1' ? 1 : c == '4' ? 5 : 0; }
            else c = k == 'A' ? 1001 : k == 'B' ? 1002 : k == 'C' ? 1003 : k == 'D' ? 1004 : k == 'H' ? 1 : k == 'F' ? 5 : 0;
            if (!c) continue;
        } else if (k >= 32 && k < 127) { ch = k; if (put(&ch, 1)) how = 1; }
        else if (k == '\n' || k == '\r') { ch = '\n'; if (put(&ch, 1)) how = 2; }
        else if (k == '\t') { ch = '\t'; if (put(&ch, 1)) how = 1; }
        else if (k == 8 || k == 127) {
            if (gs > 0) { how = buf[gs - 1] == '\n' ? 2 : 1; if (how == 2) ln--; gs--; changed = 1; }
        } else c = k;
        switch (c) {
        case 0: break;
        case 1001: updown(-1); keep = 1; break;
        case 1002: updown(1); keep = 1; break;
        case 1003: if (gs < len()) go(gs + 1); break;
        case 1004: if (gs > 0) go(gs - 1); break;
        case 1: go(bol(gs)); break;                     /* ^A */
        case 5: go(eol(gs)); break;                     /* ^E */
        case 25: updown(-(rows - 1)); keep = 1; break;  /* ^Y */
        case 22: updown(rows - 1); keep = 1; break;     /* ^V */
        case 4: if (gs < len()) { how = buf[ge] == '\n' ? 2 : 1; ge++; changed = 1; } break;    /* ^D */
        case 11: cutline(); how = 2; keep = 2; break;   /* ^K */
        case 21: if (ncut && put(cut, ncut)) how = 2; break;    /* ^U */
        case 23: find(0); break;                        /* ^W */
        case 7: find(1); break;                         /* ^G */
        case 15: writeout(0); break;                    /* ^O */
        case 1000: writeout(1); how = 3; break;
        case 24:                                        /* ^X */
            if (changed) {
                k = key1("Save it before ending? y, n (ESC: go on)");
                if (k == 'y' || k == 'Y') { if (writeout(0) == 0) stop = 1; }
                else if (k == 'n' || k == 'N') stop = 1;
            } else stop = 1;
            break;
        }
        if (stop) break;
        if (keep != 1) want = -1;
        cutting = keep == 2;
        if (resized) continue;
        show(how);
    }
    if (mouse) gmclose();
    gclose();
    return 0;
}
