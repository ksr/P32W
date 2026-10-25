/* net.c -- sockets, through the I/O card's network coprocessor (D45;
 * docs/net-mailbox.md, docs/net-design.md). The coprocessor (an RP2040
 * with a W5500) does TCP/IP and TLS; P32W Unix keeps the socket as a file
 * descriptor and the BSD calls. So there is no protocol code here, only:
 *
 * - the mailbox: a request and its reply, byte by byte through NDATA,
 *   polled (the coprocessor answers within microseconds). A whole exchange
 *   runs without sleeping, and the kernel is not pre-empted, so two
 *   processes never interleave on the mailbox. The interrupt handler never
 *   touches it.
 * - waiting: a socket that cannot go on (no data yet, a connection still
 *   being made) sleeps until the coprocessor raises ATTN (IRQ7), then looks
 *   again. Every sleeper wakes at every event and checks its own socket:
 *   with 8 sockets at most, simple beats clever. A tick every 200 ms wakes
 *   them too, in case an event is ever missed.
 * - a socket per coprocessor socket: the coprocessor numbers them 0-7, and
 *   socks[] is indexed by that number.
 * - select, over terminals, pipes and sockets alike.
 *
 * The system calls follow 4.2BSD's; sendto and recvfrom take an argument
 * block, as a system call carries four words. */
#include "defs.h"
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/netctl.h>
#include <sys/sockio.h>

#define NDATA IOB(NETBASE)
#define NSTAT IOB(NETBASE + 1)
#define NCTRL IOB(NETBASE + 1)
#define RXF   0x01
#define TXE   0x02
#define ATTN  0x04
#define IEN   0x08
#define READY 0x80

/* commands */
#define C_HELLO    0x01
#define C_IFINFO   0x02
#define C_IFCONFIG 0x03
#define C_EVENTS   0x04
#define C_SOCKET   0x10
#define C_CONNECT  0x11
#define C_BIND     0x12
#define C_LISTEN   0x13
#define C_ACCEPT   0x14
#define C_SEND     0x15
#define C_RECV     0x16
#define C_SENDTO   0x17
#define C_RECVFROM 0x18
#define C_SHUTDOWN 0x19
#define C_CLOSE    0x1A
#define C_STATE    0x1B

/* the coprocessor's socket types and states */
#define T_TCP 1
#define T_UDP 2
#define T_TLS 3
#define T_DNS 4
#define T_ICMP 5
#define S_FREE 0
#define S_NEW 1
#define S_CONNECTING 2
#define S_CONNECTED 3
#define S_LISTENING 4
#define S_PEERCLOSED 5
#define S_FAILED 6
#define S_BOUND 7

#define NCSOCK 8                        /* the coprocessor's sockets */
#define MAXPAY 1024                     /* a message's payload, at most */
#define SPIN   200000                   /* polls of a latch before the coprocessor counts as gone */

struct sock {
    int used;
    int type;                           /* SOCK_STREAM or SOCK_DGRAM */
    int tls;
    uint daddr, dport;                  /* UDP: connect's address (network order); hasdest */
    int hasdest;
    char host[128];                     /* TLS: the server's name (SNI, and the certificate's check) */
};
static struct sock socks[NCSOCK];

static int netok;                       /* the coprocessor answered HELLO */
static int nsleep;                      /* processes waiting for an event (net or select) */
static uchar rep[MAXPAY];               /* the last reply's payload */
static int rlen, rsock;
static uchar req[MAXPAY + 8];           /* a request's payload being built */
int selchan;                            /* select's sleepers wait on this; tty input and the clock wake it */
static int nselect;

/* the status codes as errno values (net-mailbox.md section 5) */
static const short errmap[] = {
    0, EAGAIN, EINVAL, ENOBUFS, ECONNREFUSED, ETIMEDOUT, EHOSTUNREACH, ECONNRESET,
    ENOTCONN, EADDRINUSE, ENOENT, ECONNABORTED, ENETDOWN, EMSGSIZE, EINVAL,
};
static int nerr(int st) { return st > 0 && st < (int)(sizeof errmap / sizeof errmap[0]) ? -errmap[st] : -EIO; }

static uint g32(const uchar *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint)p[3] << 24; }   /* (as stored: network order stays) */
static uint g16le(const uchar *p) { return p[0] | p[1] << 8; }

/* ---------------------------------------------------------------- the mailbox */
static int gone(void)
{
    if (netok) kprintf("net: the coprocessor does not answer\n");
    netok = 0;
    return -1;
}

