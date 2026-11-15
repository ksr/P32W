/* view -- a file looked at: a picture or text, in a window of the window manager or on the whole of the
 * video card's screen (view(1), D125).
 *
 *   view file
 *
 * A picture is a PBM file of the packed kind (it begins P4: a width and a height, then the dots a line at a
 * time, the top line first, eight dots a byte, a 1 a black dot); wm's Snapshot makes them, and so do many
 * programs elsewhere. Anything else is shown as text, 8 by 16 dots a character, long lines folded.
 *
 * Under wm the window is the picture's size (as much of it as fits), or 78 columns by 24 lines of text.
 * What does not fit is moved to with the keys, and text with the mouse on the bar at the right as well.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <gfx.h>

#define SB 12                           /* the scroll bar's width */

static const char *name;
static int fd;
static volatile int stop;
static void sig(int n) { (void)n; stop = 1; }

/* a key, or the mouse: arrows come as ESC [ A to D and are told as the letters that do the same */
static int input(int *x, int *y, int *b)
{
    int k = ginput(x, y, b, -1);
    if (k != 27) return k;
    if ((k = ginput(x, y, b, 5)) != '[') return k > 0 && k < G_MOUSE ? k : 27;
    k = ginput(x, y, b, 5);
    return k == 'A' ? 'k' : k == 'B' ? 'j' : k == 'C' ? 'l' : k == 'D' ? 'h' : 0;
}

/* ---------------------------------------------------------------- a picture */
static unsigned short *bits;            /* its dots: wpl words a line, the top line first, a word's top bit its */
static int pw, ph, wpl;                 /* left-hand dot, a 1 a lit dot */

static int number(void)                 /* the next number of the file's heading (a # begins a comment), or -1 */
{
    unsigned char c;
    int v = 0, any = 0;
    for (;;) {
        if (read(fd, &c, 1) != 1) return any ? v : -1;
        if (c == '#' && !any) { while (read(fd, &c, 1) == 1 && c != '\n') ; continue; }
        if (c >= '0' && c <= '9') { if (v > 100000) return -1; v = v * 10 + (c - '0'); any = 1; continue; }
        if (any) return v;              /* (the one space after the last number is taken with it) */
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') return -1;
    }
}

static int load(void)                   /* the picture read: 0, or -1 (and why is said) */
{
    unsigned char *row;
    int y, k, bpl;
    pw = number(); ph = number();
    if (pw < 1 || ph < 1 || pw > 4096 || ph > 4096) { fprintf(stderr, "view: %s: not a picture this can show\n", name); return -1; }
    wpl = (pw + 15) / 16; bpl = (pw + 7) / 8;
    bits = calloc(wpl * ph, sizeof *bits);
    row = malloc(bpl + 1);
    if (!bits || !row) { fprintf(stderr, "view: %s: no room for a picture of %d by %d\n", name, pw, ph); return -1; }
    for (y = 0; y < ph; y++) {
        if (read(fd, row, bpl) != bpl) { fprintf(stderr, "view: %s: it ends before its last line\n", name); break; }
        row[bpl] = 0;
        for (k = 0; k < wpl; k++) bits[y * wpl + k] = (unsigned short)~(row[2 * k] << 8 | (2 * k + 1 < bpl ? row[2 * k + 1] : 0xFF));
        if (pw & 15) bits[y * wpl + wpl - 1] &= (unsigned short)(0xFFFF << (16 - (pw & 15)));     /* (past its edge: out) */
    }
    free(row);
    return 0;
}

static void picture(void)
{
    static unsigned short part[8 * 40];
    int vw, vh, x0, ytop, ox = 0, oy = 0, k, y, r, n, j, w;
    gsize(pw > 640 ? 640 : pw < 64 ? 64 : pw, ph > 440 ? 440 : ph < 32 ? 32 : ph);
    if (gopen() < 0) { perror("view: /dev/video"); return; }
    vw = pw < gwidth ? pw : gwidth; vh = ph < gheight ? ph : gheight;   /* what shows of it, */
    x0 = (gwidth - vw) / 2; ytop = gheight - 1 - (gheight - vh) / 2;    /* in the middle of what there is */
    w = (vw + 15) / 16;
    while (!stop) {
        for (y = 0; y < vh; y += r) {   /* the part from ox, oy of the picture, eight lines at a time */
            r = vh - y < 8 ? vh - y : 8;
            for (n = j = 0; j < r; j++)
                for (k = 0; k < w; k++) part[n++] = ox / 16 + k < wpl ? bits[(oy + y + j) * wpl + ox / 16 + k] : 0;
            gwrite(x0, ytop - y - r + 1, x0 + vw - 1, ytop - y, part, G_COPY);
        }
        gflush();
        for (;;) {                      /* the keys: q, and h j k l or the arrows where there is more than shows */
            int nx = ox, ny = oy;
            k = input(0, 0, 0);
            if (k == 'q' || k == 'Q' || k < 0 || stop) { stop = 1; break; }
            if (k == 'h') nx -= 64;
            if (k == 'l') nx += 64;
            if (k == 'k') ny -= 64;
            if (k == 'j') ny += 64;
            if (nx > pw - vw) nx = (pw - vw + 15) & ~15;
            if (ny > ph - vh) ny = ph - vh;
            if (nx < 0) nx = 0;
            if (ny < 0) ny = 0;
            if (nx != ox || ny != oy) { ox = nx; oy = ny; break; }
        }
    }
    gclose();
}

