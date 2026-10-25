/* ntpdate -- set the clock from a time server (SNTP, RFC 4330; D46).
 *
 *   ntpdate [-q] [server[:port]]
 *
 * Asks the server (port 123; the first "server NAME" line of /etc/ntp.conf
 * unless given, else pool.ntp.org) for the time: one 48-byte UDP request,
 * up to three tries two seconds apart. The answer's transmit time, in
 * seconds since 1900, less 2,208,988,800 is Unix time; half the round trip
 * is added. As root, ntpdate sets the clock (stime(2): the kernel's, and
 * the battery-backed clock's) and says by how much it moved; -q only asks.
 * /etc/rc runs ntpdate at boot if /etc/ntp.conf exists. The clock keeps
 * whole seconds, and so does ntpdate. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/times.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define NTP_UNIX 2208988800u            /* seconds from 1900 to 1970 */

static void conf_server(char *buf, int n)
{
    FILE *f = fopen("/etc/ntp.conf", "r");
    char line[256], name[200];
    strcpy(buf, "pool.ntp.org");
    if (!f) return;
    while (fgets(line, sizeof line, f))
        if (sscanf(line, " server %199s", name) == 1) { strncpy(buf, name, n - 1); buf[n - 1] = 0; break; }
    fclose(f);
}

int main(int argc, char **argv)
{
    char server[256], *colon;
    unsigned char pkt[48];
    struct sockaddr_in a, from;
    struct hostent *h;
    int fd, k, port = 123, query = 0, i = 1, n;
    long t0, rtt = 0;
    time_t now, t;
    if (i < argc && !strcmp(argv[i], "-q")) { query = 1; i++; }
    if (i < argc - 1) { fprintf(stderr, "usage: ntpdate [-q] [server[:port]]\n"); return 2; }
    if (i < argc) { strncpy(server, argv[i], sizeof server - 1); server[sizeof server - 1] = 0; }
    else conf_server(server, sizeof server);
    if ((colon = strchr(server, ':'))) { *colon = 0; port = atoi(colon + 1); }
    if (!(h = gethostbyname(server))) { fprintf(stderr, "ntpdate: %s: %s\n", server, hstrerror(h_errno)); return 1; }
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    memcpy(&a.sin_addr, h->h_addr, 4);
    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { fprintf(stderr, "ntpdate: %s\n", strerror(errno)); return 1; }
    for (k = 0; k < 3; k++) {
        struct timeval tv;
        fd_set rd;
        socklen_t len = sizeof from;
        memset(pkt, 0, sizeof pkt);
        pkt[0] = 0x23;                  /* no leap warning, version 4, mode 3: a client */
        t0 = times(0);
        if (sendto(fd, pkt, sizeof pkt, 0, (struct sockaddr *)&a, sizeof a) < 0) {
            fprintf(stderr, "ntpdate: %s\n", strerror(errno)); return 1;
        }
        FD_ZERO(&rd); FD_SET(fd, &rd);
        tv.tv_sec = 2; tv.tv_usec = 0;
        if (select(fd + 1, &rd, 0, 0, &tv) <= 0) continue;
        n = recvfrom(fd, pkt, sizeof pkt, 0, (struct sockaddr *)&from, &len);
        if (n >= 48 && (pkt[0] & 7) == 4 && pkt[1] != 0) { rtt = times(0) - t0; break; }   /* mode 4, a server; stratum 0 is a refusal */
    }
    close(fd);
    if (k == 3) { fprintf(stderr, "ntpdate: %s: no answer\n", server); return 1; }
    t = (time_t)(((unsigned)pkt[40] << 24 | pkt[41] << 16 | pkt[42] << 8 | pkt[43]) - NTP_UNIX);
    if (pkt[44] & 0x80) t++;            /* (the fraction rounds) */
    t += (rtt / 2 + CLK_TCK / 2) / CLK_TCK;
    now = time(0);
    if (query) {
        printf("%s: offset %+d s: %s", server, (int)(t - now), ctime(&t));
        return 0;
    }
    if (t != now && stime(&t) < 0) { fprintf(stderr, "ntpdate: setting the clock: %s\n", strerror(errno)); return 1; }
    printf("ntpdate: %s: the clock %s %+d s: %s", server, t == now ? "was right," : "moved", (int)(t - now), ctime(&t));
    return 0;
}