/* one exchange: the reply's status code (its payload in rep, rlen bytes;
   its socket byte in rsock), or -1 if the coprocessor has gone quiet */
static int netreq(int cmd, int sock, const void *pay, int len)
{
    const uchar *p = pay;
    uchar h[4];
    int k, n;
    if (!netok) return -1;
    h[0] = cmd; h[1] = sock; h[2] = len; h[3] = len >> 8;
    for (k = 0; k < 4 + len; k++) {
        for (n = 0; !(NSTAT & TXE); ) if (++n > SPIN) return gone();
        NDATA = k < 4 ? h[k] : p[k - 4];
    }
    for (k = 0; k < 4; k++) {
        for (n = 0; !(NSTAT & RXF); ) if (++n > SPIN) return gone();
        h[k] = NDATA;
    }
    rsock = h[1];
    rlen = h[2] | h[3] << 8;
    if (rlen > MAXPAY) return gone();
    for (k = 0; k < rlen; k++) {
        for (n = 0; !(NSTAT & RXF); ) if (++n > SPIN) return gone();
        rep[k] = NDATA;
    }
    return h[0];
}

/* a request whose answer is only a status: 0 or -errno */
static int netdo(int cmd, int sock, const void *pay, int len)
{
    int st = netreq(cmd, sock, pay, len);
    if (st < 0) return -ENETDOWN;
    return st ? nerr(st) : 0;
}

/* the coprocessor, at boot: there, and speaking version 1? */
int net_init(void)
{
    int k;
    if ((NSTAT & 0xC0) != READY) return -1;            /* no coprocessor running (bit 6 always reads 0) */
    NCTRL = 0x80;                                       /* (a clean start: RESET empties the latches) */
    for (k = 0; k < SPIN && (NSTAT & 0xC0) != READY; k++) ;
    netok = 1;
    if (netreq(C_HELLO, 0, 0, 0) != 0 || rlen < 8 || memcmp(rep, "P32NET 1", 8) != 0) {
        netok = 0;
        kprintf("net: the coprocessor speaks another protocol\n");
        return -1;
    }
    mtcr_imask(mfcr_imask() | 1 << IRQ_NET);
    if (netreq(C_IFINFO, 0, 0, 0) == 0 && rlen >= 24)
        kprintf("net: %d.%d.%d.%d%s\n", rep[8], rep[9], rep[10], rep[11], rep[0] & 1 ? "" : " (no link)");
    return 0;
}

/* IRQ7: ATTN. Mask it at the source (it stays up until EVENTS) and wake
   the sleepers, who send EVENTS themselves, in process context */
void netintr(void)
{
    NCTRL = 0;
    wakeup(&netok);
    wakeup(&selchan);
}

/* the clock's help: every 200 ms while anyone waits (an event missed, or
   select's timeout); every tick for select */
void net_tick(void)
{
    if (nsleep && ticks % (HZ / 5) == 0) wakeup(&netok);
    if (nselect) wakeup(&selchan);
}

static void events(void)
{
    if (NSTAT & ATTN) netreq(C_EVENTS, 0, 0, 0);
}

/* wait for the coprocessor's next event: 0, or -EINTR for a signal */
static int netwait(void)
{
    int s = splhi();
    if (!(NSTAT & ATTN)) {
        NCTRL = IEN;
        nsleep++;
        sleep(&netok);
        nsleep--;
    }
    splx(s);
    events();
    return curproc->killed ? -EINTR : 0;
}

/* a socket's state (STATE): 0 with st[] filled (state, error, bytes ready,
   room, local port, peer address, peer port), or -errno */
struct cstate { int state, err; uint ready, room, lport, paddr, pport; };
static int cstate(int cs, struct cstate *c)
{
    int st = netreq(C_STATE, cs, 0, 0);
    if (st < 0) return -ENETDOWN;
    if (st) return nerr(st);
    c->state = rep[0];
    c->err = rep[1];
    c->ready = g16le(rep + 2);
    c->room = g16le(rep + 4);
    c->lport = rep[6] | rep[7] << 8;
    c->paddr = g32(rep + 8);
    c->pport = rep[12] | rep[13] << 8;
    return 0;
}

