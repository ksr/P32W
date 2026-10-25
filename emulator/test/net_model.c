/* net_model.c -- the I/O card's network coprocessor (the mailbox model
   in p32io.h, D45; docs/net-mailbox.md) driven through its two ports,
   stand-alone. net_peer.py runs it with -N against servers of its own on
   the Mac (TCP, UDP and TLS echo; a client for P32W's listening socket),
   whose port numbers it types on the console; and once without -N.
   Covers: HELLO, IFINFO, TCP connect / send / recv / shutdown / the end,
   a refused connection, DNS, UDP, listen / accept with ATTN and EVENTS,
   TLS (and a certificate for the wrong name), all 8 sockets in use, bad
   requests, RESET. */
#include <stdio.h>
#include <string.h>
#define NDATA (*(volatile unsigned char *)0xFFFFFF52)
#define NSTAT (*(volatile unsigned char *)0xFFFFFF53)

static unsigned char rep[1100];
static int rlen, rsock;

/* a request; the reply's status (its payload in rep, rlen bytes) */
static int req(int cmd, int sock, const void *pay, int len)
{
    const unsigned char *p = pay;
    int k, st, n;
    unsigned char h[4];
    h[0] = cmd; h[1] = sock; h[2] = len; h[3] = len >> 8;
    for (k = 0; k < 4 + len; k++) {
        while (!(NSTAT & 2)) ;
        NDATA = k < 4 ? h[k] : p[k - 4];
    }
    for (k = 0; k < 4; k++) {
        n = 0;
        while (!(NSTAT & 1)) if (++n > 1000000) return -1;
        h[k] = NDATA;
    }
    st = h[0];
    rsock = h[1];
    rlen = h[2] | h[3] << 8;
    for (k = 0; k < rlen; k++) { while (!(NSTAT & 1)) ; rep[k] = NDATA; }
    return st;
}

static void ip4(unsigned char *p, int a, int b, int c, int d) { p[0] = a; p[1] = b; p[2] = c; p[3] = d; }

/* the socket's state, waiting while it is connecting (STATE) */
static int waitstate(int s)
{
    int k;
    for (k = 0; k < 2000000; k++) {
        req(0x1B, s, 0, 0);
        if (rep[0] != 2) return rep[0];
    }
    return -1;
}

/* n bytes from a stream socket into buf (RECV until there are n, or the end) */
static int recvn(int s, char *buf, int n)
{
    int got = 0, k, st;
    unsigned char m[2];
    for (k = 0; got < n && k < 3000000; k++) {
        m[0] = n - got; m[1] = 0;
        st = req(0x16, s, m, 2);
        if (st == 1) continue;
        if (st != 0) return -st;
        if (rlen == 0) break;
        memcpy(buf + got, rep, rlen);
        got += rlen;
    }
    buf[got] = 0;
    return got;
}

static int num(void)                    /* a number typed on the console */
{
    int c, v = 0;
    while ((c = getchar()) == ' ' || c == '\n') ;
    while (c >= '0' && c <= '9') { v = v * 10 + c - '0'; c = getchar(); }
    return v;
}

