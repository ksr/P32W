/* term_test.c -- wm's terminal in a window (software/cmd/wmterm.c, D122) on the machine the system is built on,
   with wm's windows and the chip's model behind it. Text is put on two terminals by different roads (all
   at once and a piece at a time; written in order and written by moving the cursor) and the two windows' pictures
   must be the same dot for dot, and the characters what was written.
   It prints "term_test: N checks", or what failed. */
#include <stdint.h>
#include <gfx.h>
#include "wmwin.h"
#include "wmterm.h"

static int checks, fails, verbose;
static void ok(int c, const char *what) { checks++; if (!c) { fails++; printf("FAIL: %s\n", what); } else if (verbose) printf("ok: %s\n", what); }
static struct term a, b;
void trequest(struct term *t, char *req) { (void)t; (void)req; }      /* (a program's drawing: wmgfx_test.c) */
static unsigned short pa[164 * 21], pb[164 * 21];
static void put(struct term *t, const char *s) { tput(t, (const unsigned char *)s, (int)strlen(s)); }
static int same(void)                   /* the two windows' pictures, and the screen where each is */
{
    int x, y, w, h, r;
    wdraw(a.win); gread(0, 0, gwidth - 1, gheight - 1, pa);
    wdraw(b.win); gread(0, 0, gwidth - 1, gheight - 1, pb);
    r = !memcmp(pa, pb, sizeof pa);
    wpointer(0); wdraw(0);
    wplace(a.win, &x, &y, &w, &h); gread(x, y, x + w - 1, y + h - 1, pb);
    if (memcmp(pa, pb, sizeof pa)) r = 0;       /* (and what the screen shows of the first is its picture) */
    wpointer(1);
    return r;
}
static int row(struct term *t, int r, const char *want) { char l[TCOLS + 1]; ttext(t, r, l); return !strcmp(l, want); }

static unsigned short qa[200 * 40], qb[200 * 40];
static int same2(void)                  /* the same, for windows of any one size (that have been made another) */
{
    int x, y, w, h, w2, h2, r;
    memset(qa, 0, sizeof qa); memset(qb, 0, sizeof qb);
    wplace(a.win, &x, &y, &w, &h); wplace(b.win, 0, 0, &w2, &h2);
    if (w != w2 || h != h2 || h > 200) return 0;
    wdraw(a.win); gread(0, 0, w - 1, h - 1, qa);
    wdraw(b.win); gread(0, 0, w - 1, h - 1, qb);
    r = !memcmp(qa, qb, sizeof qa);
    wpointer(0); wdraw(0);
    memset(qb, 0, sizeof qb); gread(x, y, x + w - 1, y + h - 1, qb);
    if (memcmp(qa, qb, sizeof qa)) r = 0;
    wpointer(1);
    return r;
}
#define SIZE(t, c, r) (wresize((t)->win, 8 * (c) + 2 * TM, 16 * (r) + 2 * TM), tresize(t, c, r))