/* ---------------------------------------------------------------- sockets as files */
static int sockof(int fd, struct file **fp, struct sock **sp)
{
    int r = fdget(fd, fp);
    if (r < 0) return r;
    if ((*fp)->type != FD_SOCKET) return -ENOTSOCK;
    *sp = (*fp)->sock;
    return (*sp - socks);               /* the coprocessor's number */
}

static int nonblock(struct file *f, int flags) { return (f->flags & O_NONBLOCK) || (flags & MSG_DONTWAIT); }

void sock_close(struct sock *s)
{
    netdo(C_CLOSE, s - socks, 0, 0);
    s->used = 0;
}

/* a new file for the coprocessor's socket cs: its descriptor, or -errno */
static int sockfile(int cs, int type, int tls)
{
    struct file *f;
    int fd;
    struct sock *s = &socks[cs];
    memset(s, 0, sizeof *s);
    s->used = 1;
    s->type = type;
    s->tls = tls;
    if (!(f = filealloc())) { sock_close(s); return -ENFILE; }
    f->type = FD_SOCKET;
    f->sock = s;
    f->flags = O_RDWR;
    if ((fd = fdalloc(f)) < 0) { fileclose(f); return fd; }
    return fd;
}

/* the data of a stream socket: up to n bytes into dst, or 0 at the end, or -errno */
static int sock_recv(struct file *f, struct sock *s, uint dst, int n, int flags)
{
    int cs = s - socks, st;
    uchar m[2];
    if (n <= 0) return 0;
    if (n > MAXPAY) n = MAXPAY;
    for (;;) {
        m[0] = n; m[1] = n >> 8;
        if (s->type == SOCK_DGRAM) st = netreq(C_RECVFROM, cs, m, 2);
        else st = netreq(C_RECV, cs, m, 2);
        if (st < 0) return -ENETDOWN;
        if (st == 0) {
            if (s->type == SOCK_DGRAM) {        /* (read on a datagram socket: the sender dropped) */
                if (copyout(dst, rep + 6, rlen - 6) < 0) return -EFAULT;
                return rlen - 6;
            }
            if (copyout(dst, rep, rlen) < 0) return -EFAULT;
            return rlen;
        }
        if (st != 1) return nerr(st);
        if (nonblock(f, flags)) return -EAGAIN;
        if ((st = netwait()) < 0) return st;
    }
}

/* n bytes of a stream socket from src: n, or as many as went before an
   error or a signal, or -errno */
static int sock_send(struct file *f, struct sock *s, uint src, int n, int flags)
{
    int cs = s - socks, k = 0, st;
    if (s->type == SOCK_DGRAM) {
        if (!s->hasdest) return -EDESTADDRREQ;
        if (n > MAXPAY - 6) return -EMSGSIZE;
        memcpy(req, &s->daddr, 4);
        memcpy(req + 4, &s->dport, 2);
        if (copyin(req + 6, src, n) < 0) return -EFAULT;
        st = netreq(C_SENDTO, cs, req, n + 6);
        return st < 0 ? -ENETDOWN : st ? nerr(st) : n;
    }
    while (k < n) {
        int c = n - k > MAXPAY ? MAXPAY : n - k;
        if (copyin(req, src + k, c) < 0) return k ? k : -EFAULT;
        st = netreq(C_SEND, cs, req, c);
        if (st < 0) return k ? k : -ENETDOWN;
        if (st == 0) { k += g16le(rep); continue; }
        if (st != 1) {
            if (st == 7 || st == 8) psignal(curproc, SIGPIPE);     /* (RESET, NOTCONN: as a pipe without a reader) */
            return k ? k : (st == 7 || st == 8 ? -EPIPE : nerr(st));
        }
        if (nonblock(f, flags)) return k ? k : -EAGAIN;
        if ((st = netwait()) < 0) return k ? k : st;
    }
    return n;
}

int sock_read(struct file *f, uint dst, int n) { return sock_recv(f, f->sock, dst, n, 0); }
int sock_write(struct file *f, uint src, int n) { return sock_send(f, f->sock, src, n, 0); }

