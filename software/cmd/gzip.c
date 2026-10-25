/* gzip, gunzip, zcat -- compress or expand files, gzip format (RFC 1952;
 * D25), exchangeable with the Mac's gzip.
 *
 *   gzip [-cdfknNqtv0..9] [file ...]  file -> file.gz (the original removed unless -k or -c)
 *   gunzip [...] file.gz ...          the same as gzip -d (also .tgz -> .tar, .z)
 *   zcat file.gz ...                  the same as gzip -dc
 *
 * No files, or "-": stdin to stdout. The output keeps the input's mode; its
 * modification time is the input's, or when expanding the one stored in
 * the gzip header (not with -n); -N also restores the stored name.
 * Concatenated gzip members are expanded in turn. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <utime.h>
#include <deflate.h>
#include <sys/stat.h>

#define NAMEMAX 256

static int to_stdout, decomp, force, keep, quiet, test, verbose, level = 6;
static int no_name, restore_name;     /* -n, -N */
static int status;

struct in { int fd, pos, len, eof; unsigned char buf[4096]; unsigned char rest[8]; int nrest; };
struct out { int fd; unsigned crc, size; };

static int in_byte(void *v)
{
    struct in *s = v;
    if (s->nrest) {                     /* (bytes inflate read past the data, handed back) */
        int c = s->rest[0];
        memmove(s->rest, s->rest + 1, --s->nrest);
        return c;
    }
    if (s->pos >= s->len) {
        if (s->eof) return -1;
        s->len = read(s->fd, s->buf, sizeof s->buf);
        s->pos = 0;
        if (s->len <= 0) { s->len = 0; s->eof = 1; return -1; }
    }
    return s->buf[s->pos++];
}

static unsigned in_word(struct in *s)       /* 4 bytes, little-endian */
{
    unsigned v = 0, k;
    for (k = 0; k < 4; k++) {
        int c = in_byte(s);
        if (c < 0) return 0xFFFFFFFF;
        v |= (unsigned)c << (8 * k);
    }
    return v;
}

static struct in *rd_in;
static unsigned rd_crc, rd_size;

static int rd(void *v, unsigned char *p, unsigned n)   /* deflate's input: from rd_in, counted */
{
    int k = read(((struct in *)v)->fd, p, n);
    if (k > 0) { rd_crc = crc32(rd_crc, p, k); rd_size += k; }
    return k;
}

static int wr(void *v, const unsigned char *p, unsigned n)
{
    struct out *o = v;
    o->crc = crc32(o->crc, p, n);
    o->size += n;
    if (test) return 0;
    while (n) {
        int k = write(o->fd, p, n);
        if (k <= 0) return -1;
        p += k;
        n -= k;
    }
    return 0;
}

static int wr_raw(void *v, const unsigned char *p, unsigned n)
{
    struct out *o = v;
    o->size += n;
    while (n) {
        int k = write(o->fd, p, n);
        if (k <= 0) return -1;
        p += k;
        n -= k;
    }
    return 0;
}

static void put_word(unsigned char *b, unsigned v)
{
    b[0] = v; b[1] = v >> 8; b[2] = v >> 16; b[3] = v >> 24;
}

static unsigned in_total, out_total;    /* (the last file's sizes, for -v) */

/* in -> gzip on fd out; 0 or -1. The name and time go in the header (not with -n). */
static int compress(int in, int out, const char *name, unsigned mtime)
{
    unsigned char h[10], t[8];
    struct in s;
    struct out o;
    const char *base = name ? strrchr(name, '/') : 0;
    int r;
    base = base ? base + 1 : name;
    if (no_name) { base = 0; mtime = 0; }
    h[0] = 0x1F; h[1] = 0x8B; h[2] = 8;
    h[3] = base ? 8 : 0;                /* FNAME */
    put_word(h + 4, mtime);
    h[8] = level == 9 ? 2 : level == 1 ? 4 : 0;
    h[9] = 3;                           /* made on Unix */
    o.fd = out; o.crc = 0; o.size = 0;
    if (wr_raw(&o, h, 10) < 0) return -1;
    if (base && wr_raw(&o, (const unsigned char *)base, strlen(base) + 1) < 0) return -1;
    s.fd = in;
    rd_crc = rd_size = 0;
    r = deflate_raw(rd, &s, wr_raw, &o, level);
    if (r) return -1;
    put_word(t, rd_crc);
    put_word(t + 4, rd_size);
    if (wr_raw(&o, t, 8) < 0) return -1;
    in_total = rd_size;
    out_total = o.size;
    return 0;
}

/* a member's header. 1: read (its name, if any, to hname; its time to *mtime);
   0: the end of the input after at least one member; -1: an error, said */
