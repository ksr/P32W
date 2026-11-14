/* wmwin.h -- wm: the windows (wmwin.c). A window is opened and drawn in with the graphics library (gfx(3));
   every window's picture is kept, as much of each shown as is not covered, a window moved when its bar is
   dragged, and what the mouse and the keyboard did told (D122). */
#ifndef _WIN_H
#define _WIN_H

#define W_KEY    1              /* a key: key is its character */
#define W_DOWN   2              /* the left button went down inside window win, at its dot x, y */
#define W_UP     3              /* ... and came up (it may have left the window: x, y are from its bottom-left all the same) */
#define W_BUTTON 4              /* button id of window win was pressed */
#define W_MENU   5              /* item `item` (0 the first) of menu id was chosen */
#define W_CLOSE  6              /* the close box of window win was clicked: the program closes it, or does not */
#define W_MOVED  7              /* window win was dragged somewhere else (it is shown there already) */
#define W_DRAG   8              /* the pointer moved, the button still down since a W_DOWN in window win: its dot x, y */

struct wevent { int type, win, x, y, key, id, item; };

int  wstart(void);              /* the screen, the mouse and the keyboard taken, the desk drawn: 0, or -1 (errno) */
void wend(void);                /* ... all given back */

int  wopen(int x, int y, int w, int h, const char *title);     /* a window whose inside is w by h dots, the */
                                /* bottom-left one at x, y of the screen: its number (1 or more), or -1 */
void wclose(int win);
void wdraw(int win);            /* the graphics library draws into window win from here on: 0, 0 is the bottom-left */
                                /* dot of its inside, gwidth and gheight its size. White ink, plain look, whole */
                                /* lines, solid fills, no clipping. Nothing shows until wshow */
void wshow(int win);            /* what has been drawn in it is put on the screen, where it is not covered */
void wpart(int win, int x0, int y0, int x1, int y1);    /* ... that rectangle of it only: quicker after a small change */
void wmove(int win, int x, int y);
void wfront(int win);           /* in front of the others */
void wtitle(int win, const char *title);
int  wplace(int win, int *x, int *y, int *w, int *h);          /* where it is and how big: 0, or -1 if there is none */

int  wbutton(int win, int x, int y, int w, int h, const char *label);   /* a button in a window: its number */
void wbuttons(int win);         /* the window's buttons drawn again (after the program has cleared it) */
int  wmenu(const char *title, const char *items);       /* a menu on the bar at the top, its items one to a */
                                /* line of `items`: its number (0 the first) */

int  wevent(struct wevent *e, int wait);        /* what happened next: 1, or 0 if nothing did. wait as gmouse's: */
                                /* 0 not at all, above 0 that many 50ths of a second, below 0 until something does */
int  wtop(void);                /* the window in front (the one the keys are for), or 0 */
void wfeed(int x, int y, int buttons);          /* the pointer put at x, y of the screen with those buttons down, as if */
                                /* the mouse had done it: for a program that plays a hand */
void wpointer(int on);          /* the pointer taken off the screen (0), or put back (1) */
void wrepaint(void);            /* the whole screen made again from the windows' pictures */
#endif
