/* sqlui.h -- what sqlui.c (the screens) and sqluiscr.c (the terminal, the
 * screen, editing a line, SQL text) share. See sqlui(1). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "sqldb.h"

#define MAXW    30                      /* widest column in the grid */
#define MAXV    1000                    /* longest value */
#define MAXC    64                      /* most columns (a join of two tables) */
#define A_REV   1                       /* attributes inside a line */
#define A_BOLD  2
#define A_OFF   3
#define K_UP    0x101                   /* keys made of escape sequences */
#define K_DOWN  0x102
#define K_RIGHT 0x103
#define K_LEFT  0x104
#define K_PGUP  0x105
#define K_PGDN  0x106
#define K_HOME  0x107
#define K_END   0x108
#define K_DEL   0x109
#define ESC     27
#define CTL(c)  ((c) & 31)

struct buf { char *s; int n, cap; };    /* a growing string */

extern sqldb *db;
extern char *dbname;
extern int R, C;                        /* the screen's rows and columns */
extern char msg[256];                   /* for the status line */
extern int lv;                          /* the width of the line being made */

void scrinit(void);                     /* the terminal: modes, signals */
void done(int status);                  /* put it back and exit */
void *xmalloc(int n);
char *xstrdup(const char *s);
int getkey(void);                       /* a character or a K_ key */
void gorc(int r, int c);
void forget(int r);
void clearscr(void);
void lbeg(void);                        /* make a line: */
void lattr(int a);
void lstr(const char *s, int n);
void lpad(int x);
void lend(int r);                       /* show it as line r */
void status(const char *scr, const char *info);
void keys(const char *s);
void error(const char *s);
int edit(char *s, int max, int r, int x, int w, int *pos, int *nul);
int lineedit(const char *label, char *s, int max, int keep);
int ask(const char *q);
void view(const char *title, char **ln, int n);
void add(struct buf *b, const char *s, int n);
void addname(struct buf *b, const char *s);
void addval(struct buf *b, const char *s, int nul, int isint);
void firstword(const char *s, char *w);
int isquery(const char *s);
int count(const char *q);