/* ---------------------------------------------------------------- the system calls */
/* socket(domain, type, protocol) */
int sys_socket(void)
{
    int type = arg(1), proto = arg(2), ctype, st;
    uchar t;
    if (arg(0) != AF_INET) return -EAFNOSUPPORT;
    if (type == SOCK_STREAM && (proto == 0 || proto == IPPROTO_TCP)) ctype = T_TCP;
    else if (type == SOCK_STREAM && proto == IPPROTO_TLS) ctype = T_TLS;
    else if (type == SOCK_DGRAM && (proto == 0 || proto == IPPROTO_UDP)) ctype = T_UDP;
    else if (type == SOCK_DGRAM && proto == IPPROTO_ICMP) ctype = T_ICMP;     /* (ping: whole ICMP messages) */
    else if (type == SOCK_STREAM || type == SOCK_DGRAM) return -EPROTONOSUPPORT;
    else return -EPROTOTYPE;
    if (!netok) return -ENETDOWN;
    t = ctype;
    if ((st = netreq(C_SOCKET, 0, &t, 1)) != 0) return st < 0 ? -ENETDOWN : nerr(st);
    if (rsock >= NCSOCK || socks[rsock].used) { netdo(C_CLOSE, rsock, 0, 0); return -EIO; }
    return sockfile(rsock, type, ctype == T_TLS);
}

/* an address from the user: 0 with addr and port (network order), or -errno */
static int getaddr(uint ua, uint len, uint *addr, uint *port)
{
    struct sockaddr_in a;
    if (len < sizeof a) return -EINVAL;
    if (copyin(&a, ua, sizeof a) < 0) return -EFAULT;
    if (a.sin_family != AF_INET) return -EAFNOSUPPORT;
    *addr = a.sin_addr.s_addr;
    *port = a.sin_port;
    return 0;
}

/* an address to the user (addr, port in network order) with its length */
static int putaddr(uint ua, uint lenp, uint addr, uint port)
{
    struct sockaddr_in a;
    uint len;
    if (!ua) return 0;
    if (copyin(&len, lenp, 4) < 0) return -EFAULT;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = port;
    a.sin_addr.s_addr = addr;
    if (len > sizeof a) len = sizeof a;
    if (copyout(ua, &a, len) < 0) return -EFAULT;
    len = sizeof a;
    return copyout(lenp, &len, 4);
}

static uint swap16(uint v) { return (v >> 8 & 0xFF) | (v & 0xFF) << 8; }

/* bind(fd, addr, len): the port only (the coprocessor has one address) */
int sys_bind(void)
{
    struct file *f;
    struct sock *s;
    uint addr, port;
    int cs = sockof(arg(0), &f, &s), r;
    uchar p[2];
    if (cs < 0) return cs;
    if ((r = getaddr(arg(1), arg(2), &addr, &port)) < 0) return r;
    if (s->tls) return -EOPNOTSUPP;
    if (swap16(port) < 1024 && swap16(port) != 0 && curproc->euid) return -EACCES;    /* (Unix's reserved ports) */
    memcpy(p, &port, 2);
    return netdo(C_BIND, cs, p, 2);
}

int sys_listen(void)
{
    struct file *f;
    struct sock *s;
    int cs = sockof(arg(0), &f, &s);
    uchar b;
    if (cs < 0) return cs;
    if (s->type != SOCK_STREAM || s->tls) return -EOPNOTSUPP;
    b = (int)arg(1) < 1 ? 1 : (int)arg(1) > 8 ? 8 : arg(1);
    return netdo(C_LISTEN, cs, &b, 1);
}

/* accept(fd, addr, lenp): a new descriptor for the next connection */
int sys_accept(void)
{
    struct file *f;
    struct sock *s;
    int cs = sockof(arg(0), &f, &s), st, fd;
    uint paddr, pport;
    if (cs < 0) return cs;
    for (;;) {
        if ((st = netreq(C_ACCEPT, cs, 0, 0)) < 0) return -ENETDOWN;
        if (st == 0) break;
        if (st != 1) return nerr(st);
        if (f->flags & O_NONBLOCK) return -EAGAIN;
        if ((st = netwait()) < 0) return st;
    }
    paddr = g32(rep);
    pport = rep[4] | rep[5] << 8;
    if (rsock >= NCSOCK || socks[rsock].used) { netdo(C_CLOSE, rsock, 0, 0); return -EIO; }
    if ((fd = sockfile(rsock, SOCK_STREAM, 0)) < 0) return fd;
    if (putaddr(arg(1), arg(2), paddr, pport) < 0) return -EFAULT;
    return fd;
}

/* connect(fd, addr, len): a stream socket's connection (waiting for it,
   unless O_NONBLOCK: EINPROGRESS); a datagram socket's address to send to */
