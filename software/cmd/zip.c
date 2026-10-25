/* zip -- make or update a zip archive (PKZIP format; D25), exchangeable
 * with the Mac's zip / unzip and Finder.
 *
 *   zip [-rq0..9] archive[.zip] file ...
 *
 * Adds the files (with -r, directories and everything below them) to the
 * archive, replacing entries of the same name; a new archive if there is
 * none. Entries are deflated (-0: stored). Each keeps its Unix mode, owner
 * and modification time (the "UT", "ux" and "UX" extra fields that Info-ZIP
 * and macOS read), so they come back as they were. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <deflate.h>
#include <sys/stat.h>

#define NAMEMAX 256

struct entry {
    char name[NAMEMAX];
    unsigned crc, csize, usize, offset, mtime, mode, uid, gid, made_by, ext_attr;
    int method, flags, needed, dostime, dosdate;
    int owner;                          /* 1: uid and gid known (written as "ux") */
    unsigned src_offset;                /* (kept from the old archive: its local header there) */
    int old;                            /* 1: from the old archive, not replaced */
    struct entry *next;
};

static struct entry *entries, *last_entry;
static int level = 6, recurse, quiet, status, nadded;
static int ofd, ifd_old = -1;
static unsigned opos;
static unsigned char obuf[4096];
static int olen;
static char archive[NAMEMAX + 8], temp[NAMEMAX + 16];
static int self_dev[2] = { -1, -1 };    /* the old archive and the new one: not added */
static unsigned self_ino[2];

static void die(const char *what, const char *why)
{
    fprintf(stderr, "zip: %s: %s\n", what, why);
    if (temp[0]) unlink(temp);
    exit(2);
}

/* ---------------------------------------------------------------- output, with its position */
static void oflush(void)
{
    int k = 0, n;
    while (k < olen) {
        n = write(ofd, obuf + k, olen - k);
        if (n <= 0) die(temp, strerror(errno));
        k += n;
    }
    olen = 0;
}

static void obytes(const void *p, unsigned n)
{
    const unsigned char *s = p;
    opos += n;
    while (n) {
        unsigned k = sizeof obuf - olen;
        if (k > n) k = n;
        memcpy(obuf + olen, s, k);
        olen += k; s += k; n -= k;
        if (olen == sizeof obuf) oflush();
    }
}

static void o16(unsigned v) { unsigned char b[2]; b[0] = v; b[1] = v >> 8; obytes(b, 2); }
static void o32(unsigned v) { unsigned char b[4]; b[0] = v; b[1] = v >> 8; b[2] = v >> 16; b[3] = v >> 24; obytes(b, 4); }
static unsigned g16(const unsigned char *b) { return b[0] | b[1] << 8; }
static unsigned g32(const unsigned char *b) { return b[0] | b[1] << 8 | b[2] << 16 | (unsigned)b[3] << 24; }

static void dos_time(struct entry *e)
{
    time_t t = e->mtime;
    struct tm *tm = localtime(&t);      /* (a DOS time is local time) */
    if (tm->tm_year < 80) { e->dosdate = 1 << 5 | 1; e->dostime = 0; return; }
    e->dostime = tm->tm_hour << 11 | tm->tm_min << 5 | tm->tm_sec / 2;
    e->dosdate = (tm->tm_year - 80) << 9 | (tm->tm_mon + 1) << 5 | tm->tm_mday;
}

/* UT: the modification time (Info-ZIP); ux: the owner; UX: the time again, the
   older field that macOS's ditto and Finder read (they ignore UT) */
static void extra(struct entry *e, int central)
{
    o16(0x5455); o16(5); obytes("\1", 1); o32(e->mtime);
    if (e->owner) { o16(0x7875); o16(11); obytes("\1\4", 2); o32(e->uid); obytes("\4", 1); o32(e->gid); }
    o16(0x5855); o16(central || !e->owner ? 8 : 12); o32(e->mtime); o32(e->mtime);
    if (!central && e->owner) { o16(e->uid); o16(e->gid); }
}

static unsigned extra_len(struct entry *e, int central)     /* (what extra writes) */
{
    return 9 + (e->owner ? 15 : 0) + (central || !e->owner ? 12 : 16);
}

/* an entry's name: the path less any leading / and ./ */
static const char *entry_name(const char *path)
{
    for (;;)
        if (path[0] == '/') path++;
        else if (path[0] == '.' && path[1] == '/') path += 2;
        else if (path[0] == '.' && path[1] == 0) path++;
        else return path;
}

