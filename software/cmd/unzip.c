/* unzip -- list, test or extract a zip archive (PKZIP format; D25): from
 * the Mac's zip, Finder's "Compress", or P32W's own zip.
 *
 *   unzip [-lqtoan] [-d dir] archive[.zip] [name ...]
 *
 *   -l  list    -t  test the CRCs    -q  quiet    -d dir  extract there
 *   -o  overwrite without asking     -n  never overwrite
 *   -a  also extract __MACOSX/ (Finder's resource forks; left out otherwise)
 *
 * Stored and deflated entries; modes from a Unix-made archive, times from
 * the "UT" or "UX" extra field (else the DOS time: local time, by TZ). Names: all, or
 * those given and what is below them. Encrypted and Zip64 entries are
 * refused; symbolic links are skipped, and a file failing its CRC is removed.
 * Owners ("ux", else "UX") are restored when run as root. Asks before
 * replacing a file (y, n, A all, N none) when stdin is a terminal. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <utime.h>
#include <deflate.h>
#include <sys/stat.h>

#define NAMEMAX 256

static int list, test, quiet, overwrite, never, macosx, status;
static char *dir;
static char **names;
static int nnames;
static char *found;                 /* (which names were met) */
static int fd;

static unsigned g16(const unsigned char *b) { return b[0] | b[1] << 8; }
static unsigned g32(const unsigned char *b) { return b[0] | b[1] << 8 | b[2] << 16 | (unsigned)b[3] << 24; }

static void die(const char *what, const char *why)
{
    fprintf(stderr, "unzip: %s: %s\n", what, why);
    exit(2);
}

/* ---------------------------------------------------------------- an entry's data, as a byte stream */
struct src { unsigned left; int pos, len; unsigned char buf[4096]; };

static int src_byte(void *v)
{
    struct src *s = v;
    if (s->pos >= s->len) {
        int n;
        if (!s->left) return -1;
        n = read(fd, s->buf, s->left < sizeof s->buf ? s->left : sizeof s->buf);
        if (n <= 0) return -1;
        s->left -= n;
        s->len = n;
        s->pos = 0;
    }
    return s->buf[s->pos++];
}

struct dst { int fd; unsigned crc, size; };

static int dst_write(void *v, const unsigned char *p, unsigned n)
{
    struct dst *d = v;
    d->crc = crc32(d->crc, p, n);
    d->size += n;
    if (d->fd < 0) return 0;
    while (n) {
        int k = write(d->fd, p, n);
        if (k <= 0) return -1;
        p += k;
        n -= k;
    }
    return 0;
}

/* ---------------------------------------------------------------- helpers */
static int wanted(const char *name)
{
    int i, n, r = 0;
    if (!macosx && !strncmp(name, "__MACOSX/", 9)) return 0;
    if (!nnames) return 1;
    for (i = 0; i < nnames; i++) {
        n = strlen(names[i]);
        while (n > 1 && names[i][n - 1] == '/') n--;
        if (!strncmp(name, names[i], n) && (name[n] == 0 || name[n] == '/')) { found[i] = 1; r = 1; }
    }
    return r;
}

static void mkparents(char *path)
{
    char *s;
    for (s = path + 1; *s; s++)
        if (*s == '/') {
            *s = 0;
            if (access(path, 0) < 0) mkdir(path, 0777);
            *s = '/';
        }
}

static int may_replace(const char *path)       /* 1: go ahead */
{
    char line[16];
    if (access(path, 0) < 0 || overwrite) return 1;
    if (never) return 0;
    if (!isatty(0)) {
        fprintf(stderr, "unzip: %s exists, not replaced (-o to replace)\n", path);
        status = 1;
        return 0;
    }
    for (;;) {
        fprintf(stderr, "replace %s? [y]es, [n]o, [A]ll, [N]one: ", path);
        if (!fgets(line, sizeof line, stdin)) return 0;
        if (line[0] == 'y') return 1;
        if (line[0] == 'n') return 0;
        if (line[0] == 'A') { overwrite = 1; return 1; }
        if (line[0] == 'N') { never = 1; return 0; }
    }
}