static int gz_header(struct in *s, const char *name, int members, char *hname, unsigned *mtime)
{
    int c, flags, n = 0;
    c = in_byte(s);
    if (c < 0 && members) return 0;     /* the end, after at least one member */
    if (c != 0x1F || in_byte(s) != 0x8B) {
        if (members) {                  /* trailing garbage after a member: ignored */
            if (!quiet) fprintf(stderr, "gzip: %s: trailing garbage ignored\n", name);
            return 0;
        }
        fprintf(stderr, "gzip: %s: not in gzip format\n", name);
        return -1;
    }
    if (in_byte(s) != 8) { fprintf(stderr, "gzip: %s: unknown method\n", name); return -1; }
    flags = in_byte(s);
    *mtime = in_word(s);
    in_byte(s); in_byte(s);             /* xfl, os */
    if (flags & 4) {                    /* FEXTRA */
        int k = in_byte(s);
        k |= in_byte(s) << 8;
        while (k-- > 0) in_byte(s);
    }
    if (hname) hname[0] = 0;
    if (flags & 8)                      /* FNAME */
        while ((c = in_byte(s)) > 0)
            if (hname && n < NAMEMAX - 1) { hname[n++] = c; hname[n] = 0; }
    if (flags & 16) while ((c = in_byte(s)) > 0) ;     /* FCOMMENT */
    if (flags & 2) { in_byte(s); in_byte(s); }         /* FHCRC */
    if (flags & 0xE0) { fprintf(stderr, "gzip: %s: unknown flags\n", name); return -1; }
    if (c < 0 && (flags & 24)) { fprintf(stderr, "gzip: %s: unexpected end of file\n", name); return -1; }
    return 1;
}

/* the members on s -> out, the first one's header already read; 0, or -1 with a message */
static int expand(struct in *s, int out, const char *name)
{
    struct out o;
    int members = 0;
    unsigned t;
    o.fd = out;
    for (;;) {
        int r;
        unsigned crc, size;
        if (members && (r = gz_header(s, name, members, 0, &t)) <= 0) return r;
        o.crc = o.size = 0;
        r = inflate_raw(in_byte, s, wr, &o);
        if (r) {
            fprintf(stderr, "gzip: %s: %s\n", name, r == Z_EOF ? "unexpected end of file" :
                    r == Z_IO ? strerror(errno) : "invalid compressed data");
            return -1;
        }
        {
            unsigned char rest[8];
            int n = inflate_rest(rest), k;
            for (k = n - 1; k >= 0; k--) {  /* push them back in front of the buffer */
                memmove(s->rest + 1, s->rest, s->nrest++);
                s->rest[0] = rest[k];
            }
        }
        crc = in_word(s);
        size = in_word(s);
        if (crc != o.crc || size != o.size) {
            fprintf(stderr, "gzip: %s: %s\n", name, crc != o.crc ? "crc error" : "length error");
            return -1;
        }
        members++;
    }
}

static const char *suffix(const char *name)     /* the gzip suffix it ends with, or 0 */
{
    static const char *const sfx[] = { ".gz", ".tgz", ".z", 0 };
    int n = strlen(name), i;
    for (i = 0; sfx[i]; i++) {
        int k = strlen(sfx[i]);
        if (n > k && !strcmp(name + n - k, sfx[i])) return sfx[i];
    }
    return 0;
}

static unsigned percent(unsigned big, unsigned small)  /* how much smaller, in percent */
{
    return big > small && big >= 100 ? (big - small) / (big / 100) : 0;
}

static struct in gin;

/* the standard input to the standard output (no files, or -) */
static int filter(void)
{
    char hname[NAMEMAX];
    unsigned t;
    if (!decomp && isatty(1) && !force) {
        fprintf(stderr, "gzip: compressed data not written to a terminal (-f to force)\n");
        return 1;
    }
    if (decomp) {
        memset(&gin, 0, sizeof gin);
        gin.fd = 0;
        if (gz_header(&gin, "stdin", 0, hname, &t) < 0) return 1;
        if (expand(&gin, 1, "stdin") < 0) return 1;
        if (test && verbose) fprintf(stderr, "stdin:\tOK\n");
        return 0;
    }
    if (compress(0, 1, 0, 0) < 0) { fprintf(stderr, "gzip: stdout: %s\n", strerror(errno)); return 1; }
    return 0;
}

static void warn(const char *name, const char *why)   /* (not with -q) */
{
    if (!quiet) fprintf(stderr, "gzip: %s%s\n", name, why);
    status = 1;
}

