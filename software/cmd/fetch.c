/* fetch -- get a web page or file by HTTP or HTTPS (D45).
 *
 *   fetch [-qv] [-o file | -O] url
 *
 * url is http://host[:port]/path or https://... . The page goes to the
 * standard output, or to file with -o, or with -O to a file named after
 * the last part of the path (index.html for a directory). -v prints the
 * request's reply line and headers on the standard error; -q says nothing
 * but errors. Redirections (301, 302, 303, 307, 308) are followed, five
 * at most.
 *
 * HTTPS is a socket of protocol IPPROTO_TLS: the network coprocessor does
 * the TLS and checks the server's certificate against its name, so fetch
 * itself only speaks HTTP. The request is HTTP/1.1 with "Connection:
 * close"; a reply's body is read to its Content-Length, in chunks if it is
 * "chunked", or to the end of the connection.
 *
 * The exit status is 0 for a 2xx reply, 1 for an HTTP error or a failure
 * on the way, 2 for a bad command line. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

static int verbose, quiet;
static int fd = -1;
static unsigned char ib[1024];
static int ilen, ipos;

static int getbyte(void)                /* the next byte from the server, -1 at the end */
{
    if (ipos == ilen) {
        ilen = read(fd, ib, sizeof ib);
        ipos = 0;
        if (ilen <= 0) { ilen = 0; return -1; }
    }
    return ib[ipos++];
}

static int getline_(char *buf, int max)   /* a line, without its CR LF; -1 at the end */
{
    int n = 0, c;
    while ((c = getbyte()) >= 0 && c != '\n')
        if (n < max - 1) buf[n++] = c;
    if (c < 0 && n == 0) return -1;
    if (n && buf[n - 1] == '\r') n--;
    buf[n] = 0;
    return n;
}

static int same(const char *a, const char *b, int n)    /* the first n characters, any case */
{
    int k;
    for (k = 0; k < n; k++) {
        int x = a[k], y = b[k];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
        if (!x) return 1;
    }
    return 1;
}

struct url { int tls; char host[256]; int port; char path[1024]; };

static int parse(const char *u, struct url *p)
{
    const char *h, *e;
    int n;
    if (!strncmp(u, "http://", 7)) { p->tls = 0; p->port = 80; h = u + 7; }
    else if (!strncmp(u, "https://", 8)) { p->tls = 1; p->port = 443; h = u + 8; }
    else return -1;
    for (e = h; *e && *e != '/' && *e != ':'; e++) ;
    n = e - h;
    if (n == 0 || n >= (int)sizeof p->host) return -1;
    memcpy(p->host, h, n);
    p->host[n] = 0;
    if (*e == ':') {
        p->port = atoi(e + 1);
        while (*e && *e != '/') e++;
        if (p->port <= 0 || p->port > 65535) return -1;
    }
    if (strlen(*e ? e : "/") >= sizeof p->path) return -1;
    strcpy(p->path, *e ? e : "/");
    return 0;
}

static int dial(struct url *u)
{
    struct sockaddr_in a;
    struct hostent *h;
    int s;
    if (!(h = gethostbyname(u->host))) { fprintf(stderr, "fetch: %s: %s\n", u->host, hstrerror(h_errno)); return -1; }
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(u->port);
    memcpy(&a.sin_addr, h->h_addr, 4);
    if ((s = socket(AF_INET, SOCK_STREAM, u->tls ? IPPROTO_TLS : 0)) < 0) { fprintf(stderr, "fetch: %s\n", strerror(errno)); return -1; }
    if (u->tls) setsockopt(s, IPPROTO_TLS, TLS_HOSTNAME, u->host, strlen(u->host));
    if (connect(s, (struct sockaddr *)&a, sizeof a) < 0) {
        fprintf(stderr, "fetch: %s: %s%s\n", u->host, strerror(errno),
                errno == ECONNABORTED && u->tls ? " (TLS: the certificate is not trusted, or not for this name)" : "");
        close(s);
        return -1;
    }
    return s;
}

static int writeall(int out, const unsigned char *p, int n)
{
    int k = 0, r;
    while (k < n) {
        if ((r = write(out, p + k, n - k)) <= 0) return -1;
        k += r;
    }
    return 0;
}

/* the body: len bytes (-1: to the end), to out; the bytes written, or -1 */
static long body(int out, long len)
{
    long done = 0;
    while (len < 0 || done < len) {
        int n;
        if (ipos == ilen) {
            ilen = read(fd, ib, sizeof ib);
            ipos = 0;
            if (ilen <= 0) { ilen = 0; break; }
        }
        n = ilen - ipos;
        if (len >= 0 && n > len - done) n = len - done;
        if (writeall(out, ib + ipos, n) < 0) { fprintf(stderr, "fetch: write: %s\n", strerror(errno)); return -1; }
        ipos += n;
        done += n;
    }
    if (len >= 0 && done < len) fprintf(stderr, "fetch: the connection ended %ld bytes short\n", len - done);
    return done;
}

