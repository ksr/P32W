/* wmwin.c -- wm: the windows. Frames, the menu bar, buttons and the pointer on the video card's screen, over
 * the graphics library (gfx(3)) and the mouse (mouse(4)); wm.c puts a terminal in each (wm(1), D122).
 *
 * Every window has a picture of its own: its inside, kept in the chip's memory off the screen (gmake). The
 * program draws into that (wdraw: the graphics library's drawing is sent there), and the screen is put
 * together from the pictures: of each window, the parts no window in front of it covers are copied to the
 * screen by the chip (gstamp), and its frame drawn round them. So a window that is uncovered or moved is
 * simply copied again; the program is never asked to draw anything twice.
 *
 * What shows of a window is its rectangle less the rectangles of the windows in front of it and the menu
 * bar: a list of rectangles, each drawn with the chip clipping to it. The desk shows where no window is.
 *
 * The pointer is a kept picture put down in the way that turns dots over (G_FLIP), so putting it down again
 * takes it away; it is taken away before anything is drawn on the screen and put back after.
 *
 * The mouse does three things without the program: it brings the window it is pressed in to the front, it
 * moves a window whose bar it drags (an outline follows the pointer; the window is copied there when the
 * button comes up), and it opens the menus. The rest it hands on: wevent.
 */
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#ifdef __P32__
#include <sgtty.h>
#endif
#include <gfx.h>
#include "wmwin.h"

#define NWIN   16
#define NBUT   32
#define NMENU  8
#define NITEM  16
#define NEV    16
#define NRECT  64
#define BAR    18                       /* the menu bar's lines at the top of the screen */
#define TB     18                       /* a window's bar: lines above its inside, the frame's top line the last */
#define TITLE  40

struct rect { short x0, y0, x1, y1; };  /* both corners in it */
static struct win { int used, x, y, w, h, pic; char title[TITLE]; } wins[NWIN + 1];
static int z[NWIN], nz;                 /* the windows from the back to the front */
static struct but { int used, win, x, y, w, h; char label[TITLE]; } buts[NBUT];
static struct menu { int x, w, n; char title[TITLE]; char item[NITEM][TITLE]; } menus[NMENU];
static int nmenu;
static struct wevent evq[NEV];
static int evh, evt;
static int sw, sh;                      /* the screen */
static int started, cur;                /* cur: the window the program draws into (0: none) */
static int px, py, pb, pshown, ppic = -1, phide;        /* the pointer; phide: taken off, how many times over */
static int mode;                        /* what the held button is doing: 0 nothing; D_ a window dragged, a menu open, */
enum { D_NONE, D_DRAG, D_MENU, D_BUTTON, D_INSIDE };    /* a button pressed, the button down inside a window */
static int dwin, dox, doy, dx0, dy0;    /* D_DRAG: the window, the pointer's place in it, where its outline is drawn */
static int mopen, mitem, mkeep, mx0, my0, mw, mh;       /* D_MENU: the menu, the item under the pointer, what it covers */
static int bheld, bin;                  /* D_BUTTON: the button, and whether the pointer is on it */
#ifdef __P32__
static struct sgttyb tty0;
#endif

static const unsigned short grey[16] = { 0x8888, 0, 0x2222, 0, 0x8888, 0, 0x2222, 0, 0x8888, 0, 0x2222, 0, 0x8888, 0, 0x2222, 0 };

/* ---------------------------------------------------------------- rectangles */
static struct rect R(int x0, int y0, int x1, int y1) { struct rect r; r.x0 = (short)x0; r.y0 = (short)y0; r.x1 = (short)x1; r.y1 = (short)y1; return r; }
static struct rect outer(const struct win *w) { return R(w->x - 1, w->y - 1, w->x + w->w, w->y + w->h + TB); }
static int cut(struct rect *a, struct rect b)           /* a cut to b: 0 if nothing is left */
{
    if (a->x0 < b.x0) a->x0 = b.x0;
    if (a->y0 < b.y0) a->y0 = b.y0;
    if (a->x1 > b.x1) a->x1 = b.x1;
    if (a->y1 > b.y1) a->y1 = b.y1;
    return a->x0 <= a->x1 && a->y0 <= a->y1;
}

