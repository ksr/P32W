/* nettest.c -- P32W Unix's sockets (D45) through the BSD calls, against
 * net_test.py's servers on the Mac (10.0.2.2): TCP, UDP and TLS echo, and
 * a client that connects to the port given last.
 *
 *   nettest TCPPORT UDPPORT TLSPORT LISTENPORT
 *
 * Prints what happened, without the port numbers (they change). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sgtty.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/sockio.h>
#include <sys/netctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

static struct sockaddr_in mac(int port)
{
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = inet_addr("10.0.2.2");
    return a;
}

static void err(const char *what) { printf("%s: %s\n", what, strerror(errno)); }

static int readn(int fd, char *buf, int n)  /* n bytes, or up to the end */
{
    int k = 0, r;
    while (k < n && (r = read(fd, buf + k, n - k)) > 0) k += r;
    buf[k] = 0;
    return k;
}

static void onalarm(int sig) { (void)sig; }

int main(int argc, char **argv)
{
    struct netinfo ni;
    struct hostent *h;
    struct sockaddr_in a, b;
    struct stat st;
    struct timeval tv;
    socklen_t len;
    fd_set rd, wr;
    char buf[256];
    int tcp, udp, tls, lport, s, l, c, k, n, v, socks[9];
    if (argc != 5) { fprintf(stderr, "usage: nettest TCPPORT UDPPORT TLSPORT LISTENPORT\n"); return 2; }
    tcp = atoi(argv[1]); udp = atoi(argv[2]); tls = atoi(argv[3]); lport = atoi(argv[4]);

    if (netctl(NETCTL_INFO, &ni) < 0) { err("netctl"); return 1; }
    a.sin_addr.s_addr = ni.addr;
    printf("interface: %s, link %s, mac %02x:%02x:%02x:%02x:%02x:%02x\n", inet_ntoa(a.sin_addr),
           ni.flags & NETF_UP ? "up" : "down", ni.mac[0], ni.mac[1], ni.mac[2], ni.mac[3], ni.mac[4], ni.mac[5]);
    h = gethostbyname("localhost");
    printf("localhost is %s\n", h ? inet_ntoa(*(struct in_addr *)h->h_addr) : hstrerror(h_errno));
    h = gethostbyname("no.such.host.invalid");
    printf("no.such.host.invalid: %s\n", h ? "found?" : hstrerror(h_errno));

    /* TCP: connect, write, read the echo, shut down our side, the end */
    s = socket(AF_INET, SOCK_STREAM, 0);
    a = mac(tcp);
    if (connect(s, (struct sockaddr *)&a, sizeof a) < 0) err("tcp connect");
    fstat(s, &st);
    printf("tcp: connected; fstat says %s\n", S_ISSOCK(st.st_mode) ? "a socket" : "not a socket");
    len = sizeof b;
    getpeername(s, (struct sockaddr *)&b, &len);
    printf("tcp: the peer is %s, %s port\n", inet_ntoa(b.sin_addr), ntohs(b.sin_port) == tcp ? "the right" : "a wrong");
    write(s, "hello over tcp\n", 15);
    n = readn(s, buf, 15);
    printf("tcp: echo of %d bytes: %s", n, buf);
    for (k = 0; k < 200; k++) buf[k] = 'a' + k % 26;
    for (k = 0; k < 20; k++) write(s, buf, 200);        /* 4000 bytes: more than one message */
    for (v = 0; v < 4000 && (k = read(s, buf, sizeof buf - 1)) > 0; v += k) ;
    printf("tcp: 4000 bytes there and back: %s\n", v == 4000 ? "all" : "not all");
    shutdown(s, SHUT_WR);
    printf("tcp: after shutdown, read gives %d (the end)\n", (int)read(s, buf, 10));
    close(s);

    /* a refused connection */
    s = socket(AF_INET, SOCK_STREAM, 0);
    a = mac(1);
    if (connect(s, (struct sockaddr *)&a, sizeof a) < 0) err("connect to port 1");
    close(s);

    /* without waiting: connect, select for writing, SO_ERROR, an empty read */
    s = socket(AF_INET, SOCK_STREAM, 0);
    fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
    a = mac(tcp);
    if (connect(s, (struct sockaddr *)&a, sizeof a) < 0 && errno != EINPROGRESS) err("tcp connect, non-blocking");
    else printf("tcp connect, non-blocking: started (or, on the loopback, already done)\n");
    FD_ZERO(&wr); FD_SET(s, &wr);
    tv.tv_sec = 5; tv.tv_usec = 0;
    n = select(s + 1, 0, &wr, 0, &tv);
    len = 4;
    getsockopt(s, SOL_SOCKET, SO_ERROR, &v, &len);
    printf("select: %d ready to write; SO_ERROR %d\n", n, v);
    if (read(s, buf, 10) < 0) err("read with nothing there");
    write(s, "x", 1);
    FD_ZERO(&rd); FD_SET(s, &rd);
    tv.tv_sec = 5;
    n = select(s + 1, &rd, 0, 0, &tv);
    ioctl(s, FIONREAD, &v);
    printf("select: %d ready to read, FIONREAD %d\n", n, v);
    close(s);

    /* a blocking read, interrupted by a signal */
    s = socket(AF_INET, SOCK_STREAM, 0);
    a = mac(tcp);
    connect(s, (struct sockaddr *)&a, sizeof a);
    signal(SIGALRM, onalarm);
    alarm(1);
    if (read(s, buf, 10) < 0) err("read, with an alarm after 1 s");
    close(s);

    /* UDP: nothing yet (select times out), then there and back */
    s = socket(AF_INET, SOCK_DGRAM, 0);
    FD_ZERO(&rd); FD_SET(s, &rd);
    tv.tv_sec = 0; tv.tv_usec = 300000;
    printf("udp: select with nothing sent: %d\n", select(s + 1, &rd, 0, 0, &tv));
    a = mac(udp);
    n = sendto(s, "ping by udp", 11, 0, (struct sockaddr *)&a, sizeof a);
    len = sizeof b;
    k = recvfrom(s, buf, sizeof buf - 1, 0, (struct sockaddr *)&b, &len);
    buf[k > 0 ? k : 0] = 0;
    printf("udp: sent %d, back %d from %s: %s\n", n, k, inet_ntoa(b.sin_addr), buf);
    close(s);

    /* a server: the Mac's client connects to LISTENPORT */
    l = socket(AF_INET, SOCK_STREAM, 0);
    a = mac(80);
    a.sin_addr.s_addr = INADDR_ANY;
    if (bind(l, (struct sockaddr *)&a, sizeof a) < 0) err("bind to port 80 (not root)");
    close(l);
    l = socket(AF_INET, SOCK_STREAM, 0);
    a.sin_port = htons(lport);
    if (bind(l, (struct sockaddr *)&a, sizeof a) < 0 || listen(l, 4) < 0) err("bind, listen");
    len = sizeof b;
    getsockname(l, (struct sockaddr *)&b, &len);
    printf("server: listening on %s, %s port\n", inet_ntoa(b.sin_addr), ntohs(b.sin_port) == lport ? "the right" : "a wrong");
    FD_ZERO(&rd); FD_SET(l, &rd);
    tv.tv_sec = 30; tv.tv_usec = 0;
    n = select(l + 1, &rd, 0, 0, &tv);
    len = sizeof b;
    c = accept(l, (struct sockaddr *)&b, &len);
    printf("server: select %d; accepted a client from %s\n", n, c >= 0 ? inet_ntoa(b.sin_addr) : strerror(errno));
    k = 0;
    while (k < (int)sizeof buf - 1 && read(c, buf + k, 1) == 1 && buf[k] != '\n') k++;
    buf[k] = 0;
    printf("server: the client says \"%s\"\n", buf);
    write(c, "hello mac\n", 10);
    close(c);
    close(l);

    /* TLS */
    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TLS);
    setsockopt(s, IPPROTO_TLS, TLS_HOSTNAME, "localhost", 9);
    a = mac(tls);
    if (connect(s, (struct sockaddr *)&a, sizeof a) < 0) err("tls connect");
    write(s, "secret words\n", 13);
    n = readn(s, buf, 13);
    printf("tls: echo of %d bytes: %s", n, buf);
    close(s);
    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TLS);
    setsockopt(s, IPPROTO_TLS, TLS_HOSTNAME, "wrong.name", 10);
    if (connect(s, (struct sockaddr *)&a, sizeof a) < 0) err("tls connect, a certificate for another name");
    close(s);

    /* every socket in use; bad requests */
    for (k = 0; k < 9; k++) socks[k] = socket(AF_INET, SOCK_STREAM, 0);
    k = errno;
    printf("sockets: the eighth %s; ", socks[7] >= 0 ? "made" : "refused");
    errno = k;
    if (socks[8] < 0) err("the ninth"); else printf("the ninth made?\n");
    for (k = 0; k < 8; k++) close(socks[k]);
    if (socket(AF_INET + 1, SOCK_STREAM, 0) < 0) err("socket, another family");
    if (socket(AF_INET, SOCK_DGRAM, IPPROTO_TLS) < 0) err("socket, TLS over UDP");
    if (connect(0, (struct sockaddr *)&a, sizeof a) < 0) err("connect on standard input");
    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (send(s, "x", 1, 0) < 0) err("udp send with no address");
    close(s);
    printf("done\n");
    return 0;
}