int sys_connect(void)
{
    struct file *f;
    struct sock *s;
    struct cstate c;
    uint addr, port;
    int cs = sockof(arg(0), &f, &s), r, n = 6;
    if (cs < 0) return cs;
    if ((r = getaddr(arg(1), arg(2), &addr, &port)) < 0) return r;
    if (s->type == SOCK_DGRAM) {
        s->daddr = addr;
        s->dport = port;
        s->hasdest = 1;
        return 0;
    }
    if ((r = cstate(cs, &c)) < 0) return r;
    if (c.state == S_CONNECTED || c.state == S_PEERCLOSED) return -EISCONN;
    if (c.state == S_CONNECTING) return f->flags & O_NONBLOCK ? -EALREADY : -EINPROGRESS;
    memcpy(req, &addr, 4);
    memcpy(req + 4, &port, 2);
    if (s->tls) {                       /* the server's name, for SNI and the certificate */
        n += strlen(s->host);
        memcpy(req + 6, s->host, n - 6);
    }
    if ((r = netdo(C_CONNECT, cs, req, n)) < 0) return r;
    for (;;) {
        if ((r = cstate(cs, &c)) < 0) return r;
        if (c.state == S_CONNECTED || c.state == S_PEERCLOSED) return 0;
        if (c.state == S_FAILED) return nerr(c.err);
        if (f->flags & O_NONBLOCK) return -EINPROGRESS;
        if ((r = netwait()) < 0) return r;
    }
}

/* sendto(fd, block): struct __sendto */
int sys_sendto(void)
{
    struct file *f;
    struct sock *s;
    struct __sendto b;
    uint addr, port;
    int cs = sockof(arg(0), &f, &s), r, n;
    if (cs < 0) return cs;
    if (copyin(&b, arg(1), sizeof b) < 0) return -EFAULT;
    if ((int)b.n < 0) return -EINVAL;
    if (s->type == SOCK_STREAM || !b.to) return sock_send(f, s, (uint)b.buf, b.n, b.flags);
    if ((r = getaddr((uint)b.to, b.tolen, &addr, &port)) < 0) return r;
    n = b.n;
    if (n > MAXPAY - 6) return -EMSGSIZE;
    memcpy(req, &addr, 4);
    memcpy(req + 4, &port, 2);
    if (copyin(req + 6, (uint)b.buf, n) < 0) return -EFAULT;
    for (;;) {
        r = netreq(C_SENDTO, cs, req, n + 6);
        if (r < 0) return -ENETDOWN;
        if (r == 0) return n;
        if (r != 1) return nerr(r);
        if (nonblock(f, b.flags)) return -EAGAIN;
        if ((r = netwait()) < 0) return r;
    }
}

/* recvfrom(fd, block): struct __recvfrom */
int sys_recvfrom(void)
{
    struct file *f;
    struct sock *s;
    struct __recvfrom b;
    int cs = sockof(arg(0), &f, &s), r;
    uchar m[2];
    if (cs < 0) return cs;
    if (copyin(&b, arg(1), sizeof b) < 0) return -EFAULT;
    if ((int)b.n < 0) return -EINVAL;
    if (b.flags & MSG_PEEK) return -EOPNOTSUPP;
    if (s->type == SOCK_STREAM) {
        struct cstate c;
        r = sock_recv(f, s, (uint)b.buf, b.n, b.flags);
        if (r >= 0 && b.from && cstate(cs, &c) == 0 && putaddr((uint)b.from, (uint)b.fromlen, c.paddr, swap16(c.pport)) < 0) return -EFAULT;
        return r;
    }
    if (b.n > MAXPAY - 6) b.n = MAXPAY - 6;
    for (;;) {
        m[0] = b.n; m[1] = b.n >> 8;
        r = netreq(C_RECVFROM, cs, m, 2);
        if (r < 0) return -ENETDOWN;
        if (r == 0) break;
        if (r != 1) return nerr(r);
        if (nonblock(f, b.flags)) return -EAGAIN;
        if ((r = netwait()) < 0) return r;
    }
    r = rlen - 6;
    if (copyout((uint)b.buf, rep + 6, r) < 0) return -EFAULT;
    if (b.from && putaddr((uint)b.from, (uint)b.fromlen, g32(rep), rep[4] | rep[5] << 8) < 0) return -EFAULT;
    return r;
}

int sys_shutdown(void)
{
    struct file *f;
    struct sock *s;
    int cs = sockof(arg(0), &f, &s), how = arg(1);
    if (cs < 0) return cs;
    if (how < 0 || how > 2) return -EINVAL;
    if (how == SHUT_RD || s->type == SOCK_DGRAM) return 0;
    return netdo(C_SHUTDOWN, cs, 0, 0);
}