/* the n rectangles of list less s: what is left of each is up to four (above s, below it, to its left and right) */
static int less(struct rect *list, int n, struct rect s)
{
    struct rect out[NRECT];
    int k, m = 0;
    for (k = 0; k < n; k++) {
        struct rect a = list[k], c = a;
        if (!cut(&c, s)) { if (m < NRECT) out[m++] = a; continue; }
        if (a.y1 > c.y1 && m < NRECT) out[m++] = R(a.x0, c.y1 + 1, a.x1, a.y1);
        if (a.y0 < c.y0 && m < NRECT) out[m++] = R(a.x0, a.y0, a.x1, c.y0 - 1);
        if (a.x0 < c.x0 && m < NRECT) out[m++] = R(a.x0, c.y0, c.x0 - 1, c.y1);
        if (a.x1 > c.x1 && m < NRECT) out[m++] = R(c.x1 + 1, c.y0, a.x1, c.y1);
    }
    memcpy(list, out, m * sizeof *out);
    return m;
}

/* what shows of the rectangle r of the window at place k of z: not under the menu bar, nor a window in front */
static int showing(int k, struct rect r, struct rect *out)
{
    int n = 1, j;
    out[0] = r;
    if (!cut(&out[0], R(0, 0, sw - 1, sh - BAR - 1))) return 0;
    for (j = k + 1; j < nz && n; j++) n = less(out, n, outer(&wins[z[j]]));
    return n;
}

/* ---------------------------------------------------------------- the pointer */
static void pflip(void) { gclip(0, 0, sw - 1, sh - 1); gstamp(ppic, px, py - 15, G_FLIP); }

static void poff(void)                  /* off the screen while it is drawn on (and the screen is what is drawn on) */
{
    if (!phide++) {
        gtarget(-1);
        if (pshown) { pflip(); pshown = 0; }
    }
}

static void pon(void)                   /* ... and back; the program's window is what is drawn into again */
{
    if (--phide) return;
    if (ppic >= 0) { pflip(); pshown = 1; }
    gclip(0, 0, sw - 1, sh - 1);
    gflush();
    if (cur) gtarget(wins[cur].pic);
}

void wpointer(int on)
{
    if (!started) return;
    if (on) { if (phide > 0) pon(); } else poff();
}

/* ---------------------------------------------------------------- drawing the screen */
static void plain(void) { gink(G_WHITE); glook(0); gstyle(0xFFFF); gpattern(0); }

static void text(int x, int y, int room, const char *s)        /* as much of s as there is room for (dots) */
{
    char b[TITLE];
    int n = strlen(s);
    if (n > room / 8) n = room / 8;
    if (n <= 0) return;
    memcpy(b, s, n); b[n] = 0;
    gtext(x, y, b);
}

/* a window as it is: its frame and its inside. The front window's bar is white with black letters. */
static void frame(int n)
{
    struct win *w = &wins[n];
    int front = nz && z[nz - 1] == n, ty = w->y + w->h + 1, len = strlen(w->title);
    plain();
    gbox(w->x - 1, w->y - 1, w->x + w->w, w->y + w->h + TB);
    gline(w->x, w->y + w->h, w->x + w->w - 1, w->y + w->h);
    gink(front ? G_WHITE : G_BLACK);
    gfbox(w->x, ty, w->x + w->w - 1, ty + TB - 2);
    gink(front ? G_BLACK : G_WHITE);
    if (w->w >= 20) gbox(w->x + 3, ty + 3, w->x + 13, ty + 13);        /* the close box */
    if (len > (w->w - 40) / 8) len = (w->w - 40) / 8;
    if (len > 0) text(w->x + (w->w - 8 * len) / 2, ty, 8 * len, w->title);
    gink(G_WHITE);
    gstamp(w->pic, w->x, w->y, G_COPY);
}