static unsigned dos_to_unix(unsigned date, unsigned time)
{
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    tm.tm_year = (date >> 9) + 80;
    tm.tm_mon = ((date >> 5) & 15) - 1;
    tm.tm_mday = date & 31;
    tm.tm_hour = time >> 11;
    tm.tm_min = (time >> 5) & 63;
    tm.tm_sec = (time & 31) * 2;
    return mktime(&tm);
}

/* ---------------------------------------------------------------- one entry */
struct dirattr { unsigned mode, mtime; int uid, gid; struct dirattr *next; char path[1]; };
static struct dirattr *dirattrs;

/* an owner from a local header's extra fields: "ux" (Info-ZIP, zip(1)), else the
   16-bit one of "UX"; 1 if found */
static int owner(const unsigned char *x, unsigned xl, int *uid, int *gid)
{
    unsigned k;
    int found = 0;
    for (k = 0; k + 4 <= xl && k + 4 + g16(x + k + 2) <= xl; k += 4 + g16(x + k + 2)) {
        unsigned id = g16(x + k), sz = g16(x + k + 2);
        if (id == 0x7875 && sz >= 3 && x[k + 4] == 1) {
            unsigned us = x[k + 5], gs, u = 0, g = 0, i;
            if (us > 4 || 3 + us > sz) continue;
            gs = x[k + 6 + us];
            if (gs > 4 || 3 + us + gs > sz) continue;
            for (i = 0; i < us; i++) u |= (unsigned)x[k + 6 + i] << (8 * i);
            for (i = 0; i < gs; i++) g |= (unsigned)x[k + 7 + us + i] << (8 * i);
            *uid = u;
            *gid = g;
            return 1;
        }
        if (id == 0x5855 && sz >= 12 && !found) { *uid = g16(x + k + 12); *gid = g16(x + k + 14); found = 1; }
    }
    return found;
}