/* setsockopt(fd, level << 16 | name, val, len) */
int sys_setsockopt(void)
{
    struct file *f;
    struct sock *s;
    int cs = sockof(arg(0), &f, &s), level = arg(1) >> 16, name = arg(1) & 0xFFFF;
    uint len = arg(3);
    if (cs < 0) return cs;
    if (level == IPPROTO_TLS && name == TLS_HOSTNAME) {
        if (!s->tls) return -ENOPROTOOPT;
        if (len >= sizeof s->host) return -EINVAL;
        if (copyin(s->host, arg(2), len) < 0) return -EFAULT;
        s->host[len] = 0;
        return 0;
    }
    if (level == SOL_SOCKET && (name == SO_REUSEADDR || name == SO_KEEPALIVE)) return 0;
    if (level == IPPROTO_TCP) return 0;  /* (TCP_NODELAY and the like: the coprocessor decides) */
    return -ENOPROTOOPT;
}

/* getsockopt(fd, level << 16 | name, val, lenp) */
int sys_getsockopt(void)
{
    struct file *f;
    struct sock *s;
    struct cstate c;
    int cs = sockof(arg(0), &f, &s), name = arg(1) & 0xFFFF, v, r;
    uint len = 4;
    if (cs < 0) return cs;
    if ((arg(1) >> 16) != SOL_SOCKET) return -ENOPROTOOPT;
    if (name == SO_ERROR) {
        if ((r = cstate(cs, &c)) < 0) return r;
        v = c.state == S_FAILED ? -nerr(c.err) : 0;
    } else if (name == SO_TYPE) v = s->type;
    else if (name == SO_REUSEADDR || name == SO_KEEPALIVE) v = 1;
    else return -ENOPROTOOPT;
    if (copyout(arg(2), &v, 4) < 0 || copyout(arg(3), &len, 4) < 0) return -EFAULT;
    return 0;
}

static int names(int peer)
{
    struct file *f;
    struct sock *s;
    struct cstate c;
    int cs = sockof(arg(0), &f, &s), r;
    uint me;
    if (cs < 0) return cs;
    if ((r = cstate(cs, &c)) < 0) return r;
    if (peer) {
        if (c.state != S_CONNECTED && c.state != S_PEERCLOSED) {
            if (!s->hasdest) return -ENOTCONN;
            return putaddr(arg(1), arg(2), s->daddr, s->dport);
        }
        return putaddr(arg(1), arg(2), c.paddr, swap16(c.pport));
    }
    if (netreq(C_IFINFO, 0, 0, 0) != 0 || rlen < 24) return -ENETDOWN;
    me = g32(rep + 8);
    return putaddr(arg(1), arg(2), c.lport ? me : 0, swap16(c.lport));
}
int sys_getsockname(void) { return names(0); }
int sys_getpeername(void) { return names(1); }

/* ioctl on a socket: FIONREAD, FIONBIO */
int sock_ioctl(struct file *f, int cmd, uint arg)
{
    struct cstate c;
    int r, v;
    if (cmd == FIONBIO) {
        if (copyin(&v, arg, 4) < 0) return -EFAULT;
        if (v) f->flags |= O_NONBLOCK; else f->flags &= ~O_NONBLOCK;
        return 0;
    }
    if (cmd != FIONREAD) return -ENOTTY;
    if ((r = cstate(f->sock - socks, &c)) < 0) return r;
    v = c.ready;
    return copyout(arg, &v, 4);
}