static void local_header(struct entry *e)
{
    o32(0x04034B50);
    o16(e->needed); o16(e->flags); o16(e->method);
    o16(e->dostime); o16(e->dosdate);
    o32(e->crc); o32(e->csize); o32(e->usize);
    o16(strlen(e->name)); o16(extra_len(e, 0));
    obytes(e->name, strlen(e->name));
    extra(e, 0);
}

/* ---------------------------------------------------------------- adding a file */
static int src_fd;
static unsigned src_crc, src_size;

static int rd(void *v, unsigned char *p, unsigned n)
{
    int k = read(src_fd, p, n);
    (void)v;
    if (k > 0) { src_crc = crc32(src_crc, p, k); src_size += k; }
    return k;
}

static int wr(void *v, const unsigned char *p, unsigned n)
{
    (void)v;
    obytes(p, n);
    return 0;
}

static struct entry *find(const char *name)
{
    struct entry *e;
    for (e = entries; e; e = e->next)
        if (!strcmp(e->name, name)) return e;
    return 0;
}

static struct entry *new_entry(const char *name)
{
    struct entry *e = find(name);
    if (e) {                            /* an entry of the old archive: replaced */
        e->old = 0;
        return e;
    }
    if (!(e = calloc(1, sizeof *e))) die(name, "out of memory");
    strcpy(e->name, name);
    if (last_entry) last_entry->next = e; else entries = e;
    last_entry = e;
    return e;
}

static void add(const char *path);

static void add_dir(const char *path, struct stat *st)
{
    char name[NAMEMAX];                 /* the path with a / */
    const char *ename;
    struct entry *e;
    int n = strlen(path);
    if (n + 2 > NAMEMAX) { fprintf(stderr, "zip: %s: name too long\n", path); status = 1; return; }
    strcpy(name, path);
    if (n && name[n - 1] != '/') strcat(name, "/");
    ename = entry_name(name);
    if (ename[0]) {                     /* (not for . or /: only what is in them) */
        e = new_entry(ename);
        e->mtime = st->st_mtime; e->mode = st->st_mode; e->uid = st->st_uid; e->gid = st->st_gid;
        e->owner = 1;
        e->method = 0; e->needed = 20; e->crc = e->csize = e->usize = 0;
        e->made_by = 0x031E;
        e->ext_attr = (unsigned)st->st_mode << 16 | 0x10;
        dos_time(e);
        e->flags = 0;
        e->offset = opos;
        local_header(e);
        nadded++;
        if (!quiet) printf("  adding: %s\t(stored 0%%)\n", ename);
        fflush(stdout);                 /* (in order with any messages) */
    }
    if (recurse) {
        DIR *d = opendir(path);
        struct dirent *de;
        char *list = 0, *p;
        int len = 0, cap = 0;
        if (!d) { fprintf(stderr, "zip: %s: %s\n", path, strerror(errno)); status = 1; return; }
        while ((de = readdir(d)) != 0) {
            int k = strlen(de->d_name) + 1;
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            if (len + k > cap) {
                char *nl = realloc(list, cap = cap * 2 + 512);
                if (!nl) die(path, "out of memory");
                list = nl;
            }
            strcpy(list + len, de->d_name);
            len += k;
        }
        closedir(d);
        for (p = list; p && p < list + len; p += strlen(p) + 1) {
            char sub[NAMEMAX + 32];
            sprintf(sub, "%s%s", name, p);
            add(sub);
        }
        free(list);
    }
}

