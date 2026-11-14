/* wmterm.c -- wm: a terminal in a window. What a program writes to its pseudo-terminal is written here on a
 * sheet of characters (tput), as the console's own screen does it (kernel/video.c: the same control characters
 * and ESC [ sequences, so anything that runs on the console runs in a window); tshow then draws what has
 * changed into the window's picture and has that part put on the screen.
 *
 * The two are apart so that a burst of output costs what its result costs: a hundred lines scrolled past are
 * one movement of the picture (the chip's copy) and one screenful of characters, not a hundred of each.
 */
#include <string.h>
#include <gfx.h>
#include "wmwin.h"
#include "wmterm.h"

static void touch(struct term *t, int r, int c0, int c1)
{
    if (c0 < t->d0[r]) t->d0[r] = (short)c0;
    if (c1 > t->d1[r]) t->d1[r] = (short)c1;
}

static void blank(struct term *t, int r, int c0, int c1)       /* columns c0 to c1 of row r: spaces */
{
    if (c0 > c1) return;
    memset(t->ch[r] + c0, ' ', c1 - c0 + 1);
    memset(t->at[r] + c0, 0, c1 - c0 + 1);
    touch(t, r, c0, c1);
}

void tinit(struct term *t, int win, int cols, int rows)
{
    int r;
    memset(t, 0, sizeof *t);
    t->win = win;
    t->cols = cols > TCOLS ? TCOLS : cols;
    t->rows = rows > TROWS ? TROWS : rows;
    for (r = 0; r < t->rows; r++) { memset(t->ch[r], ' ', TCOLS); t->d0[r] = TCOLS; t->d1[r] = -1; }
}

static void scroll(struct term *t)      /* everything up a line; the last line empty */
{
    int r;
    memmove(t->ch[0], t->ch[1], (t->rows - 1) * sizeof t->ch[0]);
    memmove(t->at[0], t->at[1], (t->rows - 1) * sizeof t->at[0]);
    memmove(t->d0, t->d0 + 1, (t->rows - 1) * sizeof t->d0[0]);
    memmove(t->d1, t->d1 + 1, (t->rows - 1) * sizeof t->d1[0]);
    r = t->rows - 1;
    t->d0[r] = TCOLS; t->d1[r] = -1;    /* (the picture's last line is cleared when it is moved) */
    memset(t->ch[r], ' ', TCOLS); memset(t->at[r], 0, TCOLS);
    t->up++;
}

static void newline(struct term *t)
{
    if (t->row < t->rows - 1) t->row++; else scroll(t);
}

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void csi(struct term *t, int c)  /* the end of ESC [ params c */
{
    int n = t->par[0] ? t->par[0] : 1, k;
    t->wrap = 0;
    switch (c) {
    case 'H': case 'f': t->row = clamp(n - 1, 0, t->rows - 1); t->col = clamp((t->par[1] ? t->par[1] : 1) - 1, 0, t->cols - 1); break;
    case 'A': t->row = clamp(t->row - n, 0, t->rows - 1); break;
    case 'B': t->row = clamp(t->row + n, 0, t->rows - 1); break;
    case 'C': t->col = clamp(t->col + n, 0, t->cols - 1); break;
    case 'D': t->col = clamp(t->col - n, 0, t->cols - 1); break;
    case 'J':
        if (t->par[0] == 2) for (k = 0; k < t->rows; k++) blank(t, k, 0, t->cols - 1);
        else {
            blank(t, t->row, t->col, t->cols - 1);
            for (k = t->row + 1; k < t->rows; k++) blank(t, k, 0, t->cols - 1);
        }
        break;
    case 'K': blank(t, t->row, t->col, t->cols - 1); break;
    case 'm':
        for (k = 0; k < (t->npar ? t->npar : 1); k++)
            switch (t->par[k]) {
            case 0: t->attr = 0; break;
            case 1: t->attr |= 4; break;
            case 4: t->attr |= 2; break;
            case 7: t->attr |= 1; break;
            case 22: t->attr &= ~4; break;
            case 24: t->attr &= ~2; break;
            case 27: t->attr &= ~1; break;
            }
        break;
    }                                   /* (anything else: ignored) */
}