int main(int argc, char **argv)
{
    char line[100];
    int k;
    (void)argv; verbose = argc > 1;
    ok(wstart() == 0, "wstart");
    tinit(&a, wopen(8, 8, 320 + 2 * TM, 160 + 2 * TM, "a"), 40, 10);
    tinit(&b, wopen(8, 220, 320 + 2 * TM, 160 + 2 * TM, "b"), 40, 10);
    wfront(a.win);
    put(&a, "hello, world\r\nsecond line"); tshow(&a);
    put(&b, "\033[2;1Hsecond line\033[1;1Hhello, world\033[2;12H"); tshow(&b);
    ok(row(&a, 0, "hello, world") && row(&a, 1, "second line") && a.row == 1 && a.col == 11, "text, a new line: the characters and the cursor");
    ok(same(), "written in order, and by moving the cursor: the same picture");
    put(&a, "\033[1m bold\033[0m \033[4munder\033[24m \033[7minverse\033[27m\ttab"); tshow(&a);
    for (k = 0; k < 4; k++) { static const char *p[] = { "\033[1m bold", "\033[0m \033[4munder", "\033[0m \033[7minverse\033[0m", "\ttab" }; put(&b, p[k]); tshow(&b); }
    ok(same() && row(&a, 1, "second line bold under inverse  tab"), "bold, underlined, inverse, a tab; all at once and a piece at a time");
    put(&a, "\r\n"); put(&b, "\r\n");
    for (k = 0; k < 25; k++) {          /* 25 lines through 10 rows: one at once, the other a line at a time */
        sprintf(line, "line %d of twenty-five\r\n", k);
        put(&a, line);
        put(&b, line); tshow(&b);
    }
    tshow(&a);
    ok(row(&a, 0, "line 16 of twenty-five") && row(&a, 8, "line 24 of twenty-five") && row(&a, 9, "") && a.row == 9 && a.col == 0, "lines scrolled off the top: the last nine and an empty one");
    ok(same(), "scrolled all at once, and a line at a time: the same picture");
    put(&a, "0123456789012345678901234567890123456789X"); put(&b, "0123456789012345678901234567890123456789\r\nX"); tshow(&a); tshow(&b);
    ok(same() && row(&a, 9, "X") && row(&a, 8, "0123456789012345678901234567890123456789"), "a line longer than the window goes on on the next");
    put(&a, "\033[5;10Hmiddle\033[K\033[3;1H\033[Jgone below\b\b\b\b\bX"); tshow(&a);
    put(&b, "\033[3;1Hgone Xelow\033[K"); for (k = 3; k < 10; k++) { sprintf(line, "\033[%d;1H\033[K", k + 1); put(&b, line); }
    put(&b, "\033[3;7H"); tshow(&b);
    ok(same() && row(&a, 2, "gone Xelow") && row(&a, 4, "") && row(&a, 9, ""), "ESC [ K, ESC [ J, backspace");
    put(&a, "\033[2J\033[Hcleared"); put(&b, "\033[H\033[Jcleared"); tshow(&a); tshow(&b);
    ok(same() && row(&a, 0, "cleared") && row(&a, 1, ""), "ESC [ 2J and ESC [ H ESC [ J");
    put(&a, "\033[99;99H*\033[A\033[D\033[D+\033[5B\033[20C-"); put(&b, "\033[10;40H*\033[9;38H+\033[10;40H-"); tshow(&a); tshow(&b);
    ok(same(), "the cursor stops at the edges");
    for (k = 0; k < 200; k++) { sprintf(line, "\r\n%d", k); put(&a, line); put(&b, line); if (k % 7 == 0) tshow(&b); }
    tshow(&a); tshow(&b);
    ok(same() && row(&a, 9, "199") && row(&a, 0, "190"), "200 more lines, shown now and then: the same");
    /* another size (its window's corner dragged, D126): what was written stays, from the top-left */
    put(&a, "\033[2J\033[Hone\r\ntwo\r\nthree\r\nfour\r\nfive"); tshow(&a);
    SIZE(&a, 30, 3); tshow(&a);
    ok(a.rows == 3 && a.cols == 30 && row(&a, 0, "three") && row(&a, 2, "five") && a.row == 2 && a.col == 4,
       "made smaller: the lines above go, so that the cursor's is the last");
    put(&b, "\033[2J\033[H"); SIZE(&b, 30, 3); put(&b, "three\r\nfour\r\nfive"); tshow(&b);
    ok(same2(), "... its picture that of a terminal of that size with those lines written on it");
    SIZE(&a, 50, 8); tshow(&a); put(&a, "\r\nsix"); tshow(&a);
    SIZE(&b, 50, 8); put(&b, "\r\nsix"); tshow(&b);
    ok(row(&a, 0, "three") && row(&a, 3, "six") && row(&a, 4, "") && a.row == 3 && same2(), "made bigger: the lines where they were, and room below them");
    put(&a, "\033[1;1H0123456789012345678901234567890123456789\033[4;4H"); tshow(&a);
    SIZE(&a, 20, 8); tshow(&a);
    k = row(&a, 0, "01234567890123456789") && a.col == 3;
    SIZE(&a, 50, 8); tshow(&a);
    put(&b, "\033[1;1H01234567890123456789\033[K\033[4;4H"); tshow(&b);
    ok(k && row(&a, 0, "01234567890123456789") && same2(), "made narrower and wider again: what was cut off does not come back");
    put(&a, "\033[8;1Hlast\033[2;50H*"); tshow(&a); SIZE(&a, 20, 4); tshow(&a);
    ok(a.row == 1 && a.col == 19 && row(&a, 0, "01234567890123456789") && row(&a, 3, "six"), "the cursor is brought inside the new size");
    ok(!dev_bad(), "the chip had no command it does not know and lost no word");
    wend();
    if (fails) printf("%d of %d checks failed\n", fails, checks); else printf("term_test: %d checks\n", checks);
    return fails != 0;
}