static void bar(void)                   /* the menu bar */
{
    int k;
    plain();
    gfbox(0, sh - BAR, sw - 1, sh - 1);
    gink(G_BLACK);
    gline(0, sh - BAR, sw - 1, sh - BAR);
    for (k = 0; k < nmenu; k++) gtext(menus[k].x, sh - BAR + 1, menus[k].title);
    gink(G_WHITE);
}

/* the screen's rectangle d made again: the desk where no window is, each window where it shows, the bar */
static void paint(struct rect d)
{
    struct rect list[NRECT], full = R(0, 0, sw - 1, sh - 1);
    int n, k, j;
    if (!cut(&d, full)) return;
    poff();
    list[0] = d; n = 1;
    if (!cut(&list[0], R(0, 0, sw - 1, sh - BAR - 1))) n = 0;
    for (j = 0; j < nz && n; j++) n = less(list, n, outer(&wins[z[j]]));
    plain();
    for (k = 0; k < n; k++) {
        gclip(list[k].x0, list[k].y0, list[k].x1, list[k].y1);
        gink(G_BLACK); gfbox(list[k].x0, list[k].y0, list[k].x1, list[k].y1);
        gink(G_WHITE); gpattern(grey); gfbox(list[k].x0, list[k].y0, list[k].x1, list[k].y1); gpattern(0);
    }
    for (j = 0; j < nz; j++) {
        struct rect o = outer(&wins[z[j]]);
        if (!cut(&o, d)) continue;
        n = showing(j, o, list);
        for (k = 0; k < n; k++) { gclip(list[k].x0, list[k].y0, list[k].x1, list[k].y1); frame(z[j]); }
    }
    if (d.y1 >= sh - BAR) { gclip(d.x0, sh - BAR, d.x1, sh - 1); bar(); }
    plain();
    pon();
}

void wrepaint(void) { if (started) paint(R(0, 0, sw - 1, sh - 1)); }

/* ---------------------------------------------------------------- windows */
static int zat(int n) { int k; for (k = 0; k < nz; k++) if (z[k] == n) return k; return -1; }
static int ok(int n) { return started && n >= 1 && n <= NWIN && wins[n].used; }

int wopen(int x, int y, int w, int h, const char *title)
{
    int n, old;
    if (!started || w < 1 || h < 1) return -1;
    for (n = 1; n <= NWIN && wins[n].used; n++)
        ;
    if (n > NWIN) return -1;
    poff();
    wins[n].pic = gmake(w, h);
    if (wins[n].pic < 0) { pon(); return -1; }
    wins[n].used = 1; wins[n].x = x; wins[n].y = y; wins[n].w = w; wins[n].h = h;
    strncpy(wins[n].title, title ? title : "", TITLE - 1); wins[n].title[TITLE - 1] = 0;
    old = nz ? z[nz - 1] : 0;
    z[nz++] = n;
    if (old) paint(outer(&wins[old]));  /* (the one that was in front: its bar changes) */
    paint(outer(&wins[n]));
    pon();
    return n;
}

void wclose(int n)
{
    struct rect o;
    int k;
    if (!ok(n)) return;
    poff();
    o = outer(&wins[n]);
    for (k = 0; k < NBUT; k++) if (buts[k].used && buts[k].win == n) buts[k].used = 0;
    k = zat(n);
    memmove(z + k, z + k + 1, (nz - k - 1) * sizeof z[0]);
    nz--;
    gdrop(wins[n].pic);
    wins[n].used = 0;
    if (cur == n) cur = 0;
    paint(o);
    if (nz) paint(outer(&wins[z[nz - 1]]));     /* (the one now in front) */
    pon();
}

void wdraw(int n)
{
    if (!started) return;
    cur = ok(n) ? n : 0;
    gtarget(cur ? wins[cur].pic : -1);
    plain();
}

