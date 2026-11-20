/* paint -- a drawing made with the mouse in a window of the window manager: lines, boxes, ovals, filled or not,
 * and lines drawn by hand (paint(1), D146). It is P8X's paint brought here. As there, the drawing is a list of
 * shapes and not a sheet of dots: taking the last one back is taking it off the list and drawing the rest
 * again, and a drawing is saved as its list, to be opened and gone on with.
 *
 *   paint [file]
 *
 * Under the top line is a strip of boxes: six tools (the pen, a line, a box, a filled box, an oval, a filled
 * oval) and five inks (white, black, and three patterns for filling). A press in a box chooses it. On the
 * sheet below, the button held down draws: the pen as the mouse goes, the others as an outline that follows
 * the mouse and is put down when the button comes up.
 *
 * The words on the top line are menus: File has New, Open, Save, Save as, Save picture (the sheet as a PBM
 * file, for view and for other machines) and Close; Edit has Undo. The keys: u takes the last shape back,
 * ^O saves, ^X ends (it asks first if the drawing was changed), and p l b B o O choose the tools.
 *
 * This machine's screen is one bit a dot, so P8X's eight colours are white, black and patterns here. A program
 * in a window cannot read its dots back, so there is no filling up to a border, and Save picture draws the
 * list again into memory, with lines and ovals of its own.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <gfx.h>

#define TOP   16                        /* the line at the top: the menus' words and the file's name */
#define STRIP 28                        /* the strip of tools and inks under it */
#define FOOT  18                        /* the line at the foot, and the rule over it */
#define BW    28                        /* a box of the strip: across */
#define NTOOL 6
#define NINK  5
#define INKX  (NTOOL * BW + 16)         /* where the inks' boxes begin */

enum { PEN, LINE, BOX, FBOX, OVAL, FOVAL };

/* a shape: its kind, its ink, and two corners (dots from the sheet's top-left corner, y downwards); the pen's
   has its places in pt[] instead, npt of them from p0 */
struct shape { char kind, ink; short x0, y0, x1, y1; int p0, npt; };
static struct shape *sh;
static short *pt;
static int nsh, shroom, npt, ptroom;
static int tool = LINE, ink;            /* the tool and the ink chosen */
static int cw, ch;                      /* the sheet: across and down */
static char name[128], note[80];
static int changed, resized;
static volatile int stop;
static void sig(int n) { (void)n; stop = 1; }

/* the inks: all white, all black (which takes away), and three patterns: one dot in two, one in four, lines */
static const unsigned short pats[NINK][16] = {
    { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF },
    { 0 },
    { 0xAAAA, 0x5555, 0xAAAA, 0x5555, 0xAAAA, 0x5555, 0xAAAA, 0x5555, 0xAAAA, 0x5555, 0xAAAA, 0x5555, 0xAAAA, 0x5555, 0xAAAA, 0x5555 },
    { 0x8888, 0x0000, 0x2222, 0x0000, 0x8888, 0x0000, 0x2222, 0x0000, 0x8888, 0x0000, 0x2222, 0x0000, 0x8888, 0x0000, 0x2222, 0x0000 },
    { 0x8888, 0x4444, 0x2222, 0x1111, 0x8888, 0x4444, 0x2222, 0x1111, 0x8888, 0x4444, 0x2222, 0x1111, 0x8888, 0x4444, 0x2222, 0x1111 },
};

/* ---------------------------------------------------------------- the list */
static struct shape *more(void)         /* room for one more shape: where it goes, or 0 */
{
    if (nsh == shroom) {
        struct shape *s = realloc(sh, (shroom + 256) * sizeof *sh);
        if (!s) { strcpy(note, "no memory for more"); return 0; }
        sh = s; shroom += 256;
    }
    return &sh[nsh];
}

static int place(int x, int y)          /* a place of the pen's kept: 0 if there is no memory */
{
    if (npt + 2 > ptroom) {
        short *p = realloc(pt, (ptroom + 2048) * sizeof *pt);
        if (!p) { strcpy(note, "no memory for more"); return 0; }
        pt = p; ptroom += 2048;
    }
    pt[npt++] = x; pt[npt++] = y;
    return 1;
}

/* ---------------------------------------------------------------- drawing a shape on the screen */
static int sy(int y) { return gheight - TOP - STRIP - 1 - y; }  /* the screen's line for the sheet's line y */