/* netctl(cmd, arg): the interface, and host names */
int sys_netctl(void)
{
    int st, cs;
    uint a = arg(1);
    if (!netok) return -ENETDOWN;
    if (arg(0) == NETCTL_INFO) {
        struct netinfo ni;
        if (netreq(C_IFINFO, 0, 0, 0) != 0 || rlen < 24) return -ENETDOWN;
        memcpy(&ni, rep, 24);           /* (the payload is the struct, byte for byte) */
        return copyout(a, &ni, sizeof ni);
    }
    if (arg(0) == NETCTL_CONFIG) {
        struct netconfig nc;
        if (curproc->euid) return -EPERM;
        if (copyin(&nc, a, sizeof nc) < 0) return -EFAULT;
        req[0] = nc.dhcp ? 0 : 1;
        memcpy(req + 1, &nc.addr, 16);
        return netdo(C_IFCONFIG, 0, req, 17);
    }
    if (arg(0) == NETCTL_RESOLVE) {     /* a DNS socket for the lookup, then closed */
        struct netresolve nr;
        struct cstate c;
        uchar t = T_DNS;
        int n, r = 0;
        if (copyin(&nr, a, sizeof nr) < 0) return -EFAULT;
        if ((n = copyinstr((char *)req, (uint)nr.name, 256)) < 0) return n;
        n = strlen((char *)req);
        if ((st = netreq(C_SOCKET, 0, &t, 1)) != 0) return st < 0 ? -ENETDOWN : nerr(st);
        cs = rsock;
        if ((st = netdo(C_CONNECT, cs, req, n)) < 0) { netdo(C_CLOSE, cs, 0, 0); return st; }
        for (;;) {
            if ((r = cstate(cs, &c)) < 0) break;
            if (c.state == S_FAILED) { r = nerr(c.err); break; }
            if (c.state == S_CONNECTED) {
                if (netreq(C_RECV, cs, 0, 0) != 0 || rlen < 4) { r = -EIO; break; }
                nr.addr = g32(rep);
                r = copyout(a, &nr, sizeof nr);
                break;
            }
            if ((r = netwait()) < 0) break;
        }
        netdo(C_CLOSE, cs, 0, 0);
        return r;
    }
    return -EINVAL;
}

/* ---------------------------------------------------------------- select */
/* fd ready to read (want 0) or to write (want 1)? */
static int fdready(struct file *f, int want)
{
    if (f->type == FD_PIPE) {
        struct pipe *p = f->pipe;
        if (want == 0) return p->nread != p->nwrite || !p->writeopen;
        return p->nwrite != p->nread + PIPESIZE || !p->readopen;
    }
    if (f->type == FD_SOCKET) {
        struct cstate c;
        if (cstate(f->sock - socks, &c) < 0) return 1;  /* (an error: let the call report it) */
        if (c.state == S_FAILED || (c.state == S_PEERCLOSED && want == 0)) return 1;
        if (want == 0) return c.ready > 0;
        if (f->sock->type == SOCK_DGRAM) return 1;
        return c.room > 0;
    }
    if (f->type == FD_INODE && want == 0) return file_inready(f);
    return 1;
}

/* select(rd, wr, ex, timeout): libc drops nfds (the masks are one word) */
int sys_select(void)
{
    uint m[3], out[3], tv[2], deadline = 0;
    int k, fd, n, s, hasnet;
    for (k = 0; k < 3; k++) {
        m[k] = 0;
        if (arg(k) && copyin(&m[k], arg(k), 4) < 0) return -EFAULT;
    }
    if (arg(3)) {
        if (copyin(tv, arg(3), 8) < 0) return -EFAULT;
        if ((int)tv[0] < 0 || (int)tv[1] < 0) return -EINVAL;
        deadline = ticks + tv[0] * HZ + (tv[1] + 1000000 / HZ - 1) / (1000000 / HZ);
    }
    for (fd = 0; fd < NOFILE; fd++)
        if (((m[0] | m[1] | m[2]) >> fd & 1) && !curproc->ofile[fd]) return -EBADF;
    for (;;) {
        n = hasnet = 0;
        out[0] = out[1] = out[2] = 0;
        for (fd = 0; fd < NOFILE; fd++) {
            struct file *f = curproc->ofile[fd];
            if (!f) continue;
            if (f->type == FD_SOCKET && ((m[0] | m[1]) >> fd & 1)) hasnet = 1;
            if ((m[0] >> fd & 1) && fdready(f, 0)) { out[0] |= 1u << fd; n++; }
            if ((m[1] >> fd & 1) && fdready(f, 1)) { out[1] |= 1u << fd; n++; }
        }
        if (n || (arg(3) && (int)(ticks - deadline) >= 0)) break;
        if (curproc->killed) return -EINTR;
        s = splhi();
        if (!hasnet || !(NSTAT & ATTN)) {
            if (hasnet) NCTRL = IEN;
            nselect++;
            sleep(&selchan);
            nselect--;
        }
        splx(s);
        if (hasnet) events();
    }
    for (k = 0; k < 3; k++)
        if (arg(k) && copyout(arg(k), &out[k], 4) < 0) return -EFAULT;
    return n;
}