void wpart(int n, int x0, int y0, int x1, int y1)
{
    struct rect list[NRECT], r;
    int k, m, t;
    if (!ok(n)) return;
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    r = R(wins[n].x + x0, wins[n].y + y0, wins[n].x + x1, wins[n].y + y1);
    if (!cut(&r, R(wins[n].x, wins[n].y, wins[n].x + wins[n].w - 1, wins[n].y + wins[n].h - 1))) return;
    poff();
    m = showing(zat(n), r, list);
    for (k = 0; k < m; k++) {
        gclip(list[k].x0, list[k].y0, list[k].x1, list[k].y1);
        gstamp(wins[n].pic, wins[n].x, wins[n].y, G_COPY);
    }
    pon();
}

void wshow(int n) { if (ok(n)) wpart(n, 0, 0, wins[n].w - 1, wins[n].h - 1); }

void wfront(int n)
{
    int k, old;
    if (!ok(n) || z[nz - 1] == n) return;
    poff();
    old = z[nz - 1];
    k = zat(n);
    memmove(z + k, z + k + 1, (nz - k - 1) * sizeof z[0]);
    z[nz - 1] = n;
    paint(outer(&wins[old]));
    paint(outer(&wins[n]));
    pon();
}

void wmove(int n, int x, int y)
{
    struct rect o;
    if (!ok(n) || (wins[n].x == x && wins[n].y == y)) return;
    poff();
    o = outer(&wins[n]);
    wins[n].x = x; wins[n].y = y;
    paint(o);
    paint(outer(&wins[n]));
    pon();
}

void wtitle(int n, const char *title)
{
    if (!ok(n)) return;
    strncpy(wins[n].title, title ? title : "", TITLE - 1); wins[n].title[TITLE - 1] = 0;
    paint(R(wins[n].x, wins[n].y + wins[n].h + 1, wins[n].x + wins[n].w - 1, wins[n].y + wins[n].h + TB - 1));
}

int wplace(int n, int *x, int *y, int *w, int *h)
{
    if (!ok(n)) return -1;
    if (x) *x = wins[n].x;
    if (y) *y = wins[n].y;
    if (w) *w = wins[n].w;
    if (h) *h = wins[n].h;
    return 0;
}

/* ---------------------------------------------------------------- buttons */
static void bdraw(int k, int down)      /* into its window's picture: black on white while it is held down */
{
    struct but *b = &buts[k];
    int len = strlen(b->label);
    gtarget(wins[b->win].pic);
    plain();
    gink(down ? G_WHITE : G_BLACK);
    gfbox(b->x, b->y, b->x + b->w - 1, b->y + b->h - 1);
    gink(G_WHITE);
    gbox(b->x, b->y, b->x + b->w - 1, b->y + b->h - 1);
    if (len > (b->w - 4) / 8) len = (b->w - 4) / 8;
    gink(down ? G_BLACK : G_WHITE);
    if (len > 0 && b->h >= 16) text(b->x + (b->w - 8 * len) / 2, b->y + (b->h - 16) / 2, 8 * len, b->label);
    plain();
    gtarget(cur ? wins[cur].pic : -1);
}

int wbutton(int n, int x, int y, int w, int h, const char *label)
{
    int k;
    if (!ok(n) || w < 4 || h < 4) return -1;
    for (k = 0; k < NBUT && buts[k].used; k++)
        ;
    if (k == NBUT) return -1;
    buts[k].used = 1; buts[k].win = n; buts[k].x = x; buts[k].y = y; buts[k].w = w; buts[k].h = h;
    strncpy(buts[k].label, label ? label : "", TITLE - 1); buts[k].label[TITLE - 1] = 0;
    bdraw(k, 0);
    wshow(n);
    return k;
}

void wbuttons(int n)
{
    int k;
    if (!ok(n)) return;
    for (k = 0; k < NBUT; k++) if (buts[k].used && buts[k].win == n) bdraw(k, 0);
    wshow(n);
}