static void sheet(int on) { if (on) gclip(0, FOOT, cw - 1, FOOT + ch - 1); else gclip(0, 0, gwidth - 1, gheight - 1); }

static void filled(int oval, int x0, int y0, int x1, int y1, int k)     /* a filled box or oval in ink k */
{
    int cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, rx = abs(x1 - x0) / 2, ry = abs(y1 - y0) / 2;
    if (k > 1) {                        /* a pattern: the ground put out first, then its dots lit */
        gink(G_BLACK);
        if (oval) gfellipse(cx, sy(cy), rx, ry); else gfbox(x0, sy(y0), x1, sy(y1));
        gpattern(pats[k]);
    }
    gink(k == 1 ? G_BLACK : G_WHITE);
    if (oval) gfellipse(cx, sy(cy), rx, ry); else gfbox(x0, sy(y0), x1, sy(y1));
    gpattern(0);
    gink(G_WHITE);
}

static void outline(int kind, int x0, int y0, int x1, int y1)   /* a line, a box or an oval in the ink that is set */
{
    if (kind == LINE) gline(x0, sy(y0), x1, sy(y1));
    else if (kind == BOX || kind == FBOX) gbox(x0, sy(y0), x1, sy(y1));
    else gellipse((x0 + x1) / 2, sy((y0 + y1) / 2), abs(x1 - x0) / 2, abs(y1 - y0) / 2);
}

static void draw(const struct shape *s)
{
    int k;
    if (s->kind == PEN) {
        gink(s->ink == 1 ? G_BLACK : G_WHITE);
        gmove(pt[s->p0], sy(pt[s->p0 + 1]));
        if (s->npt == 1) gdraw(pt[s->p0], sy(pt[s->p0 + 1]));
        for (k = 1; k < s->npt; k++) gdraw(pt[s->p0 + 2 * k], sy(pt[s->p0 + 2 * k + 1]));
    } else if (s->kind == FBOX || s->kind == FOVAL) filled(s->kind == FOVAL, s->x0, s->y0, s->x1, s->y1, s->ink);
    else { gink(s->ink == 1 ? G_BLACK : G_WHITE); outline(s->kind, s->x0, s->y0, s->x1, s->y1); }
    gink(G_WHITE);
}

