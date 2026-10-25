/* ftp -- move files to and from another computer by FTP (RFC 959; D46).
 *
 *   ftp [-n] [host [port]]
 *
 * Commands, one a line (ftp> is the prompt):
 *   open host [port]   connect (and log in: name and password asked, unless -n)
 *   user name [pass]   log in; anonymous takes any password
 *   ls [path]          the server's directory, long (LIST); nlist: names only (NLST)
 *   cd path, cdup, pwd
 *   get remote [local] put local [remote]   delete path   mkdir path   rmdir path
 *   binary / ascii     the kind of transfer: bytes as they are (the default),
 *                      or text, CR LF on the network and LF on P32W
 *   status, close, quit (bye), help (?)
 *
 * Every transfer uses passive mode: the server names a port, and ftp
 * connects to it -- at the address of the control connection, not the one
 * the server names, which behind a router (NAT) is often private and
 * unreachable (curl does the same). ftp prints the server's replies (the
 * numbered lines), and after a transfer the bytes and the time it took.
 * Plain FTP is not encrypted: names and passwords cross the network as they
 * are. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <sgtty.h>
#include <sys/times.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

static int ctl = -1, ascii, noauto, status;
static struct sockaddr_in peer;         /* the server, as the control connection reaches it */
static char host[256];
static unsigned char cb[1024];
static int clen, cpos;

static int writeall(int fd, const void *p, int n)
{
    const unsigned char *s = p;
    int k = 0, r;
    while (k < n) {
        if ((r = write(fd, s + k, n - k)) <= 0) return -1;
        k += r;
    }
    return 0;
}

static int cgetc(void)
{
    if (cpos == clen) {
        if ((clen = read(ctl, cb, sizeof cb)) <= 0) { clen = cpos = 0; return -1; }
        cpos = 0;
    }
    return cb[cpos++];
}

static int cline(char *buf, int max)    /* a reply line, without CR LF; -1 at the end */
{
    int n = 0, c;
    while ((c = cgetc()) >= 0 && c != '\n')
        if (c != '\r' && n < max - 1) buf[n++] = c;
    buf[n] = 0;
    return c < 0 && n == 0 ? -1 : n;
}

/* the server's reply, printed (a multi-line one too): its code, or -1 */
static int reply(char *last)
{
    char line[512], code[4];
    if (cline(line, sizeof line) < 0) {
        printf("ftp: the server closed the connection\n");
        close(ctl); ctl = -1;
        return -1;
    }
    printf("%s\n", line);
    if (strlen(line) >= 4 && line[3] == '-') {      /* "123-...": more lines up to "123 ..." */
        memcpy(code, line, 3); code[3] = 0;
        while (cline(line, sizeof line) >= 0) {
            printf("%s\n", line);
            if (!strncmp(line, code, 3) && line[3] == ' ') break;
        }
    }
    if (last) strcpy(last, line);
    return atoi(line);
}

static int cmd(char *last, const char *fmt, const char *arg)
{
    char buf[600];
    if (ctl < 0) { printf("Not connected.\n"); return -1; }
    snprintf(buf, sizeof buf, fmt, arg ? arg : "");
    strcat(buf, "\r\n");
    if (writeall(ctl, buf, strlen(buf)) < 0) { printf("ftp: %s\n", strerror(errno)); return -1; }
    return reply(last);
}

static void readpass(const char *prompt, char *buf, int n)   /* without echo */
{
    struct sgttyb b;
    int saved = 0, have = gtty(0, &b) == 0;
    printf("%s", prompt); fflush(stdout);
    if (have) { saved = b.sg_flags; b.sg_flags &= ~ECHO; stty(0, &b); }
    if (!fgets(buf, n, stdin)) buf[0] = 0;
    buf[strcspn(buf, "\n")] = 0;
    if (have) { b.sg_flags = saved; stty(0, &b); printf("\n"); }
}

static void login(const char *name, const char *pass)
{
    char p[128];
    int r = cmd(0, "USER %s", name);
    if (r == 331) {
        if (pass) r = cmd(0, "PASS %s", pass);
        else { readpass("Password:", p, sizeof p); r = cmd(0, "PASS %s", p); }
    }
    if (r == 230) cmd(0, ascii ? "TYPE A" : "TYPE I", 0);
}

