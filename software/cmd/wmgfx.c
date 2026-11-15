/* wmgfx.c -- wm: a program's picture. A program in a window that draws with the graphics library has the
 * library ask wm to do it (libgfx/gwin.c): each call arrives on the program's terminal as a request, a letter
 * and numbers, and is done here with the same library, into a window opened for the picture beside the
 * program's terminal. What the program has set (its ink, look, style, pattern, clipping, pen) is kept with its
 * window and put back whenever its requests begin again, as wm draws for others in between.
 *
 * The answers (a dot's state, a kept picture's number, where the mouse is) go back to the program as numbers
 * typed at its terminal: written to the terminal's master.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <gfx.h>
#include "wmwin.h"
#include "wmgfx.h"

static struct gx *cur;                  /* whose state the library has now */
static int opened;

static void answer(struct gx *g, int n, int a, int b, int c, int d)
{
    char s[64];
    int v[4], k, len;
    v[0] = a; v[1] = b; v[2] = c; v[3] = d;
    len = sprintf(s, "\033_");
    for (k = 0; k < n; k++) len += sprintf(s + len, k ? ";%d" : "%d", v[k]);
    len += sprintf(s + len, "\033\\");
    write(g->fd, s, len);
}

static void enter(struct gx *g)         /* the library drawing in its window, as the program left things */
{
    if (cur == g) return;
    wdraw(g->win);
    if (g->ink != G_WHITE) gink(g->ink);
    if (g->look) glook(g->look);
    if (g->style != 0xFFFF) gstyle(g->style);
    if (g->pat) gpattern(g->rows);
    if (g->clip) gclip(g->c[0], g->c[1], g->c[2], g->c[3]);
    gmove(g->px, g->py);
    cur = g;
}

void gx_done(void) { cur = 0; }

void gx_show(struct gx *g)
{
    if (g->win && g->dirty) { g->dirty = 0; wshow(g->win); cur = 0; }
}

void gx_close(struct gx *g)
{
    int k;
    if (!g->win) return;
    for (k = 0; k < g->nkept; k++) gdrop(g->kept[k]);
    wclose(g->win);
    g->win = 0; g->nkept = 0; g->mwait = 0;
    cur = 0;
}

static void mtell(struct gx *g)         /* where the mouse is in the window, and whether that is news */
{
    answer(g, 4, g->mx, g->my, g->mb, g->mnew);
    g->mnew = g->mwait = 0;
}

void gx_mouse(struct gx *g, int x, int y, int b)
{
    if (!g->win || (x == g->mx && y == g->my && b == g->mb)) return;
    g->mx = x; g->my = y; g->mb = b; g->mnew = 1;
    if (g->mwait) mtell(g);
}

void gx_tick(struct gx *g)
{
    if (g->win && g->mwait > 0 && --g->mwait == 0) mtell(g);
}

static int mine(struct gx *g, int n) { int k; for (k = 0; k < g->nkept; k++) if (g->kept[k] == n) return k; return -1; }