/* ---------------------------------------------------------------- menus */
int wmenu(const char *title, const char *items)
{
    struct menu *m;
    int k;
    if (!started || nmenu == NMENU) return -1;
    m = &menus[nmenu];
    strncpy(m->title, title, TITLE - 1); m->title[TITLE - 1] = 0;
    m->x = nmenu ? menus[nmenu - 1].x + 8 * (int)strlen(menus[nmenu - 1].title) + 16 : 8;
    m->n = 0; m->w = 0;
    while (*items && m->n < NITEM) {
        for (k = 0; items[k] && items[k] != '\n'; k++) if (k < TITLE - 1) m->item[m->n][k] = items[k];
        m->item[m->n][k < TITLE - 1 ? k : TITLE - 1] = 0;
        if ((int)strlen(m->item[m->n]) > m->w) m->w = strlen(m->item[m->n]);
        m->n++;
        items += k;
        if (*items) items++;
    }
    nmenu++;
    paint(R(0, sh - BAR, sw - 1, sh - 1));
    return nmenu - 1;
}

static void mrow(int i)                 /* item i of the open menu turned over (it is under the pointer, or was) */
{
    gink(G_XOR);
    gfbox(mx0 + 1, my0 + mh - 1 - 16 * (i + 1), mx0 + mw - 2, my0 + mh - 2 - 16 * i);
    gink(G_WHITE);
}

static void mshow(int k)                /* menu k let down over whatever is there, which is kept */
{
    struct menu *m = &menus[k];
    int i;
    poff();
    mw = 8 * m->w + 18; mh = 16 * m->n + 2;
    mx0 = m->x - 8; my0 = sh - BAR - mh;
    if (mx0 + mw > sw) mx0 = sw - mw;
    plain();
    gclip(0, 0, sw - 1, sh - 1);
    mkeep = gkeep(mx0, my0, mx0 + mw - 1, my0 + mh - 1);
    gfbox(mx0, my0, mx0 + mw - 1, my0 + mh - 1);
    gink(G_BLACK);
    gbox(mx0, my0, mx0 + mw - 1, my0 + mh - 1);
    for (i = 0; i < m->n; i++) gtext(mx0 + 9, my0 + mh - 1 - 16 * (i + 1), m->item[i]);
    gink(G_XOR);                        /* its title on the bar turned over */
    gfbox(m->x - 4, sh - BAR + 1, m->x + 8 * (int)strlen(m->title) + 3, sh - 2);
    gink(G_WHITE);
    mopen = k; mitem = -1;
    pon();
}

static void mhide(void)
{
    struct menu *m = &menus[mopen];
    poff();
    plain();
    gclip(0, 0, sw - 1, sh - 1);
    if (mkeep >= 0) { gstamp(mkeep, mx0, my0, G_COPY); gdrop(mkeep); }
    else paint(R(mx0, my0, mx0 + mw - 1, my0 + mh - 1));
    gink(G_XOR);
    gfbox(m->x - 4, sh - BAR + 1, m->x + 8 * (int)strlen(m->title) + 3, sh - 2);
    gink(G_WHITE);
    pon();
}

static int mfind(int x, int y)          /* the menu whose title is at x, y on the bar, or -1 */
{
    int k;
    if (y < sh - BAR) return -1;
    for (k = 0; k < nmenu; k++)
        if (x >= menus[k].x - 4 && x < menus[k].x + 8 * (int)strlen(menus[k].title) + 4) return k;
    return -1;
}

static void mtrack(int x, int y)        /* the pointer has moved with a menu open */
{
    int i = -1, k = mfind(x, y);
    if (k >= 0 && k != mopen) { mhide(); mshow(k); return; }
    if (x >= mx0 && x < mx0 + mw && y >= my0 + 1 && y < my0 + mh - 1) i = (my0 + mh - 2 - y) / 16;
    if (i >= menus[mopen].n) i = -1;
    if (i == mitem) return;
    poff();
    plain(); gclip(0, 0, sw - 1, sh - 1);
    if (mitem >= 0) mrow(mitem);
    if (i >= 0) mrow(i);
    mitem = i;
    pon();
}