int main(void)
{
    unsigned char a[300], m[2];
    char buf[256];
    int st, s, t, l, k, n, tcp, udp, tls, socks[9];
    printf("status %02x\n", NSTAT & 0xC2);
    if (!(NSTAT & 0x80)) { printf("no coprocessor\n"); return 0; }
    tcp = num(); udp = num(); tls = num();

    st = req(0x01, 0, 0, 0);
    rep[rlen] = 0;
    printf("hello %d %s\n", st, rep);
    st = req(0x02, 0, 0, 0);
    printf("ifinfo %d flags %d mac %02x:%02x:%02x:%02x:%02x:%02x ip %d.%d.%d.%d gw %d.%d.%d.%d\n", st, rep[0],
           rep[2], rep[3], rep[4], rep[5], rep[6], rep[7], rep[8], rep[9], rep[10], rep[11], rep[16], rep[17], rep[18], rep[19]);

    /* TCP to the Mac's echo server */
    a[0] = 1; req(0x10, 0, a, 1); s = rsock;
    ip4(a, 10, 0, 2, 2); a[4] = tcp >> 8; a[5] = tcp;
    st = req(0x11, s, a, 6);
    printf("tcp connect %d, state %d\n", st, waitstate(s));
    st = req(0x15, s, "hello over tcp", 14);
    printf("tcp send %d, %d taken\n", st, rep[0] | rep[1] << 8);
    n = recvn(s, buf, 14);
    printf("tcp echo %d: %s\n", n, buf);
    req(0x19, s, 0, 0);
    n = recvn(s, buf, 10);
    req(0x1B, s, 0, 0);
    printf("tcp after shutdown: %d more, state %d\n", n, rep[0]);
    st = req(0x1A, s, 0, 0);
    req(0x1B, s, 0, 0);
    printf("tcp close %d; state of a free socket: %d\n", st, req(0x1B, s, 0, 0));

    /* refused */
    a[0] = 1; req(0x10, 0, a, 1); s = rsock;
    ip4(a, 10, 0, 2, 2); a[4] = 0; a[5] = 1;
    req(0x11, s, a, 6);
    t = waitstate(s);
    printf("connect to port 1: state %d error %d\n", t, rep[1]);
    req(0x1A, s, 0, 0);

    /* DNS */
    a[0] = 4; req(0x10, 0, a, 1); s = rsock;
    req(0x11, s, "localhost", 9);
    t = waitstate(s);
    st = req(0x16, s, 0, 0);
    printf("dns localhost: state %d, %d.%d.%d.%d\n", t, rep[0], rep[1], rep[2], rep[3]);
    req(0x1A, s, 0, 0);
    a[0] = 4; req(0x10, 0, a, 1); s = rsock;
    req(0x11, s, "no.such.host.invalid", 20);
    t = waitstate(s);
    printf("dns no.such.host.invalid: state %d error %d\n", t, rep[1]);
    req(0x1A, s, 0, 0);

    /* UDP */
    a[0] = 2; req(0x10, 0, a, 1); s = rsock;
    ip4(a, 10, 0, 2, 2); a[4] = udp >> 8; a[5] = udp;
    memcpy(a + 6, "ping by udp", 11);
    st = req(0x17, s, a, 17);
    printf("udp sendto %d, %d sent\n", st, rep[0] | rep[1] << 8);
    for (k = 0; k < 3000000; k++) { m[0] = 200; m[1] = 0; if ((st = req(0x18, s, m, 2)) != 1) break; }
    rep[rlen] = 0;
    printf("udp recvfrom %d: from %d.%d.%d.%d, %s\n", st, rep[0], rep[1], rep[2], rep[3], rep + 6);
    req(0x1A, s, 0, 0);

    /* listening, for the Mac's client */
    a[0] = 1; req(0x10, 0, a, 1); l = rsock;
    a[0] = 0; a[1] = 77;
    st = req(0x12, l, a, 2);
    a[0] = 1;
    printf("bind %d, listen %d\n", st, req(0x13, l, a, 1));
    req(0x04, 0, 0, 0);                 /* (events so far: cleared) */
    for (k = 0; k < 20000000 && !(NSTAT & 4); k++) ;
    st = req(0x04, 0, 0, 0);
    printf("attn %s, events %d: the listener %s\n", k < 20000000 ? "set" : "never", st,
           (rep[0] | rep[1] << 8) & (1 << l) ? "changed" : "unchanged");
    for (k = 0; k < 3000000 && (st = req(0x14, l, 0, 0)) == 1; k++) ;
    s = rsock;
    printf("accept %d: a new socket %s, peer %d.%d.%d.%d\n", st, s != l ? "apart" : "SAME", rep[0], rep[1], rep[2], rep[3]);
    n = recvn(s, buf, 16);
    printf("from the mac: %s", buf);
    st = req(0x15, s, "hello mac\n", 10);
    printf("reply sent %d\n", st);
    req(0x1A, s, 0, 0);
    req(0x1A, l, 0, 0);

    /* TLS */
    a[0] = 3; req(0x10, 0, a, 1); s = rsock;
    ip4(a, 10, 0, 2, 2); a[4] = tls >> 8; a[5] = tls;
    memcpy(a + 6, "localhost", 9);
    req(0x11, s, a, 15);
    t = waitstate(s);
    printf("tls connect: state %d\n", t);
    req(0x15, s, "secret words", 12);
    n = recvn(s, buf, 12);
    printf("tls echo %d: %s\n", n, buf);
    req(0x1A, s, 0, 0);
    a[0] = 3; req(0x10, 0, a, 1); s = rsock;
    ip4(a, 10, 0, 2, 2); a[4] = tls >> 8; a[5] = tls;
    memcpy(a + 6, "wrong.name", 10);
    req(0x11, s, a, 16);
    t = waitstate(s);
    printf("tls to the wrong name: state %d error %d\n", t, rep[1]);
    req(0x1A, s, 0, 0);

    /* every socket in use */
    for (k = 0; k < 9; k++) { a[0] = 1; st = req(0x10, 0, a, 1); socks[k] = st == 0 ? rsock : -1; }
    printf("sockets: the eighth %s, the ninth %d\n", socks[7] >= 0 ? "made" : "refused", st);
    for (k = 0; k < 8; k++) req(0x1A, socks[k], 0, 0);

    /* bad requests */
    printf("unknown command %d\n", req(0x7F, 0, 0, 0));
    printf("state of socket 9: %d\n", req(0x1B, 9, 0, 0));
    for (k = 0; k < 4; k++) { while (!(NSTAT & 2)) ; NDATA = k == 0 ? 0x15 : k == 2 ? 0xD0 : k == 3 ? 0x07 : 0; }
    for (k = 0; k < 2000; k++) { while (!(NSTAT & 2)) ; NDATA = 'x'; }
    st = NDATA; NDATA; NDATA; NDATA;
    printf("a 2000-byte request: %d\n", st);
    NSTAT = 0x80;
    printf("after reset: rxf %d; hello %d\n", NSTAT & 1, req(0x01, 0, 0, 0));
    return 0;
}
