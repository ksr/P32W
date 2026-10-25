/* telnet -- a terminal on another computer (RFC 854; D45).
 *
 *   telnet host [port]
 *
 * Connects (port 23 unless given) and joins the terminal to the
 * connection: what is typed goes there, what comes back is printed.
 * select(2) waits on both at once.
 *
 * At first the terminal is in its usual line mode: P32W echoes and edits
 * the line, and each line goes out when Return is pressed, as CR LF (the
 * network's end of line). When the server offers to echo (WILL ECHO), as a
 * login server does, telnet agrees and puts the terminal in raw mode:
 * every key goes at once and the server echoes it. A server that stops
 * echoing (WONT ECHO) gets line mode back.
 *
 * The protocol's commands start with the byte IAC (255). Of the options a
 * server may ask for, telnet does ECHO and SUPPRESS-GO-AHEAD (character at
 * a time), TERMINAL-TYPE (it says VT100) and NAWS (the window: 80 x 24),
 * and refuses the rest. ^] closes the connection (in line mode, at the
 * start of a line followed by Return). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sgtty.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define IAC  255
#define DONT 254
#define DO   253
#define WONT 252
#define WILL 251
#define SB   250
#define SE   240
#define O_ECHO  1
#define O_SGA   3
#define O_TTYPE 24
#define O_NAWS  31
#define ESCAPE  0x1D                    /* ^] */

static int net = -1, rawmode, havetty;
static struct sgttyb saved;
static unsigned char out[1024];
static int nout;

static void flushnet(void)
{
    int k = 0, r;
    while (k < nout) {
        if ((r = write(net, out + k, nout - k)) <= 0) break;
        k += r;
    }
    nout = 0;
}

static void put(int c)
{
    if (nout == sizeof out) flushnet();
    out[nout++] = c;
}

static void cmd(int verb, int opt) { put(IAC); put(verb); put(opt); }

static void setraw(int on)
{
    struct sgttyb b;
    if (!havetty || on == rawmode) return;
    b = saved;
    if (on) b.sg_flags = (b.sg_flags & ~(ECHO | CBREAK | CRMOD)) | RAW;
    ioctl(0, TIOCSETP, &b);
    rawmode = on;
}

static void finish(int status)
{
    setraw(0);
    if (havetty) ioctl(0, TIOCSETP, &saved);
    exit(status);
}

/* an option request from the server */
static void option(int verb, int opt)
{
    if (verb == WILL) {
        if (opt == O_ECHO) { cmd(DO, O_ECHO); setraw(1); }
        else if (opt == O_SGA) cmd(DO, O_SGA);
        else cmd(DONT, opt);
    } else if (verb == WONT) {
        if (opt == O_ECHO) setraw(0);
        cmd(DONT, opt);
    } else if (verb == DO) {
        if (opt == O_TTYPE || opt == O_SGA) cmd(WILL, opt);
        else if (opt == O_NAWS) {       /* the window: 80 columns, 24 lines */
            cmd(WILL, O_NAWS);
            put(IAC); put(SB); put(O_NAWS); put(0); put(80); put(0); put(24); put(IAC); put(SE);
        } else cmd(WONT, opt);
    } else if (verb == DONT) cmd(WONT, opt);
}

/* what came from the server: data to the terminal, commands answered */
static void fromnet(const unsigned char *p, int n)
{
    static int state, verb, sbopt, sbn;
    static unsigned char sb[64];
    unsigned char o[1024];
    int k, no = 0;
    for (k = 0; k < n; k++) {
        int c = p[k];
        switch (state) {
        case 0:                         /* data */
            if (c == IAC) state = 1;
            else o[no++] = c;
            break;
        case 1:                         /* after IAC */
            if (c == IAC) { o[no++] = c; state = 0; }
            else if (c == WILL || c == WONT || c == DO || c == DONT) { verb = c; state = 2; }
            else if (c == SB) { state = 3; sbn = 0; }
            else state = 0;             /* (NOP, GA, and the rest: nothing to do) */
            break;
        case 2: option(verb, c); state = 0; break;
        case 3: sbopt = c; state = 4; break;    /* a subnegotiation: its option */
        case 4:
            if (c == IAC) state = 5;
            else if (sbn < (int)sizeof sb) sb[sbn++] = c;
            break;
        case 5:
            if (c == SE) {              /* TERMINAL-TYPE SEND: say VT100 */
                if (sbopt == O_TTYPE && sbn >= 1 && sb[0] == 1) {
                    const char *t = "VT100";
                    put(IAC); put(SB); put(O_TTYPE); put(0);
                    while (*t) put(*t++);
                    put(IAC); put(SE);
                }
                state = 0;
            } else state = 4;
            break;
        }
    }
    flushnet();
    if (no) write(1, o, no);
}

int main(int argc, char **argv)
{
    struct sockaddr_in a;
    struct hostent *h;
    unsigned char buf[512];
    int port = 23, n, k, atline = 1;
    if (argc < 2 || argc > 3) { fprintf(stderr, "usage: telnet host [port]\n"); return 2; }
    if (argc == 3 && (port = atoi(argv[2])) <= 0) { fprintf(stderr, "telnet: %s: not a port\n", argv[2]); return 2; }
    if (!(h = gethostbyname(argv[1]))) { fprintf(stderr, "telnet: %s: %s\n", argv[1], hstrerror(h_errno)); return 1; }
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    memcpy(&a.sin_addr, h->h_addr, 4);
    printf("Trying %s...\n", inet_ntoa(a.sin_addr));
    fflush(stdout);
    if ((net = socket(AF_INET, SOCK_STREAM, 0)) < 0 || connect(net, (struct sockaddr *)&a, sizeof a) < 0) {
        fprintf(stderr, "telnet: %s\n", strerror(errno));
        return 1;
    }
    printf("Connected to %s.\nEscape character is '^]'.\n", argv[1]);
    fflush(stdout);
    havetty = ioctl(0, TIOCGETP, &saved) == 0;
    signal(SIGPIPE, SIG_IGN);
    for (;;) {
        fd_set rd;
        FD_ZERO(&rd); FD_SET(0, &rd); FD_SET(net, &rd);
        if (select(net + 1, &rd, 0, 0, 0) < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (FD_ISSET(net, &rd)) {
            if ((n = read(net, buf, sizeof buf)) <= 0) {
                setraw(0);
                printf("Connection closed by foreign host.\n");
                finish(0);
            }
            fromnet(buf, n);
        }
        if (FD_ISSET(0, &rd)) {
            if ((n = read(0, buf, sizeof buf)) <= 0) break;     /* (the end of input: close) */
            for (k = 0; k < n; k++) {
                int c = buf[k];
                if (c == ESCAPE && (rawmode || atline)) {
                    setraw(0);
                    printf("\nConnection closed.\n");
                    close(net);
                    finish(0);
                }
                if (c == '\n' && !rawmode) { put('\r'); put('\n'); atline = 1; continue; }
                if (c == '\r' && rawmode) { put('\r'); put('\n'); continue; }
                if (c == IAC) put(IAC);
                put(c);
                atline = 0;
            }
            flushnet();
        }
    }
    close(net);
    printf("Connection closed.\n");
    finish(0);
    return 0;
}