/* ---------------------------------------------------------------- the picture */
static void head(void)                  /* the menus' words, the file's name and a mark if the drawing was changed */
{
    char s[100];
    int room = (gwidth - 104) / 8 - 1, n = strlen(name);
    gink(G_WHITE); gfbox(0, gheight - TOP, gwidth - 1, gheight - 1);
    gink(G_BLACK); gtext(8, gheight - TOP, "File"); gtext(56, gheight - TOP, "Edit");
    if (room > 2) {
        if (room > 90) room = 90;
        sprintf(s, "%.*s%s", room - 2, !n ? "(no name)" : n > room - 2 ? name + n - (room - 2) : name, changed ? " *" : "");
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

static void foot(void)
{
    static const char *const tn[NTOOL] = { "the pen", "a line", "a box", "a filled box", "an oval", "a filled oval" };
    char s[160];
    sprintf(s, "%s, %d shape%s%s%s", tn[tool], nsh, nsh == 1 ? "" : "s", note[0] ? "   " : "", note);
    line(s);
    note[0] = 0;
}

static void box(int k, int x)           /* box k of the strip, at x: a tool's sign or an ink's sample; lit round if chosen */
{
    int y0 = gheight - TOP - STRIP + 2, y1 = gheight - TOP - 3, on = k < NTOOL ? k == tool : k - NTOOL == ink;
    gink(G_BLACK); gfbox(x, y0 - 1, x + BW - 1, y1 + 1);
    gink(G_WHITE); gbox(x + 2, y0, x + BW - 3, y1);
    if (on) gbox(x, y0 - 2 + 1, x + BW - 1, y1 + 1);
    if (k >= NTOOL) {
        if (k - NTOOL != 1) { gpattern(pats[k - NTOOL]); gfbox(x + 5, y0 + 3, x + BW - 6, y1 - 3); gpattern(0); }
        return;
    }
    switch (k) {
    case PEN:   gmove(x + 6, y0 + 5); gdraw(x + 9, y0 + 12); gdraw(x + 13, y0 + 8); gdraw(x + 17, y0 + 15); gdraw(x + 21, y0 + 11); break;
    case LINE:  gline(x + 6, y0 + 4, x + BW - 7, y1 - 4); break;
    case BOX:   gbox(x + 6, y0 + 4, x + BW - 7, y1 - 4); break;
    case FBOX:  gfbox(x + 6, y0 + 4, x + BW - 7, y1 - 4); break;
    case OVAL:  gellipse(x + BW / 2, (y0 + y1) / 2, 8, 6); break;
    case FOVAL: gfellipse(x + BW / 2, (y0 + y1) / 2, 8, 6); break;
    }
}

static void strip(void)
{
    int k;
    gink(G_BLACK); gfbox(0, gheight - TOP - STRIP, gwidth - 1, gheight - TOP - 1); gink(G_WHITE);
    for (k = 0; k < NTOOL; k++) box(k, k * BW);
    for (k = 0; k < NINK; k++) box(NTOOL + k, INKX + k * BW);
    gline(0, gheight - TOP - STRIP, gwidth - 1, gheight - TOP - STRIP);
}

static void shapes(void)                /* the sheet: every shape of the list, in its turn */
{
    int k;
    gink(G_BLACK); gfbox(0, FOOT, cw - 1, FOOT + ch - 1); gink(G_WHITE);
    sheet(1);
    for (k = 0; k < nsh; k++) draw(&sh[k]);
    sheet(0);
}

static int fit(void)                    /* the sheet, for the size the picture is: 0 if too small */
{
    cw = gwidth; ch = gheight - TOP - STRIP - FOOT;
    return cw >= INKX + NINK * BW && ch >= 32;
}

static void all(void) { resized = 0; gclear(); head(); strip(); shapes(); foot(); gflush(); }

/* ---------------------------------------------------------------- menus, and things asked for (as the finder's) */
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

static int ask(const char *q, char *s, int max)        /* asked for at the foot. Return: 1; ESC or the mouse: 0 */
{
    char t[200];
    int k, n = strlen(s), b;
    for (;;) {
        sprintf(t, "%s: %s_", q, s);
        line(t); gflush();
        k = ginput(0, 0, &b, -1);
        if (k == G_SIZE) { resized = 1; return 0; }
        if (k == G_MOUSE) { if (b & 1) return 0; continue; }
        if (k <= 0 || stop) return 0;
        if (k == 27) { while (ginput(0, 0, 0, 5) > 0) ; return 0; }
        if (k == '\n' || k == '\r') return n > 0;
        if (k == 8 || k == 127) { if (n > 0) s[--n] = 0; continue; }
        if (k == 21) { s[n = 0] = 0; continue; }
        if (k > 32 && k < 127 && n < max) { s[n++] = k; s[n] = 0; }
    }
}

static int key1(const char *q)          /* a question at the foot, answered by one key: the key (0: given up) */
{
    int k;
    line(q); gflush();
    do k = ginput(0, 0, 0, -1); while (k == G_MOUSE);
    if (k == G_SIZE) { resized = 1; return 0; }
    if (k == 27) { while (ginput(0, 0, 0, 5) > 0) ; return 0; }
    return k > 0 ? k : 0;
}

/* ---------------------------------------------------------------- the drawing's file: a line of text a shape */
static int save(void)
{
    static const char kn[] = "PLBbOo";
    FILE *f = fopen(name, "w");
    int k, j;
    if (!f) { sprintf(note, "%.40s: %.30s", name, strerror(errno)); return -1; }
    fprintf(f, "paint 1\n");
    for (k = 0; k < nsh; k++) {
        struct shape *s = &sh[k];
        if (s->kind == PEN) {
            fprintf(f, "P %d %d", s->ink, s->npt);
            for (j = 0; j < s->npt; j++) fprintf(f, "%s%d %d", j % 8 ? " " : "\n", pt[s->p0 + 2 * j], pt[s->p0 + 2 * j + 1]);
            fprintf(f, "\n");
        } else fprintf(f, "%c %d %d %d %d %d\n", kn[(int)s->kind], s->ink, s->x0, s->y0, s->x1, s->y1);
    }
    if (ferror(f) | fclose(f)) { sprintf(note, "%.40s: not all written", name); return -1; }
    changed = 0;
    sprintf(note, "%d shape%s written", nsh, nsh == 1 ? "" : "s");
    return 0;
}

static void load(void)
{
    static const char kn[] = "PLBbOo";
    FILE *f = name[0] ? fopen(name, "r") : 0;
    char c, *p;
    int v, a, b, d, e, j;
    struct shape *s;
    nsh = npt = 0; changed = 0;
    if (!f) { if (name[0]) sprintf(note, errno == ENOENT ? "a new drawing" : "cannot be read: %.40s", strerror(errno)); return; }
    if (fscanf(f, "paint %d", &v) != 1) { fclose(f); sprintf(note, "%.40s is not a drawing", name); name[0] = 0; return; }
    while (fscanf(f, " %c", &c) == 1 && (p = strchr(kn, c)) != 0 && (s = more()) != 0) {
        s->kind = p - kn;
        if (s->kind == PEN) {
            if (fscanf(f, "%d %d", &a, &b) != 2 || b < 1) break;
            s->ink = a; s->p0 = npt; s->npt = 0;
            for (j = 0; j < b && fscanf(f, "%d %d", &d, &e) == 2 && place(d, e); j++) s->npt++;
            if (s->npt != b) break;
        } else {
            if (fscanf(f, "%d %d %d %d %d", &v, &a, &b, &d, &e) != 5) break;
            s->ink = v; s->x0 = a; s->y0 = b; s->x1 = d; s->y1 = e;
        }
        if (s->ink < 0 || s->ink >= NINK) s->ink = 0;
        nsh++;
    }
    fclose(f);
    sprintf(note, "%d shape%s", nsh, nsh == 1 ? "" : "s");
}

/* ---------------------------------------------------------------- the sheet as a picture file: drawn into memory */
static unsigned short *bm;              /* cw by ch dots, a line in bw words, the top line first */
static int bw;

static void dot(int x, int y, int k)    /* the dot x, y in ink k (a pattern's dots are lit or put out by where they are) */
{
    int on = k == 1 ? 0 : k == 0 ? 1 : (pats[k][y & 15] >> (15 - (x & 15))) & 1;
    if (x < 0 || y < 0 || x >= cw || y >= ch) return;
    if (on) bm[y * bw + (x >> 4)] |= 0x8000 >> (x & 15); else bm[y * bw + (x >> 4)] &= ~(0x8000 >> (x & 15));
}

static void mline(int x0, int y0, int x1, int y1, int k)
{
    int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy_ = y0 < y1 ? 1 : -1, e = dx + dy, e2;
    for (;;) {
        dot(x0, y0, k);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy_; }
    }
}

static void moval(int x0, int y0, int x1, int y1, int k, int fill)     /* an oval in a box: a line of it at a time */
{
    int cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, rx = abs(x1 - x0) / 2, ry = abs(y1 - y0) / 2, y, x, w, pw = -1;
    if (ry == 0) { mline(cx - rx, cy, cx + rx, cy, k); return; }
    for (y = 0; y <= ry; y++) {         /* (how wide it is y lines from its middle: a root by trying) */
        long want = (long)rx * rx * ((long)ry * ry - (long)y * y);
        for (w = 0; (long)(w + 1) * (w + 1) * ry * ry <= want; w++) ;
        if (fill) for (x = cx - w; x <= cx + w; x++) { dot(x, cy - y, k); dot(x, cy + y, k); }
        else {                          /* its edge: from the last line's width to this one's, on each side */
            int a = pw < 0 ? w : w < pw ? w : pw + (pw < w), b = pw < 0 ? w : pw;
            if (a > b) { int t = a; a = b; b = t; }
            for (x = a; x <= b; x++) { dot(cx - x, cy - y, k); dot(cx + x, cy - y, k); dot(cx - x, cy + y, k); dot(cx + x, cy + y, k); }
            if (y == ry) for (x = 0; x <= w; x++) { dot(cx - x, cy - y, k); dot(cx + x, cy - y, k); dot(cx - x, cy + y, k); dot(cx + x, cy + y, k); }
        }
        pw = w;
    }
}

static int picture(const char *file)    /* the sheet written as a PBM file: 0, or -1 (said in the note) */
{
    FILE *f;
    int k, j, x, y, a, b;
    bw = (cw + 15) / 16;
    if (!(bm = calloc(bw * ch, sizeof *bm))) { strcpy(note, "no memory for the picture"); return -1; }
    for (k = 0; k < nsh; k++) {
        struct shape *s = &sh[k];
        int x0 = s->x0 < s->x1 ? s->x0 : s->x1, x1 = s->x0 < s->x1 ? s->x1 : s->x0;
        int y0 = s->y0 < s->y1 ? s->y0 : s->y1, y1 = s->y0 < s->y1 ? s->y1 : s->y0;
        switch (s->kind) {
        case PEN:
            if (s->npt == 1) dot(pt[s->p0], pt[s->p0 + 1], s->ink == 1);
            for (j = 1; j < s->npt; j++)
                mline(pt[s->p0 + 2 * j - 2], pt[s->p0 + 2 * j - 1], pt[s->p0 + 2 * j], pt[s->p0 + 2 * j + 1], s->ink == 1);
            break;
        case LINE: mline(s->x0, s->y0, s->x1, s->y1, s->ink == 1); break;
        case BOX:
            mline(x0, y0, x1, y0, s->ink == 1); mline(x1, y0, x1, y1, s->ink == 1);
            mline(x1, y1, x0, y1, s->ink == 1); mline(x0, y1, x0, y0, s->ink == 1);
            break;
        case FBOX: for (y = y0; y <= y1; y++) for (x = x0; x <= x1; x++) dot(x, y, s->ink); break;
        case OVAL: moval(x0, y0, x1, y1, s->ink == 1, 0); break;
        case FOVAL: moval(x0, y0, x1, y1, s->ink, 1); break;
        }
    }
    if (!(f = fopen(file, "w"))) { sprintf(note, "%.40s: %.30s", file, strerror(errno)); free(bm); return -1; }
    fprintf(f, "P4\n%d %d\n", cw, ch);
    for (y = 0; y < ch; y++)
        for (x = 0; x < (cw + 7) / 8; x++) {            /* (a PBM's 1 is a dot that is not lit) */
            a = bm[y * bw + (x >> 1)]; b = x & 1 ? a & 0xFF : a >> 8;
            if (x == (cw + 7) / 8 - 1 && (cw & 7)) b |= 0xFF >> (cw & 7);
            putc(~b & 0xFF, f);
        }
    free(bm);
    if (ferror(f) | fclose(f)) { sprintf(note, "%.40s: not all written", file); return -1; }
    sprintf(note, "%.40s: %d by %d dots", file, cw, ch);
    return 0;
}

/* ---------------------------------------------------------------- the mouse on the sheet */
/* the button is down at the sheet's x, y: the tool's shape made as the mouse goes, and kept when the button comes up */
static void stroke(int x, int y)
{
    struct shape *s = more();
    int mx, my, b, e, x1 = x, y1 = y, was = 0, px = x, py = y;
    if (!s) return;
    s->kind = tool; s->ink = ink; s->x0 = x; s->y0 = y; s->p0 = npt; s->npt = 0;
    sheet(1);
    if (tool == PEN) { gink(ink == 1 ? G_BLACK : G_WHITE); if (place(x, y)) s->npt = 1; gline(x, sy(y), x, sy(y)); gflush(); }
    for (;;) {
        e = ginput(&mx, &my, &b, -1);
        if (e == G_SIZE) { resized = 1; break; }
        if (e != G_MOUSE) { if (e < 0 || stop) break; continue; }
        mx = mx < 0 ? 0 : mx >= cw ? cw - 1 : mx;
        my = gheight - TOP - STRIP - 1 - my;            /* (to the sheet's lines, and kept on the sheet) */
        my = my < 0 ? 0 : my >= ch ? ch - 1 : my;
        if (tool == PEN) {
            if ((mx != px || my != py) && s->npt < 2000 && place(mx, my)) {
                gline(px, sy(py), mx, sy(my)); s->npt++; px = mx; py = my;
                gflush();
            }
        } else if (mx != x1 || my != y1 || !was) {      /* the outline where it was taken away, and drawn where it is */
            gink(G_XOR);
            if (was) outline(tool, x, y, x1, y1);
            x1 = mx; y1 = my;
            outline(tool, x, y, x1, y1);
            was = 1;
            gflush();
        }
        if (!(b & 1)) break;
    }
    if (tool != PEN && was) { gink(G_XOR); outline(tool, x, y, x1, y1); }
    gink(G_WHITE);
    if (tool == PEN ? s->npt > 0 : was && (x1 != x || y1 != y)) {      /* (a press and no more is no shape) */
        s->x1 = x1; s->y1 = y1;
        nsh++; changed = 1;
        if (tool != PEN) draw(s);
    }
    sheet(0);
}

static void undo(void)
{
    if (!nsh) { strcpy(note, "nothing to take back"); return; }
    nsh--;
    if (sh[nsh].kind == PEN) npt = sh[nsh].p0;
    changed = 1;
    shapes();
}

static int writeout(int as)             /* saved: a name asked for if there is none yet, or (as) another is wanted */
{
    char s[128];
    if (as || !name[0]) {
        strcpy(s, name);
        if (!ask("Save as", s, 120)) return -1;
        strcpy(name, s);
    }
    return save();
}

int main(int argc, char **argv)
{
    static char *filem[] = { "New", "Open", "Save          ^O", "Save as", "Save picture", "Close         ^X" };
    static char *editm[] = { "Undo           u" };
    static const char tk[] = "plbBoO";
    char s[128], *p;
    int x, y, b, was = 0, k, c, wc = -1;
    if (argc > 2) { fprintf(stderr, "usage: paint [file]\n"); return 2; }
    if (!getenv("WM")) { fprintf(stderr, "paint: not in a window of wm\n"); return 1; }
    if (argc > 1) strncpy(name, argv[1], sizeof name - 1);
    load();
    gsize(480, 300 + TOP + STRIP + FOOT);               /* (a sheet of 480 by 300 dots) */
    if (gopen() < 0 || gmopen() < 0) { perror("paint"); return 1; }
    signal(SIGINT, sig); signal(SIGTERM, sig); signal(SIGHUP, sig);
    if (!fit()) { gclose(); fprintf(stderr, "paint: the window is too small\n"); return 1; }
    gmaysize(INKX + NINK * BW, 32 + TOP + STRIP + FOOT, 16, 16);
    all();
    while (!stop) {
        if (resized) { if (fit()) all(); else resized = 0; }
        k = ginput(&x, &y, &b, -1);
        if (k < 0) break;
        if (k == G_SIZE) { resized = 1; continue; }
        c = 0;
        if (k == G_MOUSE) {
            if (!(b & 1) || was) { was = b & 1; continue; }
            was = 1;
            if (y >= gheight - TOP) {
                if (x >= 4 && x < 44) { k = menu(4, filem, 6); c = k < 0 ? 0 : 2000 + k; was = 0; }
                else if (x >= 52 && x < 92) { k = menu(52, editm, 1); c = k == 0 ? 'u' : 0; was = 0; }
            } else if (y >= gheight - TOP - STRIP) {
                if (x < NTOOL * BW) { tool = x / BW; strip(); }
                else if (x >= INKX && x < INKX + NINK * BW) { ink = (x - INKX) / BW; strip(); }
            } else if (y >= FOOT && x < cw) { stroke(x, gheight - TOP - STRIP - 1 - y); was = 0; }
        } else if (k == 'u' || k == 15 || k == 24) c = k;
        else if (k > 0 && k < 256 && (p = strchr(tk, k)) != 0) { tool = p - tk; strip(); }
        switch (c) {
        case 'u': undo(); break;
        case 2000:                                      /* New */
            if (changed && (k = key1("Lose what was drawn? y to do it")) != 'y' && k != 'Y') break;
            nsh = npt = 0; name[0] = 0; changed = 0; shapes();
            break;
        case 2001:                                      /* Open */
            if (changed && (k = key1("Lose what was drawn? y to do it")) != 'y' && k != 'Y') break;
            s[0] = 0;
            if (ask("Open", s, 120)) { strcpy(name, s); load(); shapes(); }
            break;
        case 15: case 2002: writeout(0); break;         /* Save */
        case 2003: writeout(1); break;
        case 2004:                                      /* Save picture */
            s[0] = 0;
            if (ask("Save the picture as", s, 120)) picture(s);
            break;
        case 24: case 2005:                             /* Close */
            if (changed) {
                k = key1("Save it before ending? y, n (ESC: go on)");
                if (k == 'y' || k == 'Y') { if (writeout(0) == 0) stop = 1; }
                else if (k == 'n' || k == 'N') stop = 1;
            } else stop = 1;
            break;
        }
        if (stop) break;
        if (resized) continue;
        if (changed != wc) { head(); wc = changed; }
        foot();
        gflush();
    }
    gmclose();
    gclose();
    return 0;
}