static void doopen(const char *h, int port)
{
    struct hostent *he;
    char name[64];
    if (ctl >= 0) { printf("Already connected to %s; close first.\n", host); return; }
    if (!(he = gethostbyname(h))) { printf("ftp: %s: %s\n", h, hstrerror(h_errno)); return; }
    memset(&peer, 0, sizeof peer);
    peer.sin_family = AF_INET;
    peer.sin_port = htons(port);
    memcpy(&peer.sin_addr, he->h_addr, 4);
    if ((ctl = socket(AF_INET, SOCK_STREAM, 0)) < 0 || connect(ctl, (struct sockaddr *)&peer, sizeof peer) < 0) {
        printf("ftp: %s: %s\n", h, strerror(errno));
        if (ctl >= 0) close(ctl);
        ctl = -1;
        return;
    }
    clen = cpos = 0;
    strncpy(host, h, sizeof host - 1);
    printf("Connected to %s.\n", h);
    if (reply(0) != 220) return;
    if (noauto) return;
    printf("Name (%s): ", h); fflush(stdout);
    if (!fgets(name, sizeof name, stdin)) return;
    name[strcspn(name, "\n")] = 0;
    login(name, 0);
}

/* PASV, then a connection to the port it names (at the server's address) */
static int dataconn(void)
{
    char last[512], *p;
    unsigned v[6];
    struct sockaddr_in a;
    int d;
    if (cmd(last, "PASV", 0) != 227) return -1;
    if (!(p = strchr(last, '(')) || sscanf(p + 1, "%u,%u,%u,%u,%u,%u", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) {
        printf("ftp: a PASV reply I cannot read\n");
        return -1;
    }
    a = peer;                           /* (the control connection's address: see above) */
    a.sin_port = htons(v[4] << 8 | v[5]);
    if ((d = socket(AF_INET, SOCK_STREAM, 0)) < 0 || connect(d, (struct sockaddr *)&a, sizeof a) < 0) {
        printf("ftp: the data connection: %s\n", strerror(errno));
        if (d >= 0) close(d);
        return -1;
    }
    return d;
}

static void report(long bytes, long t0)
{
    long ticks = times(0) - t0;
    if (ticks < 1) ticks = 1;
    printf("%ld bytes in %ld.%02ld s (%ld bytes/s)\n", bytes, ticks / CLK_TCK, ticks % CLK_TCK * (100 / CLK_TCK),
           bytes * CLK_TCK / ticks);
}

/* a listing or a file from the server to fd (text: CR LF -> LF) */
static void transfer_in(const char *verb, const char *arg, int fd, int text, int quiet)
{
    unsigned char buf[1024], o[1024];
    char line[600];
    long bytes = 0, t0 = times(0);
    int d, n, k, j, r;
    if ((d = dataconn()) < 0) return;
    snprintf(line, sizeof line, "%s%s%s", verb, arg ? " " : "", arg ? arg : "");
    r = cmd(0, "%s", line);
    if (r != 125 && r != 150) { close(d); return; }
    while ((n = read(d, buf, sizeof buf)) > 0) {
        for (j = k = 0; k < n; k++)
            if (!text || buf[k] != '\r') o[j++] = buf[k];
        if (writeall(fd, o, j) < 0) { printf("ftp: write: %s\n", strerror(errno)); break; }
        bytes += n;
    }
    close(d);
    if (reply(0) == 226 && !quiet) report(bytes, t0);
}

static void put(const char *local, const char *remote)
{
    unsigned char buf[512], o[1024];
    char line[600];
    long bytes = 0, t0 = times(0);
    int fd, d, n, k, j, r;
    if ((fd = open(local, O_RDONLY)) < 0) { printf("ftp: %s: %s\n", local, strerror(errno)); return; }
    if ((d = dataconn()) < 0) { close(fd); return; }
    snprintf(line, sizeof line, "STOR %s", remote);
    r = cmd(0, "%s", line);
    if (r != 125 && r != 150) { close(d); close(fd); return; }
    while ((n = read(fd, buf, sizeof buf)) > 0) {
        for (j = k = 0; k < n; k++) {
            if (ascii && buf[k] == '\n') o[j++] = '\r';
            o[j++] = buf[k];
        }
        if (writeall(d, o, j) < 0) { printf("ftp: the data connection: %s\n", strerror(errno)); break; }
        bytes += n;
    }
    close(fd);
    close(d);
    if (reply(0) == 226) report(bytes, t0);
}

static const char *base(const char *p)
{
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static void help(void)
{
    printf("open host [port]  user name [pass]  ls [path]  nlist [path]  cd path  cdup  pwd\n"
           "get remote [local]  put local [remote]  delete path  mkdir path  rmdir path\n"
           "binary  ascii  status  close  quit\n");
}

int main(int argc, char **argv)
{
    char line[600], *av[4];
    int i = 1, n;
    if (i < argc && !strcmp(argv[i], "-n")) { noauto = 1; i++; }
    if (i < argc) doopen(argv[i], i + 1 < argc ? atoi(argv[i + 1]) : 21);
    for (;;) {
        printf("ftp> "); fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) break;
        line[strcspn(line, "\n")] = 0;
        for (n = 0, av[0] = strtok(line, " \t"); av[n] && n < 3; av[++n] = strtok(0, " \t")) ;
        if (!n) continue;
        av[n] = 0;
        if (!strcmp(av[0], "quit") || !strcmp(av[0], "bye")) break;
        else if (!strcmp(av[0], "open")) { if (n < 2) printf("usage: open host [port]\n"); else doopen(av[1], n > 2 ? atoi(av[2]) : 21); }
        else if (!strcmp(av[0], "user")) { if (n < 2) printf("usage: user name [pass]\n"); else if (ctl < 0) printf("Not connected.\n"); else login(av[1], av[2]); }
        else if (!strcmp(av[0], "ls") || !strcmp(av[0], "dir")) transfer_in("LIST", av[1], 1, 1, 1);
        else if (!strcmp(av[0], "nlist")) transfer_in("NLST", av[1], 1, 1, 1);
        else if (!strcmp(av[0], "cd")) { if (n < 2) printf("usage: cd path\n"); else cmd(0, "CWD %s", av[1]); }
        else if (!strcmp(av[0], "cdup")) cmd(0, "CDUP", 0);
        else if (!strcmp(av[0], "pwd")) cmd(0, "PWD", 0);
        else if (!strcmp(av[0], "delete")) { if (n < 2) printf("usage: delete path\n"); else cmd(0, "DELE %s", av[1]); }
        else if (!strcmp(av[0], "mkdir")) { if (n < 2) printf("usage: mkdir path\n"); else cmd(0, "MKD %s", av[1]); }
        else if (!strcmp(av[0], "rmdir")) { if (n < 2) printf("usage: rmdir path\n"); else cmd(0, "RMD %s", av[1]); }
        else if (!strcmp(av[0], "binary")) { ascii = 0; cmd(0, "TYPE I", 0); }
        else if (!strcmp(av[0], "ascii")) { ascii = 1; cmd(0, "TYPE A", 0); }
        else if (!strcmp(av[0], "get")) {
            int fd;
            const char *local = n > 2 ? av[2] : base(av[1] ? av[1] : "");
            if (n < 2) { printf("usage: get remote [local]\n"); continue; }
            if (ctl < 0) { printf("Not connected.\n"); continue; }
            if ((fd = open(local, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0) { printf("ftp: %s: %s\n", local, strerror(errno)); continue; }
            transfer_in("RETR", av[1], fd, ascii, 0);
            close(fd);
        } else if (!strcmp(av[0], "put")) {
            if (n < 2) { printf("usage: put local [remote]\n"); continue; }
            if (ctl < 0) { printf("Not connected.\n"); continue; }
            put(av[1], n > 2 ? av[2] : base(av[1]));
        } else if (!strcmp(av[0], "status")) {
            if (ctl >= 0) printf("Connected to %s.\n", host); else printf("Not connected.\n");
            printf("Type: %s.\n", ascii ? "ascii (text)" : "binary");
        } else if (!strcmp(av[0], "close")) {
            if (ctl >= 0) { cmd(0, "QUIT", 0); close(ctl); ctl = -1; }
        } else if (!strcmp(av[0], "help") || !strcmp(av[0], "?")) help();
        else printf("?Invalid command: %s\n", av[0]);
    }
    if (ctl >= 0) { cmd(0, "QUIT", 0); close(ctl); }
    return status;
}