static void one(const char *name)
{
    char outname[NAMEMAX + 8], hname[NAMEMAX];
    struct stat st;
    int in, out, r;
    unsigned mtime;
    struct utimbuf ut;
    if (!strcmp(name, "-")) { if (filter()) status = 1; return; }
    if (stat(name, &st) < 0) { fprintf(stderr, "gzip: %s: %s\n", name, strerror(errno)); status = 1; return; }
    if (!S_ISREG(st.st_mode)) { warn(name, ": not a regular file, ignored"); return; }
    if (strlen(name) >= NAMEMAX) { fprintf(stderr, "gzip: %s: name too long\n", name); status = 1; return; }
    mtime = st.st_mtime;
    if (decomp) {
        const char *sfx = suffix(name);
        int n = strlen(name);
        if (!sfx && !to_stdout && !test) { warn(name, ": unknown suffix, ignored"); return; }
        if (sfx) {
            strncpy(outname, name, n - strlen(sfx));
            outname[n - strlen(sfx)] = 0;
            if (!strcmp(sfx, ".tgz")) strcat(outname, ".tar");
        }
    } else {
        if (suffix(name) && !to_stdout) { warn(name, " already has a .gz suffix, ignored"); return; }
        sprintf(outname, "%s.gz", name);
    }
    if ((in = open(name, O_RDONLY)) < 0) { fprintf(stderr, "gzip: %s: %s\n", name, strerror(errno)); status = 1; return; }
    if (decomp) {                       /* the first header: the stored name and time */
        unsigned t;
        memset(&gin, 0, sizeof gin);
        gin.fd = in;
        if (gz_header(&gin, name, 0, hname, &t) < 0) { close(in); status = 1; return; }
        if (!no_name && t) mtime = t;
        if (restore_name && hname[0]) { /* -N: that name (its last part), in the file's directory */
            const char *b = strrchr(hname, '/'), *d = strrchr(name, '/');
            b = b ? b + 1 : hname;
            if (*b && strcmp(b, ".") && strcmp(b, "..")) {
                int k = d ? d - name + 1 : 0;
                memcpy(outname, name, k);
                strcpy(outname + k, b);
            }
        }
    }
    if (to_stdout || test)
        out = 1;
    else {
        if (!force && access(outname, 0) == 0) {
            fprintf(stderr, "gzip: %s already exists (-f to replace it)\n", outname);
            close(in); status = 1; return;
        }
        if ((out = open(outname, O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0) {
            fprintf(stderr, "gzip: %s: %s\n", outname, strerror(errno));
            close(in); status = 1; return;
        }
    }
    r = decomp ? expand(&gin, out, name) : compress(in, out, name, st.st_mtime);
    if (!decomp && r < 0) fprintf(stderr, "gzip: %s: %s\n", name, strerror(errno));
    close(in);
    if (r < 0) status = 1;
    if (out != 1) {
        close(out);
        if (r < 0) { unlink(outname); return; }
        chmod(outname, st.st_mode & 07777);
        ut.actime = ut.modtime = mtime;
        utime(outname, &ut);
        if (!keep) unlink(name);
    }
    if (r < 0 || !verbose) return;
    if (test) fprintf(stderr, "%s:\tOK\n", name);
    else if (!decomp)
        fprintf(stderr, "%s:\t%u%%%s%s\n", name, percent(in_total, out_total),
                to_stdout ? "" : keep ? " -- created " : " -- replaced with ", to_stdout ? "" : outname);
    else if (!to_stdout)
        fprintf(stderr, "%s:\t-- %s %s\n", name, keep ? "created" : "replaced with", outname);
}

int main(int argc, char **argv)
{
    const char *prog = strrchr(argv[0], '/');
    int i;
    prog = prog ? prog + 1 : argv[0];
    if (!strcmp(prog, "gunzip")) decomp = 1;
    if (!strcmp(prog, "zcat")) decomp = to_stdout = 1;
    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char *o;
        if (!strcmp(argv[i], "--")) { i++; break; }
        for (o = argv[i] + 1; *o; o++)
            switch (*o) {
            case 'c': to_stdout = 1; break;
            case 'd': decomp = 1; break;
            case 'f': force = 1; break;
            case 'k': keep = 1; break;
            case 'n': no_name = 1; restore_name = 0; break;
            case 'N': restore_name = 1; no_name = 0; break;
            case 'q': quiet = 1; break;
            case 't': test = decomp = 1; break;
            case 'v': verbose = 1; break;
            default:
                if (*o >= '0' && *o <= '9') { level = *o - '0'; break; }
                fprintf(stderr, "usage: %s [-cdfknNqtv0..9] [file ...]\n", prog);
                return 2;
            }
    }
    if (to_stdout) keep = 1;
    if (i == argc) return filter();
    for (; i < argc; i++) one(argv[i]);
    return status;
}