static void add(const char *path)
{
    struct stat st;
    struct entry *e;
    const char *name = entry_name(path);
    if (stat(path, &st) < 0) { fprintf(stderr, "zip: %s: %s\n", path, strerror(errno)); status = 1; return; }
    if ((st.st_dev == self_dev[0] && st.st_ino == self_ino[0]) ||
        (st.st_dev == self_dev[1] && st.st_ino == self_ino[1])) return;   /* (not the archive itself) */
    if (S_ISDIR(st.st_mode)) { add_dir(path, &st); return; }
    if (!S_ISREG(st.st_mode)) { fprintf(stderr, "zip: %s: not a regular file, skipped\n", path); status = 1; return; }
    if (strlen(name) >= NAMEMAX) { fprintf(stderr, "zip: %s: name too long\n", path); status = 1; return; }
    if ((src_fd = open(path, O_RDONLY)) < 0) { fprintf(stderr, "zip: %s: %s\n", path, strerror(errno)); status = 1; return; }
    e = new_entry(name);
    e->mtime = st.st_mtime; e->mode = st.st_mode; e->uid = st.st_uid; e->gid = st.st_gid;
    e->owner = 1;
    e->method = level && st.st_size ? 8 : 0;
    e->needed = e->method ? 20 : 10;
    e->flags = 0;
    e->made_by = 0x031E;                /* Unix, zip 3.0 */
    e->ext_attr = (unsigned)st.st_mode << 16;
    dos_time(e);
    e->offset = opos;
    e->crc = e->csize = e->usize = 0;
    local_header(e);                    /* (crc and sizes filled in below) */
    src_crc = src_size = 0;
    {
        unsigned start = opos;
        if (e->method == 8) {
            if (deflate_raw(rd, 0, wr, 0, level)) die(path, "read error");
        } else {
            unsigned char b[4096];
            int k;
            while ((k = rd(0, b, sizeof b)) > 0) obytes(b, k);
        }
        e->csize = opos - start;
    }
    close(src_fd);
    e->crc = src_crc;
    e->usize = src_size;
    oflush();                           /* the header's crc and sizes, now known */
    lseek(ofd, e->offset + 14, 0);
    {
        unsigned char b[12];
        b[0] = e->crc; b[1] = e->crc >> 8; b[2] = e->crc >> 16; b[3] = e->crc >> 24;
        b[4] = e->csize; b[5] = e->csize >> 8; b[6] = e->csize >> 16; b[7] = e->csize >> 24;
        b[8] = e->usize; b[9] = e->usize >> 8; b[10] = e->usize >> 16; b[11] = e->usize >> 24;
        if (write(ofd, b, 12) != 12) die(temp, strerror(errno));
    }
    lseek(ofd, opos, 0);
    nadded++;
    if (!quiet)
        printf("  adding: %s\t(%s %u%%)\n", name, e->method ? "deflated" : "stored",
               e->usize > e->csize && e->usize >= 100 ? (e->usize - e->csize) / (e->usize / 100) : 0);
    fflush(stdout);
}

/* ---------------------------------------------------------------- the old archive */
static void read_old(void)
{
    unsigned char tail[22 + 1024], h[46];
    int size, n, k, i;
    unsigned count, cdoff;
    if ((ifd_old = open(archive, O_RDONLY)) < 0) return;
    size = lseek(ifd_old, 0, 2);
    n = size < (int)sizeof tail ? size : (int)sizeof tail;
    lseek(ifd_old, size - n, 0);
    if (read(ifd_old, tail, n) != n) die(archive, "read error");
    for (k = n - 22; k >= 0; k--)
        if (g32(tail + k) == 0x06054B50) break;
    if (k < 0) die(archive, "not a zip archive (or a long comment)");
    count = g16(tail + k + 10);
    cdoff = g32(tail + k + 16);
    lseek(ifd_old, cdoff, 0);
    for (i = 0; i < (int)count; i++) {
        struct entry *e;
        unsigned nl, el, cl;
        if (read(ifd_old, h, 46) != 46 || g32(h) != 0x02014B50) die(archive, "damaged central directory");
        nl = g16(h + 28); el = g16(h + 30); cl = g16(h + 32);
        if (!(e = calloc(1, sizeof *e))) die(archive, "out of memory");
        if (nl >= NAMEMAX) die(archive, "an entry's name is too long");
        if (read(ifd_old, e->name, nl) != (int)nl) die(archive, "read error");
        e->name[nl] = 0;
        lseek(ifd_old, el + cl, 1);     /* (its extra fields: written again from what follows) */
        e->made_by = g16(h + 4); e->needed = g16(h + 6); e->flags = g16(h + 8) & ~8;
        e->method = g16(h + 10); e->dostime = g16(h + 12); e->dosdate = g16(h + 14);
        e->crc = g32(h + 16); e->csize = g32(h + 20); e->usize = g32(h + 24);
        e->ext_attr = g32(h + 38); e->src_offset = g32(h + 42);
        e->mode = e->ext_attr >> 16;
        e->mtime = 0;
        e->old = 1;
        if (last_entry) last_entry->next = e; else entries = e;
        last_entry = e;
    }
}

