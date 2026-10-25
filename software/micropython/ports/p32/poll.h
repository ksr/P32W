// poll.h -- for MicroPython's POSIX files on P32W Unix, which has no poll
// system call: every descriptor is reported ready. (Only a file object's
// "is it readable / writable" ioctl asks; there is no select module.)
#ifndef P32_POLL_H
#define P32_POLL_H
#define POLLIN   0x001
#define POLLPRI  0x002
#define POLLOUT  0x004
#define POLLERR  0x008
#define POLLHUP  0x010
#define POLLNVAL 0x020
struct pollfd { int fd; short events, revents; };
static inline int poll(struct pollfd *fds, unsigned int nfds, int timeout) {
    unsigned int k;
    (void)timeout;
    for (k = 0; k < nfds; k++) {
        fds[k].revents = fds[k].events & (POLLIN | POLLOUT);
    }
    return (int)nfds;
}
#endif
