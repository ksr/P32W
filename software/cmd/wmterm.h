/* wmterm.h -- wm: a terminal in a window (wmterm.c) */
#define TCOLS 80                        /* the most a terminal has */
#define TM 2                            /* dots between the window's frame and its text: a window is 8 a column */
                                        /* and 16 a row, and twice this, in size */
#define TROWS 30
struct term {
    int win;                            /* its window */
    int cols, rows;
    int row, col, wrap;                 /* the cursor; wrap: a character waits to wrap first */
    int attr;                           /* what is written now: 1 inverse, 2 underlined, 4 bold */
    int esc, npar, par[4];              /* the escape parser: 0 text, 1 after ESC, 2 in ESC [, 3 in a request, 4 at its end */
    char req[1024];                     /* a request: what came between ESC _ and ESC \ (the most the kernel takes whole) */
    int nreq;
    unsigned char ch[TROWS][TCOLS];     /* the characters on it */
    unsigned char at[TROWS][TCOLS];     /* ... and how each is written */
    short d0[TROWS], d1[TROWS];         /* what of each row has changed since it was drawn: columns d0 to d1 (none: d0 > d1) */
    int up;                             /* lines everything has moved up since then */
    int cur, crow, ccol;                /* the cursor is drawn in the picture, and where */
    int wrote;                          /* something has been written on it (not a request) since this was last cleared */
};
void tinit(struct term *t, int win, int cols, int rows);
void tput(struct term *t, const unsigned char *s, int n);      /* what the program wrote: onto the terminal */
void tshow(struct term *t);             /* ... into the window's picture, and the changed part onto the screen */
void ttext(const struct term *t, int row, char *line);         /* a row's characters, without the spaces at its end */
void trequest(struct term *t, char *req);       /* (wm.c) a program on the terminal asks for something: wmgfx.c */
