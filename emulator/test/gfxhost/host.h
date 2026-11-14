/* host.h -- libgfx on the machine the system is built on (make test: gfx_test.c). The library's open, close,
   read, write and ioctl become dev.c's, which stand for /dev/video with the emulators' model of the chip
   behind it. This file is put in front of every source (cc -include): the real headers first, so that only
   the calls are renamed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>

int  sh_open(const char *path, int flags, ...);
int  sh_close(int fd);
long sh_read(int fd, void *buf, unsigned long n);
long sh_write(int fd, const void *buf, unsigned long n);
int  sh_ioctl(int fd, int cmd, void *arg);
unsigned sh_sleep(unsigned seconds);
int  sh_select(int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *t);
#define open  sh_open
#define close sh_close
#define read  sh_read
#define write sh_write
#define ioctl sh_ioctl
#define sleep sh_sleep
#define select sh_select

/* what a test may look at */
const unsigned short *dev_mem(void);    /* the chip's memory: word 0 the screen's top-left, 40 words a line */
int dev_bad(void);                      /* the chip had a command it does not know, or a word was lost */
unsigned long dev_words(void);          /* words written to the chip so far */
void dev_mouse(int b0, int b1, int b2); /* the mouse sends a packet (for whoever has /dev/mouse open, or opens it) */
void dev_key(int c);                    /* a key is typed (standard input) */
