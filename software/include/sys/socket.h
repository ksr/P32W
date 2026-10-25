/* sys/socket.h -- P32W Unix sockets (D45): BSD's calls and numbers, through
 * the I/O card's network coprocessor (docs/net-mailbox.md). IPv4
 * only: TCP (SOCK_STREAM), UDP (SOCK_DGRAM), and TLS as a stream socket of
 * protocol IPPROTO_TLS, its server's name given with setsockopt first. At
 * most 8 sockets in the whole system (the W5500's). */
#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H
#include <sys/types.h>

typedef unsigned socklen_t;
typedef unsigned short sa_family_t;

struct sockaddr {
    sa_family_t sa_family;
    char sa_data[14];
};

#define AF_INET     2
#define PF_INET     AF_INET
#define SOCK_STREAM 1
#define SOCK_DGRAM  2

#define SOL_SOCKET  0xFFFF
#define SO_REUSEADDR 0x0004             /* (accepted, and ignored: the coprocessor allows it) */
#define SO_KEEPALIVE 0x0008             /* (accepted, and ignored: the coprocessor keeps connections alive) */
#define SO_ERROR     0x1007             /* getsockopt: the pending error (an errno), cleared */
#define SO_TYPE      0x1008             /* getsockopt: SOCK_STREAM or SOCK_DGRAM */

#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2

#define MSG_PEEK     0x2                /* (not supported: EOPNOTSUPP) */
#define MSG_DONTWAIT 0x80               /* this call only: as if O_NONBLOCK */

int socket(int domain, int type, int protocol);
int bind(int fd, const struct sockaddr *addr, socklen_t len);
int listen(int fd, int backlog);
int accept(int fd, struct sockaddr *addr, socklen_t *len);
int connect(int fd, const struct sockaddr *addr, socklen_t len);
ssize_t send(int fd, const void *buf, size_t n, int flags);
ssize_t recv(int fd, void *buf, size_t n, int flags);
ssize_t sendto(int fd, const void *buf, size_t n, int flags, const struct sockaddr *to, socklen_t tolen);
ssize_t recvfrom(int fd, void *buf, size_t n, int flags, struct sockaddr *from, socklen_t *fromlen);
int shutdown(int fd, int how);
int setsockopt(int fd, int level, int name, const void *val, socklen_t len);
int getsockopt(int fd, int level, int name, void *val, socklen_t *len);
int getsockname(int fd, struct sockaddr *addr, socklen_t *len);
int getpeername(int fd, struct sockaddr *addr, socklen_t *len);

/* the argument blocks of sendto and recvfrom (a system call takes 4 words) */
struct __sendto { const void *buf; size_t n; int flags; const struct sockaddr *to; socklen_t tolen; };
struct __recvfrom { void *buf; size_t n; int flags; struct sockaddr *from; socklen_t *fromlen; };
#endif