/* the old entries' times (UT) and owners (ux) from their local headers, then their data copied */
static void copy_old(struct entry *e)
{
    unsigned char h[30], x[256];
    unsigned nl, el, k, left;
    lseek(ifd_old, e->src_offset, 0);
    if (read(ifd_old, h, 30) != 30 || g32(h) != 0x04034B50) die(e->name, "damaged local header");
    nl = g16(h + 26); el = g16(h + 28);
    lseek(ifd_old, nl, 1);
    if (el <= sizeof x && read(ifd_old, x, el) == (int)el)
        for (k = 0; k + 4 <= el; k += 4 + g16(x + k + 2)) {
            unsigned id = g16(x + k), sz = g16(x + k + 2);
            if (id == 0x5455 && sz >= 5 && (x[k + 4] & 1)) e->mtime = g32(x + k + 5);
            if (id == 0x5855 && sz >= 8 && !e->mtime) e->mtime = g32(x + k + 8);
            if (id == 0x7875 && sz >= 11 && x[k + 5] == 4 && x[k + 10] == 4) {
                e->uid = g32(x + k + 6); e->gid = g32(x + k + 11); e->owner = 1;
            }
            if (id == 0x5855 && sz >= 12 && !e->owner) { e->uid = g16(x + k + 12); e->gid = g16(x + k + 14); e->owner = 1; }
        }
    else
        lseek(ifd_old, e->src_offset + 30 + nl + el, 0);
    if (!e->mtime) {                    /* only a DOS time: back to seconds */
        struct tm tm;
        memset(&tm, 0, sizeof tm);
        tm.tm_year = (e->dosdate >> 9) + 80; tm.tm_mon = ((e->dosdate >> 5) & 15) - 1; tm.tm_mday = e->dosdate & 31;
        tm.tm_hour = e->dostime >> 11; tm.tm_min = (e->dostime >> 5) & 63; tm.tm_sec = (e->dostime & 31) * 2;
        e->mtime = mktime(&tm);
    }
    e->offset = opos;
    local_header(e);
    for (left = e->csize; left > 0; ) {
        unsigned char b[4096];
        int n = read(ifd_old, b, left < sizeof b ? left : sizeof b);
        if (n <= 0) die(archive, "read error");
        obytes(b, n);
        left -= n;
    }
}

int main(int argc, char **argv)
{
    int i = 1, n;
    struct entry *e;
    unsigned cdstart, count = 0;
    char *slash;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char *o;
        for (o = argv[i] + 1; *o; o++)
            if (*o == 'r') recurse = 1;
            else if (*o == 'q') quiet = 1;
            else if (*o >= '0' && *o <= '9') level = *o - '0';
            else { fprintf(stderr, "usage: zip [-rq0..9] archive[.zip] file ...\n"); return 2; }
    }
    if (argc - i < 2) { fprintf(stderr, "usage: zip [-rq0..9] archive[.zip] file ...\n"); return 2; }
    if (strlen(argv[i]) >= NAMEMAX) die(argv[i], "name too long");
    strcpy(archive, argv[i++]);
    n = strlen(archive);
    if (n < 4 || strcmp(archive + n - 4, ".zip")) {     /* .zip added, unless a file has the name */
        struct stat st;
        if (stat(archive, &st) < 0 || S_ISDIR(st.st_mode)) strcat(archive, ".zip");
    }
    strcpy(temp, archive);              /* the new archive is made beside the old one, then renamed */
    slash = strrchr(temp, '/');
    sprintf(slash ? slash + 1 : temp, "zi%05d.tmp", getpid());
    read_old();
    if ((ofd = open(temp, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0) die(temp, strerror(errno));
    {
        struct stat st;
        if (ifd_old >= 0 && fstat(ifd_old, &st) == 0) {   /* the old archive's mode is kept */
            self_dev[0] = st.st_dev; self_ino[0] = st.st_ino;
            chmod(temp, st.st_mode & 07777);
        }
        if (fstat(ofd, &st) == 0) { self_dev[1] = st.st_dev; self_ino[1] = st.st_ino; }
    }
    for (; i < argc; i++)               /* the new files (replacing old entries of the same name) */
        add(argv[i]);
    for (e = entries; e; e = e->next)   /* then the old entries that remain, copied as they were */
        if (e->old) copy_old(e);
    cdstart = opos;
    for (e = entries; e; e = e->next) {
        o32(0x02014B50);
        o16(e->made_by); o16(e->needed); o16(e->flags); o16(e->method);
        o16(e->dostime); o16(e->dosdate);
        o32(e->crc); o32(e->csize); o32(e->usize);
        o16(strlen(e->name)); o16(extra_len(e, 1)); o16(0);
        o16(0); o16(0); o32(e->ext_attr); o32(e->offset);
        obytes(e->name, strlen(e->name));
        extra(e, 1);
        count++;
    }
    {
        unsigned cdsize = opos - cdstart;
        o32(0x06054B50); o16(0); o16(0); o16(count); o16(count);
        o32(cdsize); o32(cdstart); o16(0);
    }
    oflush();
    close(ofd);
    if (ifd_old >= 0) close(ifd_old);
    if (!count) {                       /* (no archive with no entries is made) */
        unlink(temp);
        fprintf(stderr, "zip: nothing added; %s not made\n", archive);
        return status;
    }
    if (rename(temp, archive) < 0) die(archive, strerror(errno));
    temp[0] = 0;
    if (!nadded) fprintf(stderr, "zip: nothing added\n");
    return status;
}