void tput(struct term *t, const unsigned char *s, int n)
{
    int c;
    while (n-- > 0) {
        c = *s++;
        if (t->esc == 1) {
            if (c == '[') { t->esc = 2; t->npar = 0; memset(t->par, 0, sizeof t->par); }
            else if (c == '_') { t->esc = 3; t->nreq = 0; }
            else t->esc = 0;            /* (ESC and anything else: dropped) */
            continue;
        }
        if (t->esc >= 3) {              /* a request, up to ESC \: handed on whole */
            if (t->esc == 4) { t->esc = 0; t->req[t->nreq] = 0; if (c == '\\') trequest(t, t->req); continue; }
            if (c == 27) t->esc = 4;
            else if (t->nreq < (int)sizeof t->req - 1) t->req[t->nreq++] = (char)c;
            continue;
        }
        if (t->esc == 2) {
            if (c >= '0' && c <= '9') { if (t->npar < 4) t->par[t->npar] = t->par[t->npar] * 10 + (c - '0'); continue; }
            if (c == ';') { if (t->npar < 4) t->npar++; continue; }
            if (t->npar < 4) t->npar++;
            t->esc = 0;
            csi(t, c);
            continue;
        }
        if (c != 27) t->wrote = 1;
        switch (c) {
        case 27: t->esc = 1; continue;
        case '\r': t->col = 0; t->wrap = 0; continue;
        case '\n': t->wrap = 0; newline(t); continue;
        case '\b': if (t->col > 0) t->col--; t->wrap = 0; continue;
        case '\t': t->col = (t->col | 7) + 1; if (t->col >= t->cols) t->col = t->cols - 1; continue;
        case 7: continue;
        }
        if (c < 0x20 || c > 0x7E) continue;
        if (t->wrap) { t->col = 0; t->wrap = 0; newline(t); }
        t->ch[t->row][t->col] = (unsigned char)c;
        t->at[t->row][t->col] = (unsigned char)t->attr;
        touch(t, t->row, t->col, t->col);
        if (t->col < t->cols - 1) t->col++; else t->wrap = 1;
    }
}

static void cursor(struct term *t)      /* the cursor's block turned over in the picture: there, or gone again */
{
    gink(G_XOR);
    gfbox(TM + 8 * t->ccol, TM + 16 * (t->rows - 1 - t->crow), TM + 8 * t->ccol + 7, TM + 16 * (t->rows - 1 - t->crow) + 15);
    gink(G_WHITE);
}

void tshow(struct term *t)
{
    char run[TCOLS + 1];
    int r, c, k, a, all = 0, w = 8 * t->cols, h = 16 * t->rows, y, was = t->cur, wr = t->crow, wc = t->ccol;
    wdraw(t->win);
    if (t->cur) { cursor(t); t->cur = 0; }      /* (the cursor out of the picture, before the picture is moved) */
    if (t->up >= t->rows) {             /* everything has gone off the top: all of it again */
        gclear();
        for (r = 0; r < t->rows; r++) { t->d0[r] = 0; t->d1[r] = (short)(t->cols - 1); }
        all = 1;
    } else if (t->up) {                 /* the picture up that far, its last lines cleared */
        gcopy(TM, TM, TM + w - 1, TM + h - 1 - 16 * t->up, TM, TM + 16 * t->up);
        gink(G_BLACK); gfbox(TM, TM, TM + w - 1, TM + 16 * t->up - 1); gink(G_WHITE);
        all = 1;
    }
    t->up = 0;
    for (r = 0; r < t->rows; r++) {
        if (t->d0[r] > t->d1[r]) continue;
        y = TM + 16 * (t->rows - 1 - r);
        for (c = t->d0[r]; c <= t->d1[r]; c = k) {      /* a run of characters written the same way at a time */
            a = t->at[r][c];
            for (k = c; k <= t->d1[r] && t->at[r][k] == a; k++) run[k - c] = (char)t->ch[r][k];
            run[k - c] = 0;
            gink(a & 1 ? G_BLACK : G_WHITE);
            glook(G_OPAQUE | (a & 2 ? G_UNDER : 0) | (a & 4 ? G_BOLD : 0));
            gtext(TM + 8 * c, y, run);
        }
        gink(G_WHITE); glook(0);
        if (!all) wpart(t->win, TM + 8 * t->d0[r], y, TM + 8 * t->d1[r] + 7, y + 15);
        t->d0[r] = TCOLS; t->d1[r] = -1;
    }
    t->crow = t->row; t->ccol = t->col; t->cur = 1;
    cursor(t);
    if (all) wshow(t->win);
    else {
        if (was) wpart(t->win, TM + 8 * wc, TM + 16 * (t->rows - 1 - wr), TM + 8 * wc + 7, TM + 16 * (t->rows - 1 - wr) + 15);
        wpart(t->win, TM + 8 * t->ccol, TM + 16 * (t->rows - 1 - t->crow), TM + 8 * t->ccol + 7, TM + 16 * (t->rows - 1 - t->crow) + 15);
    }
}

void ttext(const struct term *t, int row, char *line)
{
    int n = t->cols;
    while (n > 0 && t->ch[row][n - 1] == ' ') n--;
    memcpy(line, t->ch[row], n);
    line[n] = 0;
}
