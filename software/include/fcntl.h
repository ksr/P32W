/* fcntl.h -- P32W C library (M2, D22) */
#ifndef _FCNTL_H
#define _FCNTL_H
#include <sys/types.h>
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_ACCMODE 3
#define O_CREAT  0100
#define O_TRUNC  01000
#define O_APPEND 02000
#define O_EXCL   0200
#define O_NONBLOCK 04000        /* sockets (D45): fail with EAGAIN rather than wait */

/* fcntl's commands (Linux's numbers) */
#define F_DUPFD 0               /* the lowest free descriptor >= arg */
#define F_GETFD 1               /* (close-on-exec: always 0) */
#define F_SETFD 2
#define F_GETFL 3               /* the open flags */
#define F_SETFL 4               /* O_NONBLOCK and O_APPEND may change */
int open(const char *path, int flags, ...);
int creat(const char *path, mode_t mode);
int fcntl(int fd, int cmd, ...);
#endif
