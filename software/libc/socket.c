/* socket.c -- P32W C library: the socket calls, select and fcntl (D45).
 * The kernel's are 4.2BSD's; sendto and recvfrom pass an argument block,
 * setsockopt and getsockopt the level and name in one word, and select
 * no nfds (an fd_set is one word), since a system call carries four. */
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/syscall.h>
#include <sys/netctl.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <stdarg.h>
#include <unistd.h>

int socket(int domain, int type, int protocol) { return __syscall(SYS_socket, domain, type, protocol, 0); }
int bind(int fd, const struct sockaddr *a, socklen_t len) { return __syscall(SYS_bind, fd, (int)a, len, 0); }
int listen(int fd, int backlog) { return __syscall(SYS_listen, fd, backlog, 0, 0); }
int accept(int fd, struct sockaddr *a, socklen_t *len) { return __syscall(SYS_accept, fd, (int)a, (int)len, 0); }
int connect(int fd, const struct sockaddr *a, socklen_t len) { return __syscall(SYS_connect, fd, (int)a, len, 0); }
int shutdown(int fd, int how) { return __syscall(SYS_shutdown, fd, how, 0, 0); }
int getsockname(int fd, struct sockaddr *a, socklen_t *len) { return __syscall(SYS_getsockname, fd, (int)a, (int)len, 0); }
int getpeername(int fd, struct sockaddr *a, socklen_t *len) { return __syscall(SYS_getpeername, fd, (int)a, (int)len, 0); }
int netctl(int cmd, void *arg) { return __syscall(SYS_netctl, cmd, (int)arg, 0, 0); }

ssize_t sendto(int fd, const void *buf, size_t n, int flags, const struct sockaddr *to, socklen_t tolen)
{
    struct __sendto b;
    b.buf = buf; b.n = n; b.flags = flags; b.to = to; b.tolen = tolen;
    return __syscall(SYS_sendto, fd, (int)&b, 0, 0);
}

ssize_t recvfrom(int fd, void *buf, size_t n, int flags, struct sockaddr *from, socklen_t *fromlen)
{
    struct __recvfrom b;
    b.buf = buf; b.n = n; b.flags = flags; b.from = from; b.fromlen = fromlen;
    return __syscall(SYS_recvfrom, fd, (int)&b, 0, 0);
}

ssize_t send(int fd, const void *buf, size_t n, int flags) { return sendto(fd, buf, n, flags, 0, 0); }
ssize_t recv(int fd, void *buf, size_t n, int flags) { return recvfrom(fd, buf, n, flags, 0, 0); }

int setsockopt(int fd, int level, int name, const void *val, socklen_t len)
{
    return __syscall(SYS_setsockopt, fd, level << 16 | name, (int)val, len);
}

int getsockopt(int fd, int level, int name, void *val, socklen_t *len)
{
    return __syscall(SYS_getsockopt, fd, level << 16 | name, (int)val, (int)len);
}

int select(int nfds, fd_set *rd, fd_set *wr, fd_set *ex, struct timeval *timeout)
{
    (void)nfds;
    return __syscall(SYS_select, (int)rd, (int)wr, (int)ex, (int)timeout);
}

int fcntl(int fd, int cmd, ...)
{
    va_list ap;
    int arg;
    va_start(ap, cmd);
    arg = va_arg(ap, int);
    va_end(ap);
    return __syscall(SYS_fcntl, fd, cmd, arg, 0);
}

unsigned short htons(unsigned short v) { return (unsigned short)(v << 8 | v >> 8); }
unsigned short ntohs(unsigned short v) { return (unsigned short)(v << 8 | v >> 8); }
unsigned htonl(unsigned v) { return v << 24 | (v & 0xFF00) << 8 | (v >> 8 & 0xFF00) | v >> 24; }
unsigned ntohl(unsigned v) { return htonl(v); }
