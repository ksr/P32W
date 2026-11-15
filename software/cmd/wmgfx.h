/* wmgfx.h -- wm: a program's picture, in a window of its own (wmgfx.c) */
struct gx {
    int win;                            /* its window (0: none) */
    const char *title;                  /* the name its window is to have, or 0: Picture and a number */
    int fd;                             /* where its answers go: the terminal's master */
    int ink, look, pat, clip;           /* what the program has set: the ink and look, a pattern, a clipping rectangle */
    unsigned style;
    unsigned short rows[16];
    int c[4];
    int px, py;                         /* its pen */
    int kept[8], nkept;                 /* the pictures it has kept */
    int dirty;                          /* drawn in since it was shown */
    int mx, my, mb, mnew;               /* the mouse in it: where, the buttons, and whether that is news */
    int mwait;                          /* it waits to hear of the mouse: 50ths left, below 0 for ever (0: it does not) */
    int quiet;                          /* ... of nothing the mouse does, only of a new size (and the time being up) */
    int cansize, newsize;               /* its picture may be made another size (gmaysize); it has been, and the */
                                        /* program has not heard yet */
};
void gx_request(struct gx *g, char *req);       /* one request of the program's, done */
void gx_done(void);                     /* the requests of one terminal are over: the next begin afresh */
void gx_show(struct gx *g);             /* what has been drawn since, onto the screen */
void gx_close(struct gx *g);
void gx_mouse(struct gx *g, int x, int y, int b);       /* the mouse did something in its window */
void gx_sized(struct gx *g, int w, int h);      /* its window's corner was dragged: w by h dots wanted */
void gx_tick(struct gx *g);             /* a 50th of a second has gone by */