void gx_request(struct gx *g, char *s)
{
    int v[124], n = 0, neg = 0, any = 0, val = 0, op = *s, k, w, h;
    char *text = 0, title[40];
    if (!op) return;
    for (s++; *s; s++) {                /* its numbers, and the text after a : */
        if (*s == ':') { text = s + 1; break; }
        if (*s == '-') neg = 1;
        else if (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); any = 1; }
        else if (*s == ';') { if (n < 124) v[n++] = neg ? -val : val; val = neg = any = 0; }
    }
    if (any && n < 124) v[n++] = neg ? -val : val;
    for (k = n; k < 8; k++) v[k] = 0;
    if (text) for (s = text; *s; s++) if (*s == 0x1F) *s = '\n';
    if (op == 'N') {                    /* the mouse is waited for no longer: answered always, with no numbers */
        g->mwait = 0;
        answer(g, 0, 0, 0, 0, 0);
        return;
    }
    if (op == 'O') {                    /* gopen: a window for the picture */
        if (!g->win) {
            w = v[0] < 16 ? 16 : v[0] > 640 ? 640 : v[0];       /* (the size the library has told the program: gwin.c) */
            h = v[1] < 16 ? 16 : v[1] > 440 ? 440 : v[1];
            sprintf(title, "Picture %d", opened + 1);
            k = 200 + 20 * (opened % 6); n = 20 + 16 * (opened % 6);
            wfit(&k, &n, w, h);
            g->win = wopen(k, n, w, h, g->title ? g->title : title);
            if (g->win < 0) { g->win = 0; return; }             /* (no room: what it draws goes nowhere) */
            opened++;
        }
        else { wdraw(g->win); gclear(); g->dirty = 1; }         /* (a picture left by a program that was stopped: black again) */
        g->ink = G_WHITE; g->look = 0; g->style = 0xFFFF; g->pat = g->clip = 0; g->px = g->py = 0;
        g->mx = g->my = -1; g->mb = g->mnew = g->mwait = 0;
        cur = 0;
        return;
    }
    if (!g->win) {                      /* (no picture: what wants an answer is told so) */
        if (op == 'g' || op == 'f' || op == 'K' || op == 'U') answer(g, 1, -1, 0, 0, 0);
        if (op == 'M') answer(g, 4, 0, 0, 0, 0);
        return;
    }
    if (op == 'Q') { gx_close(g); return; }
    if (op == 'u') { gx_show(g); return; }
    if (op == 'U') { gx_show(g); gsync(); answer(g, 1, 0, 0, 0, 0); return; }
    if (op == 'M') {                    /* gmouse: now, or when there is news, or when the time is up */
        if (g->mnew || v[0] == 0) mtell(g); else g->mwait = v[0];
        return;
    }
    enter(g);
    switch (op) {
    case 'x': gclear(); break;
    case 'i': gink(g->ink = v[0]); return;
    case 'k': glook(g->look = v[0]); return;
    case 's': gstyle(g->style = (unsigned)v[0] & 0xFFFF); return;
    case 'P':
        g->pat = n >= 16;
        for (k = 0; k < 16; k++) g->rows[k] = (unsigned short)v[k];
        gpattern(g->pat ? g->rows : 0);
        return;
    case 'C': g->clip = 1; for (k = 0; k < 4; k++) g->c[k] = v[k]; gclip(v[0], v[1], v[2], v[3]); return;
    case 'p': gpoint(v[0], v[1]); break;
    case 'm': gmove(g->px = v[0], g->py = v[1]); return;
    case 'd': gdraw(g->px = v[0], g->py = v[1]); break;
    case 'l': gline(v[0], v[1], v[2], v[3]); break;
    case 'b': gbox(v[0], v[1], v[2], v[3]); break;
    case 'B': gfbox(v[0], v[1], v[2], v[3]); break;
    case 'e': gellipse(v[0], v[1], v[2], v[3]); break;
    case 'E': gfellipse(v[0], v[1], v[2], v[3]); break;
    case 'a': garc(v[0], v[1], v[2], v[3], v[4], v[5]); break;
    case 'y': gpoly(v, n / 2); break;
    case 'Y': gfpoly(v, n / 2); break;
    case 't': if (text) gtext(v[0], v[1], text); break;
    case 'c': gcopy(v[0], v[1], v[2], v[3], v[4], v[5]); break;
    case 'f': answer(g, 1, gfill(v[0], v[1]), 0, 0, 0); break;
    case 'g': answer(g, 1, gget(v[0], v[1]), 0, 0, 0); return;
    case 'K':
        k = g->nkept < 8 ? gkeep(v[0], v[1], v[2], v[3]) : -1;
        if (k >= 0) g->kept[g->nkept++] = k;
        answer(g, 1, k, 0, 0, 0);
        return;
    case 'S': if (mine(g, v[0]) >= 0) gstamp(v[0], v[1], v[2], v[3]); break;
    case 'D': if ((k = mine(g, v[0])) >= 0) { gdrop(v[0]); g->kept[k] = g->kept[--g->nkept]; } return;
    case 'w': {                         /* gwrite: whole lines of a picture, four digits a word */
        static unsigned short bits[256];
        int nw = 0, d;
        w = v[2] >= v[0] ? (v[2] - v[0] + 16) >> 4 : 0;
        h = v[3] - v[1] + 1;
        for (s = text; s && *s && nw < 256; nw++) {
            for (d = k = 0; k < 4 && *s; k++, s++) d = d << 4 | (*s <= '9' ? *s - '0' : (*s | 0x20) - 'a' + 10);
            bits[nw] = (unsigned short)d;
        }
        if (w < 1 || h < 1 || nw != w * h) return;      /* (not what it says it is) */
        gwrite(v[0], v[1], v[2], v[3], bits, v[4] & 3);
        break;
    }
    default: return;
    }
    g->dirty = 1;
}
