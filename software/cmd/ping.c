/* ping -- is a host there? ICMP echo requests, a second apart, and the
 * time each answer took (D45).
 *
 *   ping [-c count] [-s size] host
 *
 * P32W builds the ICMP messages itself (type 8, code 0, the checksum, its
 * process id and a sequence number, then size bytes of data, 56 unless
 * -s) and sends them through an ICMP socket (socket(AF_INET, SOCK_DGRAM,
 * IPPROTO_ICMP)): the coprocessor puts them in IP packets. It stops after
 * count answers' worth of requests, or at ^C, then prints the summary.
 * Times are in clock ticks of 20 ms, so they read 0, 20, 40 ms, ... */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/times.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

static int sent, got, interrupted;
static long tmin = -1, tmax, tsum;

static unsigned short cksum(const unsigned char *p, int n)   /* the Internet checksum (RFC 1071) */
{
    unsigned s = 0;
    int k;
    for (k = 0; k + 1 < n; k += 2) s += p[k] << 8 | p[k + 1];
    if (n & 1) s += p[n - 1] << 8;
    while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
    return (unsigned short)~s;
}

static void onintr(int sig) { (void)sig; interrupted = 1; }

static long now(void) { return times(0); }

int main(int argc, char **argv)
{
    struct sockaddr_in to, from;
    struct hostent *h;
    unsigned char pkt[1100], in[1100];
    int count = -1, size = 56, fd, k, n, i, id = getpid() & 0xFFFF;
    char *host;
    for (i = 1; i + 1 < argc && argv[i][0] == '-'; i += 2) {
        if (!strcmp(argv[i], "-c")) count = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "-s")) size = atoi(argv[i + 1]);
        else break;
    }
    if (i != argc - 1 || size < 0 || size > 1000) { fprintf(stderr, "usage: ping [-c count] [-s size] host\n"); return 2; }
    host = argv[i];
    if (!(h = gethostbyname(host))) { fprintf(stderr, "ping: %s: %s\n", host, hstrerror(h_errno)); return 2; }
    memset(&to, 0, sizeof to);
    to.sin_family = AF_INET;
    memcpy(&to.sin_addr, h->h_addr, 4);
    if ((fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP)) < 0) { fprintf(stderr, "ping: %s\n", strerror(errno)); return 2; }
    signal(SIGINT, onintr);
    printf("PING %s (%s): %d data bytes\n", host, inet_ntoa(to.sin_addr), size);
    for (k = 0; (count < 0 || k < count) && !interrupted; k++) {
        long t0 = now(), due = t0 + CLK_TCK;
        pkt[0] = 8; pkt[1] = 0; pkt[2] = pkt[3] = 0;
        pkt[4] = id >> 8; pkt[5] = id; pkt[6] = k >> 8; pkt[7] = k;
        for (n = 0; n < size; n++) pkt[8 + n] = (unsigned char)(n + 8);
        n = cksum(pkt, 8 + size);
        pkt[2] = n >> 8; pkt[3] = n;
        if (sendto(fd, pkt, 8 + size, 0, (struct sockaddr *)&to, sizeof to) < 0) {
            fprintf(stderr, "ping: sendto: %s\n", strerror(errno));
            if (errno == ENETDOWN) break;
        } else sent++;
        while (!interrupted) {          /* answers, until the next request is due */
            struct timeval tv;
            fd_set rd;
            long left = due - now();
            socklen_t len = sizeof from;
            if (left <= 0) break;
            FD_ZERO(&rd); FD_SET(fd, &rd);
            tv.tv_sec = left / CLK_TCK; tv.tv_usec = left % CLK_TCK * (1000000 / CLK_TCK);
            if (select(fd + 1, &rd, 0, 0, &tv) <= 0) continue;
            if ((n = recvfrom(fd, in, sizeof in, 0, (struct sockaddr *)&from, &len)) < 8) continue;
            if (in[0] != 0 || (in[4] << 8 | in[5]) != id) continue;    /* (an echo reply, ours) */
            {
                int seq = in[6] << 8 | in[7];
                long t = (now() - t0) * (1000 / CLK_TCK);
                if (seq != k) t = -1;   /* (a late one: its time is unknown) */
                got++;
                if (t >= 0) {
                    if (tmin < 0 || t < tmin) tmin = t;
                    if (t > tmax) tmax = t;
                    tsum += t;
                }
                printf("%d bytes from %s: icmp_seq=%d", n, inet_ntoa(from.sin_addr), seq);
                if (t >= 0) printf(" time=%ld ms", t);
                printf("\n");
            }
        }
    }
    printf("--- %s ping statistics ---\n", host);
    printf("%d packets transmitted, %d received, %d%% packet loss\n", sent, got, sent ? (sent - got) * 100 / sent : 0);
    if (got && tmin >= 0) printf("round-trip min/avg/max = %ld/%ld/%ld ms\n", tmin, tsum / got, tmax);
    return got ? 0 : 1;
}
