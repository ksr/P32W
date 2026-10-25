/* errno.h -- P32W C library (M2, D22): Unix V7's numbers. */
#ifndef _ERRNO_H
#define _ERRNO_H
extern int errno;
#define EPERM    1
#define ENOENT   2
#define ESRCH    3
#define EINTR    4
#define EIO      5
#define ENXIO    6
#define E2BIG    7
#define ENOEXEC  8
#define EBADF    9
#define ECHILD  10
#define EAGAIN  11
#define ENOMEM  12
#define EACCES  13
#define EFAULT  14
#define ENOTBLK 15
#define EBUSY   16
#define EEXIST  17
#define EXDEV   18
#define ENODEV  19
#define ENOTDIR 20
#define EISDIR  21
#define EINVAL  22
#define ENFILE  23
#define EMFILE  24
#define ENOTTY  25
#define ETXTBSY 26
#define EFBIG   27
#define ENOSPC  28
#define ESPIPE  29
#define EROFS   30
#define EMLINK  31
#define EPIPE   32
#define EDOM    33
#define ERANGE  34
#define ENAMETOOLONG 36
#define ENOSYS  38
#define ENOTEMPTY 39
#define EWOULDBLOCK EAGAIN              /* (POSIX: the same error) */
/* networking's (Linux's numbers; D54's sockets, and MicroPython's errno module) */
#define ENOTSOCK     88                 /* not a socket */
#define EDESTADDRREQ 89                 /* no address to send to */
#define EMSGSIZE     90
#define EPROTOTYPE   91
#define ENOPROTOOPT  92                 /* no such socket option */
#define EPROTONOSUPPORT 93
#define EOPNOTSUPP   95                 /* operation not supported */
#define EAFNOSUPPORT 97
#define EADDRINUSE   98
#define ECONNABORTED 103
#define ECONNRESET   104
#define ENETDOWN     100                /* no network: the coprocessor is not there, or no link */
#define ENOBUFS      105
#define EISCONN      106
#define ENOTCONN     107
#define ETIMEDOUT    110
#define ECONNREFUSED 111
#define EHOSTUNREACH 113
#define EALREADY     114
#define EINPROGRESS  115
#endif
