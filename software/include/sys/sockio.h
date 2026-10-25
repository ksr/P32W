/* sys/sockio.h -- P32W Unix: ioctls on sockets (D45) */
#ifndef _SYS_SOCKIO_H
#define _SYS_SOCKIO_H
#define FIONREAD 0x667F         /* bytes ready to read (int *); a listening socket: connections waiting */
#define FIONBIO  0x667E         /* non-blocking on (*(int *) != 0) or off: as fcntl's O_NONBLOCK */
#endif
