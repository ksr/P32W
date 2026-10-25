/* telnetd -- remote logins over the network (RFC 854; D46).
 *
 *   telnetd [-p port]        (port 23 unless given; /etc/rc starts it, and
 *                             it carries on in the background by itself)
 *
 * Listens; each connection gets a pseudo-terminal: the first free master
 * /dev/ptypN, and on its slave /dev/ttypN a getty, as on a serial line,
 * which runs login and then the user's shell. telnetd itself stays in the
 * middle, carrying bytes both ways with select(2):
 *   - from the network: the telnet commands (IAC ...) are taken out, the
 *     rest is the terminal's typing (a CR LF or CR NUL is one Return);
 *   - to the network: what the programs wrote, with the byte 255 doubled.
 * It offers WILL ECHO and WILL SUPPRESS-GO-AHEAD, so a telnet client sends
 * each key at once and lets P32W echo (as a terminal on a serial line).
 * When the user logs out (the slave closed) or the client goes away (the
 * socket closed), the session ends: the master is closed, which hangs up
 * whatever is still on the slave, and the terminal's /etc/utmp record is
 * cleared.
 *
 * Each connection is served by a process of its own, its grandchild: the
 * child exits at once, so the session belongs to init, which reaps it.
 * With no network (no coprocessor), telnetd exits without a word. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <utmp.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>

#define IAC  255
#define DONT 254
#define DO   253
#define WONT 252
#define WILL 251
#define SB   250
#define SE   240
#define O_ECHO 1
#define O_SGA  3
#define NPTY   4

static int writeall(int fd, const unsigned char *p, int n)
{
    int k = 0, r;
    while (k < n) {
        if ((r = write(fd, p + k, n - k)) <= 0) return -1;
        k += r;
    }
    return 0;
}

static void clear_utmp(int slot)
{
    struct utmp u;
    int fd = open(UTMP, O_WRONLY);
    if (fd < 0) return;
    memset(&u, 0, sizeof u);
    lseek(fd, slot * sizeof u, 0);
    write(fd, &u, sizeof u);
    close(fd);
}

static void session(int net)
{
    unsigned char in[512], out[1100];
    static const unsigned char hello[] = { IAC, WILL, O_ECHO, IAC, WILL, O_SGA, IAC, DO, O_SGA };
    char dev[16];
    int m = -1, n, k, j, pty, state = 0, lastcr = 0;
    for (pty = 0; pty < NPTY; pty++) {
        sprintf(dev, "/dev/ptyp%d", pty);
        if ((m = open(dev, O_RDWR)) >= 0) break;
    }
    if (m < 0) {
        static const char busy[] = "P32W Unix: every terminal is in use; try again later.\r\n";
        writeall(net, (const unsigned char *)busy, sizeof busy - 1);
        return;
    }
    sprintf(dev, "ttyp%d", pty);
    if (fork() == 0) {                  /* the login: getty on the slave, as on a serial line */
        close(net); close(m);
        setsid();
        execl("/etc/getty", "getty", dev, (char *)0);
        _exit(1);
    }
    writeall(net, hello, sizeof hello);
    for (;;) {
        fd_set rd;
        FD_ZERO(&rd); FD_SET(net, &rd); FD_SET(m, &rd);
        if (select((net > m ? net : m) + 1, &rd, 0, 0, 0) < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (FD_ISSET(m, &rd)) {         /* the programs' output, to the network (IAC doubled) */
            if ((n = read(m, in, sizeof in)) <= 0) break;       /* (logged out) */
            for (j = k = 0; k < n; k++) {
                if (in[k] == IAC) out[j++] = IAC;
                out[j++] = in[k];
            }
            if (writeall(net, out, j) < 0) break;
        }
        if (FD_ISSET(net, &rd)) {       /* the client's typing, its commands taken out */
            if ((n = read(net, in, sizeof in)) <= 0) break;    /* (the client went away) */
            for (j = k = 0; k < n; k++) {
                int c = in[k];
                switch (state) {
                case 0:
                    if (c == IAC) { state = 1; break; }
                    if (lastcr && (c == 0 || c == '\n')) { lastcr = 0; break; }   /* CR NUL, CR LF: one Return */
                    lastcr = c == '\r';
                    out[j++] = c;
                    break;
                case 1:                 /* after IAC */
                    if (c == IAC) { out[j++] = IAC; state = 0; }
                    else if (c == WILL || c == WONT || c == DO || c == DONT) state = c;
                    else if (c == SB) state = 2;
                    else state = 0;
                    break;
                case 2:                 /* a subnegotiation: skipped to IAC SE */
                    if (c == IAC) state = 3;
                    break;
                case 3: state = c == SE ? 0 : 2; break;
                default:                /* the option of a WILL, WONT, DO or DONT */
                    if (state == DO && c != O_ECHO && c != O_SGA) {        /* refuse what we do not do */
                        unsigned char r[3] = { IAC, WONT, (unsigned char)c };
                        writeall(net, r, 3);
                    } else if (state == WILL && c != O_SGA) {
                        unsigned char r[3] = { IAC, DONT, (unsigned char)c };
                        writeall(net, r, 3);
                    }
                    state = 0;
                }
            }
            if (j && writeall(m, out, j) < 0) break;
        }
    }
    close(m);                           /* (hangs up the slave: SIGHUP) */
    close(net);
    clear_utmp(5 + pty);
}

int main(int argc, char **argv)
{
    struct sockaddr_in a;
    int port = 23, l, c, one = 1;
    if (argc == 3 && !strcmp(argv[1], "-p")) port = atoi(argv[2]);
    else if (argc != 1) { fprintf(stderr, "usage: telnetd [-p port]\n"); return 2; }
    if ((l = socket(AF_INET, SOCK_STREAM, 0)) < 0) return errno == ENETDOWN ? 0 : 1;   /* (no network: quietly) */
    setsockopt(l, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    if (bind(l, (struct sockaddr *)&a, sizeof a) < 0 || listen(l, 4) < 0) {
        fprintf(stderr, "telnetd: port %d: %s\n", port, strerror(errno));
        return 1;
    }
    signal(SIGHUP, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    if (fork() > 0) return 0;           /* listening: on its own from here (init's), as cron is */
    setsid();
    for (;;) {
        if ((c = accept(l, 0, 0)) < 0) {   /* (EINTR, or every socket in use for now: try again, quietly) */
            if (errno != EINTR) sleep(1);
            continue;
        }
        if (fork() == 0) {              /* the child exits at once; its child (init's now) serves */
            close(l);
            if (fork() == 0) { session(c); _exit(0); }
            _exit(0);
        }
        close(c);
        wait(0);                        /* (the child, not the session) */
    }
}
