/* sys/select.h -- P32W Unix: select (D45). An fd_set is one word: a process
 * has at most 20 open files (NOFILE). */
#ifndef _SYS_SELECT_H
#define _SYS_SELECT_H
#include <sys/time.h>
#define FD_SETSIZE 32
typedef struct { unsigned bits; } fd_set;
#define FD_ZERO(s)     ((s)->bits = 0)
#define FD_SET(fd, s)  ((s)->bits |= 1u << (fd))
#define FD_CLR(fd, s)  ((s)->bits &= ~(1u << (fd)))
#define FD_ISSET(fd, s) (((s)->bits >> (fd)) & 1)
int select(int nfds, fd_set *rd, fd_set *wr, fd_set *ex, struct timeval *timeout);
#endif