/* ---------------------------------------------------------------- text */
static int *start;                      /* where in the file each line shown begins (a long line is several) */
static int nline, room, cols, rows, first;

static void mark(int at)
{
    if (nline == room) {
        int *p = realloc(start, (room ? 2 * room : 1024) * sizeof *start);
        if (!p) return;                 /* (no room for more: what there is is shown) */
        start = p; room = room ? 2 * room : 1024;
    }
    start[nline++] = at;
}

static void lines(void)                 /* the file gone through once: where its lines begin, as they are shown */
{
    unsigned char buf[512];
    int n, k, at = 0, col = 0, fresh = 1;
    lseek(fd, 0, 0);
    while ((n = read(fd, buf, sizeof buf)) > 0)
        for (k = 0; k < n; k++, at++) {
            int c = buf[k], wide = c == '\t' ? 8 - (col & 7) : 1;
            if (fresh) { mark(at); fresh = 0; col = 0; }
            if (c == '\n') { fresh = 1; continue; }
            if (col + wide > cols) { mark(at); col = 0; wide = c == '\t' ? 8 : 1; }      /* folded */
            col += wide;
        }
    mark(at);                           /* (the end: where the last line stops) */
    nline--;
}

static void bar(void)                   /* the bar at the right: its box is as much of the file as shows, where it is */
{
    int x = gwidth - SB, h = gheight - 2, top, len;
    gink(G_BLACK); gfbox(x, 0, gwidth - 1, gheight - 1);
    gink(G_WHITE); gbox(x, 0, gwidth - 1, gheight - 1);
    if (nline <= rows) { top = 0; len = h; }
    else { len = h * rows / nline; if (len < 6) len = 6; top = (h - len) * first / (nline - rows); }
    gfbox(x + 2, gheight - 2 - top - len + 1, gwidth - 3, gheight - 2 - top);
}

static void page(void)                  /* the lines from `first`, each written over what was there */
{
    char out[100];
    unsigned char in[100];
    int r, k, n, col;
    glook(G_OPAQUE);
    for (r = 0; r < rows; r++) {
        col = 0;
        if (first + r < nline) {
            n = start[first + r + 1] - start[first + r];
            if (n > (int)sizeof in) n = sizeof in;
            lseek(fd, start[first + r], 0);
            n = read(fd, in, n);
            for (k = 0; k < n && col < cols; k++) {
                if (in[k] == '\n' || in[k] == '\r') continue;
                if (in[k] == '\t') { do out[col++] = ' '; while ((col & 7) && col < cols); continue; }
                out[col++] = in[k] >= 0x20 && in[k] <= 0x7E ? (char)in[k] : '.';
            }
        }
        while (col < cols) out[col++] = ' ';
        out[col] = 0;
        gtext(2, gheight - 16 * (r + 1), out);
    }
    glook(0);
    bar();
    gflush();
}

static void text(void)
{
    int x, y, b, was = 0, k, to, mouse;
    gsize(640, 16 * 24);                /* (the widest there is: 78 columns beside the bar) */
    if (gopen() < 0) { perror("view: /dev/video"); return; }
    cols = (gwidth - 4 - SB) / 8; rows = gheight / 16;
    if (cols > 96) cols = 96;
    lines();
    mouse = getenv("WM") && gmopen() == 0;      /* (on the screen itself there is no pointer to aim with) */
    page();
    while (!stop) {
        k = input(&x, &y, &b);
        if (k < 0 || k == 'q' || k == 'Q') break;
        to = first;
        if (k == G_MOUSE) {             /* a press on the bar: above its middle a page back, below it a page on */
            if (mouse && (b & 1) && !was && x >= gwidth - SB) to += y >= gheight / 2 ? -(rows - 1) : rows - 1;
            was = b & 1;
        }
        if (k == ' ' || k == 'f' || k == 6) to += rows - 1;
        if (k == 'b' || k == 2) to -= rows - 1;
        if (k == 'j' || k == '\n' || k == '\r') to++;
        if (k == 'k') to--;
        if (k == 'g') to = 0;
        if (k == 'G') to = nline;
        if (to > nline - rows) to = nline - rows;
        if (to < 0) to = 0;
        if (to != first) { first = to; page(); }
    }
    gclose();
}

int main(int argc, char **argv)
{
    unsigned char m[3];
    int n;
    if (argc != 2) { fprintf(stderr, "usage: view file\n"); return 2; }
    name = argv[1];
    if ((fd = open(name, O_RDONLY)) < 0) { perror(name); return 1; }
    signal(SIGINT, sig); signal(SIGTERM, sig); signal(SIGHUP, sig);
    n = read(fd, m, 3);
    if (n == 3 && m[0] == 'P' && m[1] == '4' && (m[2] == '\n' || m[2] == ' ' || m[2] == '\t' || m[2] == '\r')) {
        if (load() < 0) return 1;
        picture();
    } else text();
    return 0;
}
