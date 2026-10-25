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
int ioctl(int fd, int cmd, void *arg);
int gtty(int fd, struct sgttyb *b);
int stty(int fd, struct sgttyb *b);
#endif
