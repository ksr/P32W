/* sgtty.h -- P32W Unix terminal modes (D22), Unix V7's ioctl interface. */
#ifndef _SGTTY_H
#define _SGTTY_H
struct sgttyb {
    char sg_ispeed, sg_ospeed;
    char sg_erase, sg_kill;
    int sg_flags;
};
#define CBREAK  02          /* characters at once, signals still work */
#define ECHO    010
#define CRMOD   020         /* \n out as \r\n, \r in as \n */
#define RAW     040         /* characters at once, no signals, no processing */
#define TIOCGETP 1
#define TIOCSETP 2

/* A terminal's size (D126), as 4.3BSD has it: the kernel keeps it for whoever knows it (the window manager
   for a window, telnetd for a caller, stty by hand) and tells it to whoever asks. 0: not known. Setting a
   different one sends the terminal's programs SIGWINCH. */
struct winsize {
    unsigned short ws_row, ws_col;      /* lines and columns of characters */
    unsigned short ws_xpixel, ws_ypixel;        /* ... and the same in dots */
};
#define TIOCGWINSZ 3
#define TIOCSWINSZ 4

int ioctl(int fd, int cmd, void *arg);
int gtty(int fd, struct sgttyb *b);
int stty(int fd, struct sgttyb *b);
int ttysize(int fd, int *cols, int *rows);      /* the size of the terminal fd is: what the kernel has; where that */
                                /* is not known, COLUMNS and LINES; else 80 by 24. 0, or -1 if fd is no terminal */
                                /* (the size is told all the same). Either pointer may be 0 */
#endif