/* ---------------------------------------------------------------- what the mouse does */
static void post(int type, int win, int x, int y, int id, int item)
{
    struct wevent *e;
    if (evh - evt == NEV) return;
    e = &evq[evh++ % NEV];
    e->type = type; e->win = win; e->x = x; e->y = y; e->key = 0; e->id = id; e->item = item;
}

static void outline(void)               /* the dragged window's outline, turned over: there, or gone again */
{
    struct win *w = &wins[dwin];
    gink(G_XOR); gclip(0, 0, sw - 1, sh - BAR - 1);
    gbox(dx0 - 1, dy0 - 1, dx0 + w->w, dy0 + w->h + TB);
    gink(G_WHITE);
}

static int wfind(int x, int y)          /* the window that shows at x, y, or 0 */
{
    int k;
    if (y >= sh - BAR) return 0;
    for (k = nz - 1; k >= 0; k--) {
        struct rect o = outer(&wins[z[k]]);
        if (x >= o.x0 && x <= o.x1 && y >= o.y0 && y <= o.y1) return z[k];
    }
    return 0;
}

static int bfind(int n, int x, int y)   /* the button of window n at its dot x, y, or -1 */
{
    int k;
    for (k = 0; k < NBUT; k++)
        if (buts[k].used && buts[k].win == n && x >= buts[k].x && x < buts[k].x + buts[k].w && y >= buts[k].y && y < buts[k].y + buts[k].h) return k;
    return -1;
}

static void bshow(int k, int down)
{
    bdraw(k, down);
    wshow(buts[k].win);
}

static void down(int x, int y)          /* the left button has gone down at x, y */
{
    struct win *w;
    int n, k;
    if ((k = mfind(x, y)) >= 0) { mshow(k); mode = D_MENU; return; }
    if (!(n = wfind(x, y))) return;
    wfront(n);
    w = &wins[n];
    if (y > w->y + w->h) {              /* its bar */
        if (x >= w->x + 3 && x <= w->x + 13 && w->w >= 20) { post(W_CLOSE, n, 0, 0, 0, 0); return; }
        dwin = n; dox = x - w->x; doy = y - w->y; dx0 = w->x; dy0 = w->y;
        poff(); outline(); pon();
        mode = D_DRAG;
        return;
    }
    if (x < w->x || x >= w->x + w->w || y < w->y) return;       /* (its frame) */
    if ((k = bfind(n, x - w->x, y - w->y)) >= 0) { bheld = k; bin = 1; bshow(k, 1); mode = D_BUTTON; return; }
    dwin = n;
    mode = D_INSIDE;
    post(W_DOWN, n, x - w->x, y - w->y, 0, 0);
}

static void moved(int x, int y)         /* the pointer is at x, y now */
{
    int in;
    poff(); px = x; py = y; pon();
    if (mode == D_DRAG) {
        poff();
        outline();
        dx0 = x - dox; dy0 = y - doy;
        outline();
        pon();
    } else if (mode == D_MENU) mtrack(x, y);
    else if (mode == D_INSIDE && wins[dwin].used) {     /* (one that is not yet taken is brought up to date instead) */
        if (evh != evt && evq[(evh - 1) % NEV].type == W_DRAG) evh--;
        post(W_DRAG, dwin, x - wins[dwin].x, y - wins[dwin].y, 0, 0);
    } else if (mode == D_BUTTON) {
        struct win *w = &wins[buts[bheld].win];
        in = bfind(buts[bheld].win, x - w->x, y - w->y) == bheld && wfind(x, y) == buts[bheld].win;
        if (in != bin) { bin = in; bshow(bheld, in); }
    }
}