static void entry(const unsigned char *h, const char *name, const unsigned char *x, unsigned xl)
{
    unsigned flags = g16(h + 8), method = g16(h + 10), crc = g32(h + 16);
    unsigned csize = g32(h + 20), usize = g32(h + 24), ext = g32(h + 38), off = g32(h + 42);
    unsigned host = g16(h + 4) >> 8, mtime = 0, mode, k;
    int isdir = name[0] && name[strlen(name) - 1] == '/';
    int uid = -1, gid = -1;
    char path[NAMEMAX + 300];
    unsigned char lh[30], lx[1024];
    struct src s;
    struct dst d;
    int r;
    for (k = 0; k + 4 <= xl; k += 4 + g16(x + k + 2)) {    /* the modification time: */
        if (g16(x + k) == 0x5455 && g16(x + k + 2) >= 5 && (x[k + 4] & 1)) mtime = g32(x + k + 5);  /* UT */
        if (g16(x + k) == 0x5855 && g16(x + k + 2) >= 8 && !mtime) mtime = g32(x + k + 8);         /* UX (ditto, Finder) */
    }
    if (!mtime) mtime = dos_to_unix(g16(h + 14), g16(h + 12));
    mode = host == 3 && (ext >> 16) ? (ext >> 16) & 07777 : isdir ? 0755 : 0644;
    if (list) {
        time_t t = mtime;
        struct tm *tm = localtime(&t);
        printf("%9u  %04d-%02d-%02d %02d:%02d   %s\n", usize, tm->tm_year + 1900, tm->tm_mon + 1,
               tm->tm_mday, tm->tm_hour, tm->tm_min, name);
        return;
    }
    if (flags & 1) { fprintf(stderr, "unzip: %s: encrypted, skipped\n", name); status = 1; return; }
    if (csize == 0xFFFFFFFF || usize == 0xFFFFFFFF) { fprintf(stderr, "unzip: %s: Zip64, not supported\n", name); status = 1; return; }
    if (method != 0 && method != 8) { fprintf(stderr, "unzip: %s: compression method %u not supported\n", name, method); status = 1; return; }
    if (name[0] == '/' || strstr(name, "../") || !strcmp(name, "..")) {
        fprintf(stderr, "unzip: %s: not extracted (an absolute path or ..)\n", name); status = 1; return;
    }
    if (host == 3 && ((ext >> 16) & 0170000) == 0120000) {
        fprintf(stderr, "unzip: %s: a symbolic link, skipped\n", name); status = 1; return;
    }
    /* the local header: the data follows it (its own name and extra lengths); its extra
       fields hold the owner */
    lseek(fd, off, 0);
    if (read(fd, lh, 30) != 30 || g32(lh) != 0x04034B50) { fprintf(stderr, "unzip: %s: bad local header\n", name); status = 1; return; }
    k = g16(lh + 28);
    lseek(fd, off + 30 + g16(lh + 26), 0);
    if (k <= sizeof lx && read(fd, lx, k) == (int)k) {
        if (geteuid() == 0 && !owner(lx, k, &uid, &gid)) uid = gid = -1;
    }
    lseek(fd, off + 30 + g16(lh + 26) + g16(lh + 28), 0);
    if (dir) sprintf(path, "%s/%s", dir, name); else strcpy(path, name);
    if (!test) mkparents(path);
    if (isdir) {
        if (test) {
            if (!quiet) printf("    testing: %-40s OK\n", name);
            return;
        }
        path[strlen(path) - 1] = 0;
        if (mkdir(path, 0700) < 0 && errno != EEXIST) { fprintf(stderr, "unzip: %s: %s\n", path, strerror(errno)); status = 1; return; }
        {   /* its mode and time last */
            struct dirattr *a = malloc(sizeof *a + strlen(path));
            if (a) {
                a->mode = mode; a->mtime = mtime; a->uid = uid; a->gid = gid;
                strcpy(a->path, path); a->next = dirattrs; dirattrs = a;
            }
        }
        if (!quiet) printf("   creating: %s\n", path);
        return;
    }
    d.crc = d.size = 0;
    d.fd = -1;
    if (!test) {
        if (!may_replace(path)) return;
        unlink(path);
        if ((d.fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0) {
            fprintf(stderr, "unzip: %s: %s\n", path, strerror(errno)); status = 1; return;
        }
    }
    s.left = csize; s.pos = s.len = 0;
    if (method == 8) {
        if (!quiet && !test) printf("  inflating: %s\n", path);
        fflush(stdout);                 /* (before any message about it) */
        r = inflate_raw(src_byte, &s, dst_write, &d);
    } else {
        int c;
        if (!quiet && !test) printf(" extracting: %s\n", path);
        fflush(stdout);
        r = 0;
        while ((c = src_byte(&s)) >= 0) {
            unsigned char b = c;
            if (dst_write(&d, &b, 1) < 0) { r = Z_IO; break; }
        }
    }
    if (d.fd >= 0) close(d.fd);
    if (r || d.crc != crc || d.size != usize) {
        fprintf(stderr, "unzip: %s: %s%s\n", name, r == Z_IO ? strerror(errno) : r ? "invalid compressed data" :
                d.crc != crc ? "bad CRC" : "bad size", test ? "" : ", removed");
        if (!test) unlink(path);
        status = 1;
        return;
    }
    if (test) {
        if (!quiet) printf("    testing: %-40s OK\n", name);
        return;
    }
    {
        struct utimbuf ut;
        if (uid >= 0) chown(path, uid, gid);
        chmod(path, mode);
        ut.actime = ut.modtime = mtime;
        utime(path, &ut);
    }
}

int main(int argc, char **argv)
{
    unsigned char tail[22 + 1024], h[46], x[1024];
    char archive[NAMEMAX + 8], name[NAMEMAX];
    int i = 1, size, n, k, count, e;
    unsigned cdoff, total = 0, files = 0;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char *o;
        for (o = argv[i] + 1; *o; o++)
            switch (*o) {
            case 'l': list = 1; break;
            case 't': test = 1; break;
            case 'q': quiet = 1; break;
            case 'o': overwrite = 1; break;
            case 'n': never = 1; break;
            case 'a': macosx = 1; break;
            case 'd':
                if (++i >= argc) { fprintf(stderr, "unzip: -d needs a directory\n"); return 2; }
                dir = argv[i];
                break;
            default:
                fprintf(stderr, "usage: unzip [-lqtoan] [-d dir] archive[.zip] [name ...]\n");
                return 2;
            }
    }
    if (i >= argc) { fprintf(stderr, "usage: unzip [-lqtoan] [-d dir] archive[.zip] [name ...]\n"); return 2; }
    if (strlen(argv[i]) >= NAMEMAX) die(argv[i], "name too long");
    strcpy(archive, argv[i++]);
    {   /* the name, else the name with .zip (also when the name is a directory) */
        struct stat st;
        if ((fd = open(archive, O_RDONLY)) >= 0 && (fstat(fd, &st) < 0 || S_ISDIR(st.st_mode))) {
            close(fd);
            fd = -1;
            errno = EISDIR;
        }
        if (fd < 0) {
            int err = errno;
            strcat(archive, ".zip");
            if ((fd = open(archive, O_RDONLY)) < 0) die(argv[i - 1], strerror(err));
        }
    }
    names = argv + i;
    nnames = argc - i;
    if (!(found = calloc(nnames + 1, 1))) die(archive, "out of memory");
    if (dir && access(dir, 0) < 0 && mkdir(dir, 0777) < 0) die(dir, strerror(errno));
    /* the end-of-central-directory record, within the last 1 KiB + 22 */
    size = lseek(fd, 0, 2);
    n = size < (int)sizeof tail ? size : (int)sizeof tail;
    lseek(fd, size - n, 0);
    if (read(fd, tail, n) != n) die(archive, "read error");
    for (k = n - 22; k >= 0; k--)
        if (g32(tail + k) == 0x06054B50) break;
    if (k < 0) die(archive, "not a zip archive");
    count = g16(tail + k + 10);
    cdoff = g32(tail + k + 16);
    if (!quiet && !list) printf("Archive:  %s\n", archive);
    fflush(stdout);
    if (list) printf("   Length  Date       Time    Name\n---------  ---------- -----   ----\n");
    for (e = 0; e < count; e++) {
        unsigned nl, xl, cl;
        lseek(fd, cdoff, 0);
        if (read(fd, h, 46) != 46 || g32(h) != 0x02014B50) die(archive, "damaged central directory");
        nl = g16(h + 28); xl = g16(h + 30); cl = g16(h + 32);
        if (nl >= NAMEMAX) die(archive, "an entry's name is too long");
        if (read(fd, name, nl) != (int)nl) die(archive, "read error");
        name[nl] = 0;
        if (xl > sizeof x) xl = 0;
        else if (read(fd, x, xl) != (int)xl) die(archive, "read error");
        cdoff += 46 + nl + g16(h + 30) + cl;
        if (!wanted(name)) continue;
        entry(h, name, x, xl);
        fflush(stdout);
        total += g32(h + 24);
        files++;
    }
    if (list) printf("---------                     -------\n%9u                     %u file%s\n", total, files, files == 1 ? "" : "s");
    while (dirattrs) {
        struct utimbuf ut;
        if (dirattrs->uid >= 0) chown(dirattrs->path, dirattrs->uid, dirattrs->gid);
        chmod(dirattrs->path, dirattrs->mode);
        ut.actime = ut.modtime = dirattrs->mtime;
        utime(dirattrs->path, &ut);
        dirattrs = dirattrs->next;
    }
    for (e = 0; e < nnames; e++)
        if (!found[e]) { fprintf(stderr, "unzip: %s: not found in the archive\n", names[e]); status = 1; }
    if (test && !status && !quiet) printf("No errors detected in %s.\n", archive);
    return status;
}