static long chunked(int out)            /* "Transfer-Encoding: chunked": a hex size, its bytes, ... 0 */
{
    char line[64];
    long total = 0, n, r;
    for (;;) {
        if (getline_(line, sizeof line) < 0) return total;
        n = strtol(line, 0, 16);
        if (n <= 0) break;
        if ((r = body(out, n)) < 0) return -1;
        total += r;
        getline_(line, sizeof line);    /* (the CR LF after the chunk) */
    }
    while (getline_(line, sizeof line) > 0) ;       /* (trailers) */
    return total;
}

static const char *lastpart(const char *path)
{
    static char name[64];
    const char *p = strrchr(path, '/'), *q;
    int n;
    p = p ? p + 1 : path;
    for (q = p; *q && *q != '?' && *q != '#'; q++) ;
    n = q - p;
    if (n == 0) return "index.html";
    if (n > 28) n = 28;                 /* (P32W's names stop at 28 characters) */
    memcpy(name, p, n);
    name[n] = 0;
    return name;
}

int main(int argc, char **argv)
{
    struct url u;
    char line[1024], loc[1024], req[1400];
    const char *file = 0, *url;
    int i, k, status = 0, hops, out = 1, isch, named = 0;
    long len, got;
    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char *o;
        for (o = argv[i] + 1; *o; o++)
            if (*o == 'q') quiet = 1;
            else if (*o == 'v') verbose = 1;
            else if (*o == 'O') named = 1;
            else if (*o == 'o' && !o[1] && i + 1 < argc) { file = argv[++i]; break; }
            else { fprintf(stderr, "usage: fetch [-qv] [-o file | -O] url\n"); return 2; }
    }
    if (i != argc - 1) { fprintf(stderr, "usage: fetch [-qv] [-o file | -O] url\n"); return 2; }
    url = argv[i];
    strncpy(loc, url, sizeof loc - 1);
    for (hops = 0; ; hops++) {
        if (parse(loc, &u) < 0) { fprintf(stderr, "fetch: %s: not an http:// or https:// URL\n", loc); return 2; }
        if ((fd = dial(&u)) < 0) return 1;
        ilen = ipos = 0;
        k = (u.tls ? u.port != 443 : u.port != 80);
        sprintf(req, "GET %s HTTP/1.1\r\nHost: %s", u.path, u.host);
        if (k) sprintf(req + strlen(req), ":%d", u.port);
        strcat(req, "\r\nUser-Agent: P32W-fetch/1\r\nAccept: */*\r\nConnection: close\r\n\r\n");
        if (writeall(fd, (unsigned char *)req, strlen(req)) < 0) { fprintf(stderr, "fetch: %s: %s\n", u.host, strerror(errno)); return 1; }
        if (getline_(line, sizeof line) < 0 || strncmp(line, "HTTP/", 5) || !strchr(line, ' ')) {
            fprintf(stderr, "fetch: %s: no HTTP reply\n", u.host);
            return 1;
        }
        status = atoi(strchr(line, ' ') + 1);
        if (verbose) fprintf(stderr, "%s\n", line);
        len = -1;
        isch = 0;
        loc[0] = 0;
        {
            char reply[128];
            strncpy(reply, strchr(line, ' ') + 1, sizeof reply - 1);
            reply[sizeof reply - 1] = 0;
            while (getline_(line, sizeof line) > 0) {
                if (verbose) fprintf(stderr, "%s\n", line);
                if (same(line, "content-length:", 15)) len = atol(line + 15);
                else if (same(line, "transfer-encoding:", 18) && strstr(line, "chunked")) isch = 1;
                else if (same(line, "location:", 9)) {
                    char *v = line + 9;
                    while (*v == ' ') v++;
                    if (*v == '/') sprintf(loc, "%s://%s:%d%s", u.tls ? "https" : "http", u.host, u.port, v);
                    else strncpy(loc, v, sizeof loc - 1);
                }
            }
            if ((status == 301 || status == 302 || status == 303 || status == 307 || status == 308) && loc[0]) {
                close(fd);
                if (hops == 5) { fprintf(stderr, "fetch: too many redirections\n"); return 1; }
                if (!quiet) fprintf(stderr, "fetch: %d, to %s\n", status, loc);
                continue;
            }
            if (status < 200 || status > 299) {
                fprintf(stderr, "fetch: %s: %s\n", url, reply);
                close(fd);
                return 1;
            }
        }
        break;
    }
    if (named) file = lastpart(u.path);
    if (file && (out = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0) {
        fprintf(stderr, "fetch: %s: %s\n", file, strerror(errno));
        return 1;
    }
    got = isch ? chunked(out) : body(out, len);
    close(fd);
    if (file) close(out);
    if (got < 0) return 1;
    if (file && !quiet) fprintf(stderr, "fetch: %ld bytes to %s\n", got, file);
    return 0;
}