static void up(int x, int y)            /* the left button has come up */
{
    int m = mode;
    mode = D_NONE;
    if (m == D_DRAG) {
        poff(); outline(); pon();
        if (dx0 != wins[dwin].x || dy0 != wins[dwin].y) { wmove(dwin, dx0, dy0); post(W_MOVED, dwin, dx0, dy0, 0, 0); }
    } else if (m == D_MENU) {
        int k = mopen, i = mitem;
        mhide();
        if (i >= 0) post(W_MENU, 0, 0, 0, k, i);
    } else if (m == D_BUTTON) {
        if (bin) { bshow(bheld, 0); post(W_BUTTON, buts[bheld].win, 0, 0, bheld, 0); }
    } else if (m == D_INSIDE && wins[dwin].used)
        post(W_UP, dwin, x - wins[dwin].x, y - wins[dwin].y, 0, 0);
}

int wtop(void) { return started && nz ? z[nz - 1] : 0; }

void wfeed(int x, int y, int b)
{
    if (!started) return;
    if (x != px || y != py) moved(x, y);
    if ((b & 1) && !(pb & 1)) down(x, y);
    if (!(b & 1) && (pb & 1)) up(x, y);
    pb = b;
    gmat(x, y);
}

/* ---------------------------------------------------------------- events */
int wevent(struct wevent *e, int wait)
{
    struct timeval tv, *tp;
    fd_set r;
    unsigned char c;
    int x, y, b, n, mfd = gmfd();
    if (!started) return 0;
    for (;;) {
        if (evh != evt) { *e = evq[evt++ % NEV]; return 1; }
        while (gmouse(&x, &y, &b, 0) > 0) {             /* what the mouse has done, a change of a button at a time */
            if (x != px || y != py) moved(x, y);
            if ((b & 1) && !(pb & 1)) down(x, y);
            if (!(b & 1) && (pb & 1)) up(x, y);
            pb = b;
            if (evh != evt) break;
        }
        if (evh != evt) continue;
        FD_ZERO(&r); FD_SET(0, &r);
        if (mfd >= 0) FD_SET(mfd, &r);
        tv.tv_sec = wait > 0 ? wait / 50 : 0;
        tv.tv_usec = wait > 0 ? wait % 50 * 20000 : 0;
        tp = wait < 0 ? 0 : &tv;
        n = select((mfd > 0 ? mfd : 0) + 1, &r, 0, 0, tp);
        if (n <= 0) return 0;
        if (FD_ISSET(0, &r) && read(0, &c, 1) == 1) {
            e->type = W_KEY; e->key = c; e->win = nz ? z[nz - 1] : 0; e->x = e->y = e->id = e->item = 0;
            return 1;
        }
        if (wait > 0) wait = 0;         /* (the mouse moved and nothing came of it: the time is not begun again) */
    }
}

/* ---------------------------------------------------------------- the start and the end */
int wstart(void)
{
    static const int arrow[] = { 0, 15, 0, 2, 3, 5, 6, 0, 8, 1, 5, 6, 10, 6 };
    if (started) return 0;
    if (gopen() < 0) return -1;
    if (gmopen() < 0) { gclose(); return -1; }
    sw = gwidth; sh = gheight;
    memset(wins, 0, sizeof wins); memset(buts, 0, sizeof buts);
    nz = nmenu = evh = evt = cur = mode = pb = pshown = 0;
    phide = 1;                          /* (no pointer until the desk is there) */
    ppic = gmake(12, 16);
    if (ppic >= 0) { gtarget(ppic); gfpoly(arrow, 7); gtarget(-1); }
    gmouse(&px, &py, 0, 0);
#ifdef __P32__
    {
        struct sgttyb t;
        gtty(0, &tty0);
        t = tty0; t.sg_flags = (t.sg_flags | CBREAK) & ~ECHO;
        stty(0, &t);
    }
#endif
    started = 1;
    paint(R(0, 0, sw - 1, sh - 1));
    pon();
    return 0;
}

void wend(void)
{
    if (!started) return;
    started = 0;
#ifdef __P32__
    stty(0, &tty0);
#endif
    gmclose();
    gclose();
}
