/* dos -- FAT files on a USB drive (D43, D44): the drive as Windows, the Mac
 * and cameras write it, read and written from P32W Unix without a FAT
 * filesystem in the kernel.
 *
 *   dosdir    [-i image] [path ...]                list directories
 *   doscp     [-i image] [-rv] source ... target   copy to or from the drive
 *   dosrm     [-i image] [-r] path ...             remove files (-r: directories)
 *   dosmkdir  [-i image] path ...                  make directories
 *   dosformat [-i image] [-F 16|32] [-v label]     make an empty FAT drive
 *
 * One program; the name it is run by picks the command (as gzip and
 * gunzip). Paths on the drive start with "::" for doscp (::hello.c,
 * ::/docs, :: alone is the top); dosdir, dosrm and dosmkdir take only
 * drive paths, so the "::" is optional there. Names on the drive are
 * matched without regard to case, as on Windows and the Mac.
 *
 * The drive is /dev/rusb, read and written in whole sectors by number
 * (ioctl USBIOREAD, USBIOWRITE: a file offset stops at 2 GB, a stick does
 * not). -i names a disk image file instead (a copy of a stick, or one the
 * Mac made), read with lseek, so up to 2 GB.
 *
 * FAT in brief. A FAT volume is: a boot sector with the layout (the BPB),
 * reserved sectors, the File Allocation Table (two copies), the root
 * directory (FAT12/16 only: a fixed area), then the data area in clusters
 * of 1 to 128 sectors, numbered from 2. The FAT has one entry per cluster:
 * 0 free, else the next cluster of the same file, or end-of-chain. The
 * entries are 12, 16 or 28 bits wide (FAT12, 16, 32); which one is fixed by
 * the number of clusters alone. A directory is a file of 32-byte entries:
 * an 8.3 short name, attributes, times, the first cluster and the size.
 * Longer names ("VFAT") go in extra entries just before the short one, 13
 * UCS-2 characters each, tied to it by a checksum of its short name.
 * A stick usually has an MBR partition table, the FAT volume in its first
 * partition; some have the volume from sector 0 ("superfloppy"). Both are
 * read; dosformat writes the first kind. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <utime.h>
#include <dirent.h>
#include <sys/stat.h>
#ifdef __P32__
#include <sgtty.h>
#include <sys/usb.h>
#endif

#define SEC 512
#define UNKNOWN 0xFFFFFFFF
#define A_RO    0x01
#define A_HID   0x02
#define A_SYS   0x04
#define A_VOL   0x08
#define A_DIR   0x10
#define A_ARC   0x20
#define A_LFN   0x0F

static const char *prog;
static char *drive = "/dev/rusb";
static int dfd = -1, rawdev, status, recurse, verbose;
static unsigned dsize;                  /* the device, in sectors */

/* the volume (from its boot sector) */
static unsigned part;                   /* its first sector on the device */
static unsigned spc, rsvd, nfats, rootents, fatsz, totsec, rootclus, fsinfo;
static unsigned fatstart, rootstart, rootsecs, datastart, nclus, csize;
static unsigned rootdir;                /* the root as a directory: 0 (FAT12/16's fixed area) or rootclus */
static unsigned eoc;                    /* the end-of-chain mark: from here up */
static int ftype;                       /* 12, 16 or 32 */
static unsigned freecnt = UNKNOWN, nextfree = 2, fsi_free = UNKNOWN, fsi_next;
static char vlabel[12];
static unsigned char *cbuf;             /* a cluster */

static void die(const char *what, const char *why)
{
    fprintf(stderr, "%s: %s: %s\n", prog, what, why);
    exit(1);
}

static void warn(const char *what, const char *why)
{
    fprintf(stderr, "%s: %s: %s\n", prog, what, why);
    status = 1;
}

static unsigned g16(const unsigned char *b) { return b[0] | b[1] << 8; }
static unsigned g32(const unsigned char *b) { return b[0] | b[1] << 8 | b[2] << 16 | (unsigned)b[3] << 24; }
static void p16(unsigned char *b, unsigned v) { b[0] = v; b[1] = v >> 8; }
static void p32(unsigned char *b, unsigned v) { b[0] = v; b[1] = v >> 8; b[2] = v >> 16; b[3] = v >> 24; }

/* ---------------------------------------------------------------- sectors */
static void sectors(unsigned lba, unsigned n, void *buf, int wr)
{
    int r;
#ifdef __P32__
    if (rawdev) {
        struct usbio io;
        unsigned k, c;
        for (k = 0; k < n; k += c) {
            c = n - k > 128 ? 128 : n - k;
            io.lba = lba + k;
            io.count = c;
            io.buf = (char *)buf + k * SEC;
            if (ioctl(dfd, wr ? USBIOWRITE : USBIOREAD, &io) < 0) die(drive, strerror(errno));
        }
        return;
    }
#endif
    if (lba + n > dsize || lba + n < lba) die(drive, "past the end");
    if (lba >= 0x400000) die(drive, "an image file stops at 2 GB here");
    if (lseek(dfd, (off_t)lba * SEC, 0) < 0) die(drive, strerror(errno));
    r = wr ? write(dfd, buf, n * SEC) : read(dfd, buf, n * SEC);
    if (r != (int)(n * SEC)) die(drive, r < 0 ? strerror(errno) : "short transfer");
}

static void rdsec(unsigned lba, unsigned n, void *buf) { sectors(lba, n, buf, 0); }
static void wrsec(unsigned lba, unsigned n, void *buf) { sectors(lba, n, buf, 1); }

/* n zero sectors from lba */
static void zero(unsigned lba, unsigned n)
{
    static unsigned char z[16 * SEC];
    unsigned c;
    for (; n; n -= c, lba += c) {
        c = n > 16 ? 16 : n;
        wrsec(lba, c, z);
    }
}

static void opendev(int mode)
{
    struct stat st;
    if ((dfd = open(drive, mode)) < 0) {
        if (errno == ENXIO) die(drive, "no USB drive (is it plugged in?)");
        die(drive, strerror(errno));
    }
    if (fstat(dfd, &st) < 0) die(drive, strerror(errno));
#ifdef __P32__
    if (S_ISCHR(st.st_mode)) {
        rawdev = 1;
        if (ioctl(dfd, USBIOSIZE, &dsize) < 0) die(drive, "not a USB drive");
        return;
    }
#endif
    rawdev = 0;
    if (!S_ISREG(st.st_mode)) die(drive, "not a USB drive or an image file");
    dsize = st.st_size / SEC;
}

/* ---------------------------------------------------------------- the volume */
static int isbpb(const unsigned char *b)
{
    unsigned s = b[13];
    return (b[0] == 0xEB || b[0] == 0xE9) && g16(b + 11) == SEC && s && !(s & (s - 1)) &&
           g16(b + 14) && (b[16] == 1 || b[16] == 2) && (g16(b + 19) || g32(b + 32)) &&
           (g16(b + 22) || g32(b + 36));
}

static void openvol(void)
{
    unsigned char b[SEC];
    unsigned k;
    rdsec(0, 1, b);
    part = 0;
    if (!isbpb(b)) {                    /* an MBR? its first FAT partition */
        if (b[510] != 0x55 || b[511] != 0xAA) die(drive, "no FAT filesystem (dosformat makes one)");
        for (k = 0; k < 4; k++) {
            unsigned char *p = b + 446 + 16 * k;
            int t = p[4];
            if (t == 0x01 || t == 0x04 || t == 0x06 || t == 0x0B || t == 0x0C || t == 0x0E) { part = g32(p + 8); break; }
        }
        if (k == 4) die(drive, "no FAT partition (dosformat makes one)");
        rdsec(part, 1, b);
        if (!isbpb(b)) die(drive, "the FAT partition has no FAT filesystem");
    }
    spc = b[13];
    rsvd = g16(b + 14);
    nfats = b[16];
    rootents = g16(b + 17);
    totsec = g16(b + 19) ? g16(b + 19) : g32(b + 32);
    fatsz = g16(b + 22) ? g16(b + 22) : g32(b + 36);
    rootsecs = (rootents * 32 + SEC - 1) / SEC;
    fatstart = part + rsvd;
    rootstart = fatstart + nfats * fatsz;
    datastart = rootstart + rootsecs;
    if (rsvd + nfats * fatsz + rootsecs >= totsec) die(drive, "a damaged FAT boot sector");
    nclus = (totsec - rsvd - nfats * fatsz - rootsecs) / spc;
    csize = spc * SEC;
    ftype = nclus < 4085 ? 12 : nclus < 65525 ? 16 : 32;
    eoc = ftype == 12 ? 0xFF8 : ftype == 16 ? 0xFFF8 : 0x0FFFFFF8;
    memcpy(vlabel, b + (ftype == 32 ? 71 : 43), 11);
    if (ftype == 32) {
        rootclus = g32(b + 44);
        rootdir = rootclus;
        fsinfo = g16(b + 48);
        if (fsinfo && fsinfo < rsvd) {
            rdsec(part + fsinfo, 1, b);
            if (g32(b) == 0x41615252 && g32(b + 484) == 0x61417272) {
                fsi_free = g32(b + 488);
                fsi_next = g32(b + 492);
                if (fsi_free <= nclus) freecnt = fsi_free;
                if (fsi_next >= 2 && fsi_next < nclus + 2) nextfree = fsi_next;
            }
        }
    } else
        rootdir = 0;
    if (part + totsec > dsize) die(drive, "the filesystem is bigger than the drive");
    if (!(cbuf = malloc(csize))) die(drive, "no memory for a cluster");
}

/* ---------------------------------------------------------------- the FAT, through a small cache */
#define NFC 8
static struct fc { unsigned sec, used; int dirty; unsigned char d[SEC]; } fc[NFC];
static unsigned fcclock;

static unsigned char *fatbyte(unsigned off, int dirty)
{
    unsigned s = off / SEC, k;
    struct fc *f = 0;
    for (k = 0; k < NFC; k++)
        if (fc[k].used && fc[k].sec == s) { f = &fc[k]; break; }
    if (!f) {
        f = &fc[0];
        for (k = 0; k < NFC; k++) {
            if (!fc[k].used) { f = &fc[k]; break; }
            if (fc[k].used < f->used) f = &fc[k];
        }
        if (f->used && f->dirty)
            for (k = 0; k < nfats; k++) wrsec(fatstart + k * fatsz + f->sec, 1, f->d);
        rdsec(fatstart + s, 1, f->d);
        f->sec = s;
        f->dirty = 0;
    }
    f->used = ++fcclock;
    if (dirty) f->dirty = 1;
    return f->d + off % SEC;
}

static unsigned fat(unsigned c)
{
    unsigned v;
    if (ftype == 12) {
        v = *fatbyte(c + c / 2, 0) | *fatbyte(c + c / 2 + 1, 0) << 8;
        return c & 1 ? v >> 4 : v & 0xFFF;
    }
    if (ftype == 16) return g16(fatbyte(2 * c, 0));
    return g32(fatbyte(4 * c, 0)) & 0x0FFFFFFF;
}

static void setfat(unsigned c, unsigned v)
{
    unsigned char *p;
    if (ftype == 12) {
        p = fatbyte(c + c / 2, 1);
        *p = c & 1 ? (*p & 0x0F) | (v << 4 & 0xF0) : v;
        p = fatbyte(c + c / 2 + 1, 1);
        *p = c & 1 ? v >> 4 : (*p & 0xF0) | (v >> 8 & 0x0F);
    } else if (ftype == 16)
        p16(fatbyte(2 * c, 1), v);
    else {
        p = fatbyte(4 * c, 1);
        p32(p, (g32(p) & 0xF0000000) | (v & 0x0FFFFFFF));
    }
}

static int valid(unsigned c) { return c >= 2 && c < nclus + 2; }
static unsigned clsec(unsigned c) { return datastart + (c - 2) * spc; }

static void countfree(void)
{
    unsigned c;
    if (freecnt != UNKNOWN) return;
    freecnt = 0;
    for (c = 2; c < nclus + 2; c++)
        if (fat(c) == 0) freecnt++;
}

/* a free cluster, marked end-of-chain and linked after prev (if any): 0 if the drive is full */
static unsigned alloc(unsigned prev)
{
    unsigned k, c = nextfree;
    if (freecnt == 0) return 0;
    for (k = 0; k < nclus; k++, c++) {
        if (!valid(c)) c = 2;
        if (fat(c) == 0) {
            setfat(c, 0x0FFFFFFF);
            if (prev) setfat(prev, c);
            if (freecnt != UNKNOWN) freecnt--;
            nextfree = c + 1;
            return c;
        }
    }
    freecnt = 0;
    return 0;
}

static void dflush(void);
static unsigned dsec, wdir;

static void freechain(unsigned c)
{
    unsigned n, k;
    dflush();                           /* (a directory's sectors may be cached: they become free) */
    dsec = wdir = 0;
    for (k = 0; valid(c) && k < nclus; k++) {
        n = fat(c);
        if (n == 0) break;              /* (already free: a damaged chain) */
        setfat(c, 0);
        if (freecnt != UNKNOWN) freecnt++;
        c = n;
    }
}

/* ---------------------------------------------------------------- directories, entry by entry */
static unsigned char dbuf[SEC];
static int ddirty;                       /* (dsec: the sector in dbuf, 0 for none) */
static unsigned wk, wclus;              /* (with wdir: the last cluster found in a chain: directory, index, cluster) */

static void dflush(void)
{
    if (dsec && ddirty) wrsec(dsec, 1, dbuf);
    ddirty = 0;
}

/* entry i of directory d (a first cluster, or 0: the fixed root), in dbuf;
   past the end, 0, or with grow a new cluster (0 if the drive is full) */
static unsigned char *ent(unsigned d, unsigned i, int grow)
{
    unsigned s, k, c, n;
    if (d == 0) {
        if (i >= rootents) return 0;
        s = rootstart + i / 16;
    } else {
        k = i / (16 * spc);
        c = d; n = 0;
        if (wdir == d && wk <= k) { c = wclus; n = wk; }
        for (; n < k; n++) {
            unsigned x = fat(c);
            if (!valid(x)) {
                if (!grow || x < eoc) return 0;
                if (!(x = alloc(c))) return 0;
                dflush();
                dsec = 0;
                zero(clsec(x), spc);
            }
            c = x;
        }
        wdir = d; wk = k; wclus = c;
        s = clsec(c) + i / 16 % spc;
    }
    if (s != dsec) {
        dflush();
        rdsec(s, 1, dbuf);
        dsec = s;
    }
    return dbuf + i % 16 * 32;
}

struct dent {
    char name[768];                     /* the long name (UTF-8), else the short one */
    char sname[13];                     /* the short name, as NAME.EXT */
    unsigned char e[32];                /* the short entry */
    unsigned idx, first;                /* its index; the first of its long-name entries */
};

static unsigned eclus(const unsigned char *e) { return g16(e + 26) | (ftype == 32 ? g16(e + 20) << 16 : 0); }

/* a directory entry's cluster as a directory (".." of a top-level directory says 0) */
static unsigned dclus(const unsigned char *e) { unsigned c = eclus(e); return c ? c : rootdir; }

static unsigned char lfnsum(const unsigned char *e)
{
    unsigned char s = 0;
    int k;
    for (k = 0; k < 11; k++) s = ((s & 1) << 7) + (s >> 1) + e[k];
    return s;
}

static void shortname(const unsigned char *e, char *out)
{
    int k, n = 0;
    for (k = 0; k < 8 && e[k] != ' '; k++) {
        int c = k == 0 && e[k] == 0x05 ? 0xE5 : e[k];
        if (c >= 'A' && c <= 'Z' && (e[12] & 0x08)) c += 32;
        out[n++] = c >= 0x20 && c < 0x7F ? c : '?';
    }
    if (e[8] != ' ') {
        out[n++] = '.';
        for (k = 8; k < 11 && e[k] != ' '; k++) {
            int c = e[k];
            if (c >= 'A' && c <= 'Z' && (e[12] & 0x10)) c += 32;
            out[n++] = c >= 0x20 && c < 0x7F ? c : '?';
        }
    }
    out[n] = 0;
}

static int utf8(char *p, unsigned u)
{
    if (u < 0x80) { p[0] = u; return 1; }
    if (u < 0x800) { p[0] = 0xC0 | u >> 6; p[1] = 0x80 | (u & 0x3F); return 2; }
    p[0] = 0xE0 | u >> 12; p[1] = 0x80 | (u >> 6 & 0x3F); p[2] = 0x80 | (u & 0x3F);
    return 3;
}

/* the next name in directory d from *pos (skipping deleted entries and the
   volume label): 1, or 0 at the end */
static int nextent(unsigned d, unsigned *pos, struct dent *de)
{
    static const unsigned char at[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
    unsigned short u[260];
    int want = 0, sum = -1, k, j, n;
    unsigned char *e;
    for (;; (*pos)++) {
        if (!(e = ent(d, *pos, 0)) || e[0] == 0) return 0;
        if (e[0] == 0xE5) { want = 0; sum = -1; continue; }
        if (e[11] == A_LFN) {
            int ord = e[0] & 0x3F;
            if (e[0] & 0x40) { want = ord; sum = e[13]; de->first = *pos; memset(u, 0, sizeof u); }
            if (ord == 0 || ord > 20 || ord != want || e[13] != sum) { want = 0; sum = -1; continue; }
            for (k = 0; k < 13; k++) u[(ord - 1) * 13 + k] = g16(e + at[k]);
            want--;
            continue;
        }
        if (e[11] & A_VOL) { want = 0; sum = -1; continue; }
        memcpy(de->e, e, 32);
        de->idx = *pos;
        shortname(e, de->sname);
        if (want == 0 && sum == lfnsum(e)) {
            for (j = n = 0; j < 260 && u[j] && u[j] != 0xFFFF; j++) n += utf8(de->name + n, u[j]);
            de->name[n] = 0;
        } else {
            strcpy(de->name, de->sname);
            de->first = *pos;
        }
        (*pos)++;
        return 1;
    }
}

static int same(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int x = *a, y = *b;
        if (x >= 'a' && x <= 'z') x -= 32;
        if (y >= 'a' && y <= 'z') y -= 32;
        if (x != y) return 0;
    }
    return *a == *b;
}

static int lookup(unsigned d, const char *name, struct dent *de)
{
    unsigned pos = 0;
    while (nextent(d, &pos, de))
        if (same(de->name, name) || same(de->sname, name)) return 1;
    return 0;
}

/* a drive path: 1 with de and *dir (the directory holding it) when it is
   there, 0 with *dir and *last when only its last name is not; the top is
   found as a directory with idx UNKNOWN */
static int namei(const char *path, struct dent *de, unsigned *dir, char *last)
{
    char comp[256];
    const char *p = path;
    unsigned d = rootdir;
    int k, isdir = 1;
    if (!strncmp(p, "::", 2)) p += 2;
    memset(de, 0, sizeof *de);
    de->e[11] = A_DIR;
    de->idx = UNKNOWN;
    strcpy(de->name, "/");
    *dir = rootdir;
    for (;;) {
        while (*p == '/') p++;
        if (!*p) return 1;
        for (k = 0; *p && *p != '/'; p++)
            if (k < 255) comp[k++] = *p;
        comp[k] = 0;
        if (!isdir) die(path, "not a directory");
        if (de->idx != UNKNOWN) d = dclus(de->e);
        if (d == rootdir && (!strcmp(comp, ".") || !strcmp(comp, ".."))) continue;
        *dir = d;
        if (!lookup(d, comp, de)) {
            while (*p == '/') p++;
            if (*p) die(path, "no such directory");
            if (last) strcpy(last, comp);
            return 0;
        }
        isdir = de->e[11] & A_DIR;
        if (de->idx != UNKNOWN && dclus(de->e) == rootdir && (de->e[11] & A_DIR)) {    /* (.. back to the top) */
            de->idx = UNKNOWN;
            strcpy(de->name, "/");
            *dir = rootdir;
        }
    }
}

/* ---------------------------------------------------------------- making entries */
static void dostime(time_t t, unsigned *date, unsigned *tm)
{
    struct tm *x = localtime(&t);
    if (!x || x->tm_year < 80) { *date = 1 << 5 | 1; *tm = 0; return; }
    *date = (x->tm_year - 80) << 9 | (x->tm_mon + 1) << 5 | x->tm_mday;
    *tm = x->tm_hour << 11 | x->tm_min << 5 | x->tm_sec / 2;
}

static time_t unixtime(const unsigned char *e)
{
    struct tm x;
    unsigned d = g16(e + 24), t = g16(e + 22);
    memset(&x, 0, sizeof x);
    x.tm_year = (d >> 9) + 80;
    x.tm_mon = (d >> 5 & 15) - 1;
    x.tm_mday = d & 31;
    x.tm_hour = t >> 11;
    x.tm_min = t >> 5 & 63;
    x.tm_sec = (t & 31) * 2;
    x.tm_isdst = -1;
    return mktime(&x);
}

static int shortok(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
           (c && strchr("!#$%&'()-@^_`{}~", c));
}

/* name as an 8.3 short name that loses nothing (out: 11 bytes, *nt: the
   lower-case flags), or 0 if it needs a long name */
static int fits83(const char *name, unsigned char *out, int *nt)
{
    const char *dot = strchr(name, '.');
    int nb = dot ? dot - name : strlen(name), ne = dot ? strlen(dot + 1) : 0, k, up, lo;
    if (nb < 1 || nb > 8 || ne > 3 || (dot && (ne == 0 || strchr(dot + 1, '.')))) return 0;
    memset(out, ' ', 11);
    *nt = 0;
    up = lo = 0;
    for (k = 0; k < nb; k++) {
        int c = (unsigned char)name[k];
        if (!shortok(c)) return 0;
        if (c >= 'a' && c <= 'z') { lo = 1; c -= 32; } else if (c >= 'A' && c <= 'Z') up = 1;
        out[k] = c;
    }
    if (lo && up) return 0;
    if (lo) *nt |= 0x08;
    up = lo = 0;
    for (k = 0; k < ne; k++) {
        int c = (unsigned char)dot[1 + k];
        if (!shortok(c)) return 0;
        if (c >= 'a' && c <= 'z') { lo = 1; c -= 32; } else if (c >= 'A' && c <= 'Z') up = 1;
        out[8 + k] = c;
    }
    if (lo && up) return 0;
    if (lo) *nt |= 0x10;
    if (out[0] == 0xE5) out[0] = 0x05;
    return 1;
}

static int shortused(unsigned d, const unsigned char *s)
{
    struct dent de;
    unsigned pos = 0;
    while (nextent(d, &pos, &de))
        if (!memcmp(de.e, s, 11)) return 1;
    return 0;
}

/* a short name for a long one: BASIS~N.EXT, unused in directory d */
static void mkshort(unsigned d, const char *name, unsigned char *out)
{
    char base[9], ext[4], tail[8];
    const char *dot = strrchr(name, '.'), *p;
    int nb = 0, ne = 0, k;
    unsigned n;
    while (*name == '.') name++;
    if (dot && dot < name) dot = 0;
    for (p = name; *p && p != dot && nb < 8; p++) {
        int c = (unsigned char)*p;
        if (c == ' ' || c == '.') continue;
        if (c >= 'a' && c <= 'z') c -= 32;
        base[nb++] = shortok(c) ? c : '_';
    }
    if (dot)
        for (p = dot + 1; *p && ne < 3; p++) {
            int c = (unsigned char)*p;
            if (c == ' ') continue;
            if (c >= 'a' && c <= 'z') c -= 32;
            ext[ne++] = shortok(c) ? c : '_';
        }
    if (nb == 0) base[nb++] = '_';
    for (n = 1; n < 1000000; n++) {
        int t = sprintf(tail, "~%u", n), b = nb > 8 - t ? 8 - t : nb;
        memset(out, ' ', 11);
        memcpy(out, base, b);
        memcpy(out + b, tail, t);
        for (k = 0; k < ne; k++) out[8 + k] = ext[k];
        if (!shortused(d, out)) return;
    }
    die(name, "no short name left in the directory");
}

/* name in UCS-2 (from UTF-8): its length, or -1 if it cannot be a FAT name */
static int ucs2(const char *name, unsigned short *u)
{
    const unsigned char *p = (const unsigned char *)name;
    int n = 0;
    while (*p) {
        unsigned c = *p++;
        if (c >= 0xE0 && (p[0] & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80) { c = (c & 15) << 12 | (p[0] & 63) << 6 | (p[1] & 63); p += 2; }
        else if (c >= 0xC0 && (p[0] & 0xC0) == 0x80) { c = (c & 31) << 6 | (p[0] & 63); p++; }
        if (c < 0x20 || (c < 0x80 && strchr("\\/:*?\"<>|", c)) || n == 255) return -1;
        u[n++] = c;
    }
    if (n == 0 || u[n - 1] == '.' || u[n - 1] == ' ') return -1;
    return n;
}

/* a new entry in directory d: 1, or 0 (with a message) */
static int create(unsigned d, const char *name, int attr, unsigned clus, unsigned size, time_t mt)
{
    static const unsigned char at[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
    unsigned short u[256];
    unsigned char s[11], *e;
    unsigned i, run, start, date, tm;
    int len, nl, nt, k, j;
    if ((len = ucs2(name, u)) < 0) { warn(name, "not a name a FAT drive can hold (none of \\/:*?\"<>|, and no . or space last)"); return 0; }
    nl = 0;
    if (!fits83(name, s, &nt)) {
        mkshort(d, name, s);
        nl = (len + 12) / 13;
        nt = 0;
    }
    for (i = run = start = 0; run < (unsigned)nl + 1; i++) {     /* nl + 1 free entries in a row */
        if (!(e = ent(d, i, 1))) {
            warn(name, d == 0 ? "the top directory is full" : "the drive is full");
            return 0;
        }
        if (e[0] == 0 || e[0] == 0xE5) { if (run++ == 0) start = i; }
        else run = 0;
    }
    for (k = nl; k >= 1; k--) {         /* the long name, last part first */
        e = ent(d, start + nl - k, 0);
        memset(e, 0, 32);
        e[0] = k | (k == nl ? 0x40 : 0);
        e[11] = A_LFN;
        e[13] = lfnsum(s);
        for (j = 0; j < 13; j++) {
            int c = (k - 1) * 13 + j;
            p16(e + at[j], c < len ? u[c] : c == len ? 0 : 0xFFFF);
        }
        ddirty = 1;
    }
    e = ent(d, start + nl, 0);
    memset(e, 0, 32);
    memcpy(e, s, 11);
    e[11] = attr;
    e[12] = nt;
    dostime(mt, &date, &tm);
    p16(e + 14, tm); p16(e + 16, date); p16(e + 18, date);     /* created, last opened */
    p16(e + 22, tm); p16(e + 24, date);                         /* written */
    p16(e + 20, clus >> 16);
    p16(e + 26, clus);
    p32(e + 28, size);
    ddirty = 1;
    return 1;
}

/* the entries of de (its long name and short entry) in directory d marked deleted */
static void unlink_ent(unsigned d, const struct dent *de)
{
    unsigned i;
    for (i = de->first; i <= de->idx; i++) {
        ent(d, i, 0)[0] = 0xE5;
        ddirty = 1;
    }
}

/* ---------------------------------------------------------------- the commands' parts */
static void finish(void)                /* everything written back, and FSInfo brought up to date */
{
    unsigned k, j;
    unsigned char b[SEC];
    dflush();
    for (k = 0; k < NFC; k++)
        if (fc[k].used && fc[k].dirty) {
            for (j = 0; j < nfats; j++) wrsec(fatstart + j * fatsz + fc[k].sec, 1, fc[k].d);
            fc[k].dirty = 0;
        }
    if (ftype == 32 && fsinfo && fsinfo < rsvd && (freecnt != fsi_free || nextfree != fsi_next)) {
        rdsec(part + fsinfo, 1, b);
        if (g32(b) == 0x41615252 && g32(b + 484) == 0x61417272) {
            p32(b + 488, freecnt);
            p32(b + 492, nextfree);
            wrsec(part + fsinfo, 1, b);
        }
    }
}

/* a drive path for messages: ::name */
static char *dpath(const char *dir, const char *name)
{
    static char b[1024];
    if (!strncmp(dir, "::", 2)) dir += 2;
    while (*dir == '/') dir++;
    snprintf(b, sizeof b, "::%s%s%s", dir, *dir && name ? "/" : "", name ? name : "");
    return b;
}

static char *join(const char *a, const char *b)
{
    char *p = malloc(strlen(a) + strlen(b) + 2);
    if (!p) die(a, "out of memory");
    sprintf(p, "%s%s%s", a, *a && a[strlen(a) - 1] != '/' ? "/" : "", b);
    return p;
}

static const char *base(const char *path)
{
    const char *p;
    size_t n = strlen(path);
    static char b[256];
    while (n > 1 && path[n - 1] == '/') n--;
    if (n >= sizeof b) n = sizeof b - 1;
    memcpy(b, path, n);
    b[n] = 0;
    p = strrchr(b, '/');
    p = p ? p + 1 : b;
    if (!strncmp(p, "::", 2)) p += 2;
    return p;
}

/* ---------------------------------------------------------------- dosdir */
static void listline(const struct dent *de)
{
    unsigned d = g16(de->e + 24), t = g16(de->e + 22);
    char sz[16];
    if (de->e[11] & A_DIR) strcpy(sz, "<DIR>");
    else sprintf(sz, "%u", g32(de->e + 28));
    printf("%10s  %04u-%02u-%02u %02u:%02u  %s\n", sz, (d >> 9) + 1980, d >> 5 & 15, d & 31, t >> 11, t >> 5 & 63, de->name);
}

static void dosdir(int argc, char **argv)
{
    struct dent de, x;
    unsigned dir, pos, nf, nd, bytes, k;
    char lab[12];
    int i, any = 0;
    opendev(O_RDONLY);
    openvol();
    countfree();
    memcpy(lab, vlabel, 11);            /* the label: the top directory's, else the boot sector's */
    for (pos = 0; ; pos++) {
        unsigned char *e = ent(rootdir, pos, 0);
        if (!e || e[0] == 0) break;
        if (e[0] != 0xE5 && (e[11] & A_VOL) && e[11] != A_LFN) { memcpy(lab, e, 11); break; }
    }
    for (k = 11; k > 0 && lab[k - 1] == ' '; k--) ;
    lab[k] = 0;
    if (!strcmp(lab, "NO NAME")) lab[0] = 0;
    printf("FAT%d volume%s%s, %u KiB, %u KiB free\n", ftype, lab[0] ? " " : "", lab,
           nclus / 2 * spc + nclus % 2 * spc / 2, freecnt / 2 * spc + freecnt % 2 * spc / 2);
    for (i = 0; i < argc || !any; i++) {
        const char *path = i < argc ? argv[i] : "::";
        any = 1;
        if (!namei(path, &de, &dir, 0)) { warn(dpath(path, 0), "not there"); continue; }
        if (!(de.e[11] & A_DIR)) { listline(&de); continue; }
        printf("\nDirectory %s\n", dpath(path, 0));
        nf = nd = bytes = 0;
        dir = de.idx == UNKNOWN ? rootdir : dclus(de.e);
        for (pos = 0; nextent(dir, &pos, &x); ) {
            if (!strcmp(x.name, ".") || !strcmp(x.name, "..")) continue;
            listline(&x);
            if (x.e[11] & A_DIR) nd++;
            else { nf++; bytes += g32(x.e + 28); }
        }
        printf("%u file%s, %u director%s, %u bytes\n", nf, nf == 1 ? "" : "s", nd, nd == 1 ? "y" : "ies", bytes);
    }
}

/* ---------------------------------------------------------------- removing */
static void rmtree(unsigned d, struct dent *de, const char *path)
{
    struct dent x;
    unsigned pos, c;
    if (de->e[11] & A_DIR) {
        c = dclus(de->e);
        for (pos = 0; nextent(c, &pos, &x); ) {
            char *p;
            if (!strcmp(x.name, ".") || !strcmp(x.name, "..")) continue;
            p = join(path, x.name);
            rmtree(c, &x, p);
            free(p);
        }
        if (verbose) printf("removed directory %s\n", dpath(path, 0));
    } else if (verbose) printf("removed %s\n", dpath(path, 0));
    freechain(eclus(de->e));
    unlink_ent(d, de);
}

static int isempty(unsigned c)
{
    struct dent x;
    unsigned pos = 0;
    while (nextent(c, &pos, &x))
        if (strcmp(x.name, ".") && strcmp(x.name, "..")) return 0;
    return 1;
}

static void dosrm(int argc, char **argv)
{
    struct dent de;
    unsigned dir;
    int i;
    opendev(O_RDWR);
    openvol();
    for (i = 0; i < argc; i++) {
        if (!namei(argv[i], &de, &dir, 0)) { warn(dpath(argv[i], 0), "not there"); continue; }
        if (de.idx == UNKNOWN) { warn(dpath(argv[i], 0), "the top directory cannot be removed"); continue; }
        if ((de.e[11] & A_DIR) && !recurse && !isempty(dclus(de.e))) { warn(dpath(argv[i], 0), "a directory, not empty (dosrm -r removes it all)"); continue; }
        if (!strcmp(de.name, ".") || !strcmp(de.name, "..")) { warn(dpath(argv[i], 0), "cannot be removed"); continue; }
        rmtree(dir, &de, argv[i]);
    }
    finish();
}

/* ---------------------------------------------------------------- making directories */
/* a new directory name in d: its cluster, or 0 (with a message) */
static unsigned mkdir_in(unsigned d, const char *name, time_t mt)
{
    unsigned c, date, tm;
    unsigned char *e = cbuf;
    if (!(c = alloc(0))) { warn(name, "the drive is full"); return 0; }
    memset(cbuf, 0, csize);
    dostime(mt, &date, &tm);
    memset(e, ' ', 11); e[0] = '.';
    memset(e + 32, ' ', 11); e[32] = e[33] = '.';
    for (; e < cbuf + 64; e += 32) {
        e[11] = A_DIR;
        p16(e + 14, tm); p16(e + 16, date); p16(e + 18, date); p16(e + 22, tm); p16(e + 24, date);
    }
    p16(cbuf + 20, c >> 16); p16(cbuf + 26, c);
    if (d != rootdir) { p16(cbuf + 32 + 20, d >> 16); p16(cbuf + 32 + 26, d); }    /* (the top: 0) */
    wrsec(clsec(c), spc, cbuf);
    if (!create(d, name, A_DIR, c, 0, mt)) { freechain(c); return 0; }
    return c;
}

static void dosmkdir(int argc, char **argv)
{
    struct dent de;
    unsigned dir;
    char last[256];
    int i;
    opendev(O_RDWR);
    openvol();
    for (i = 0; i < argc; i++) {
        if (namei(argv[i], &de, &dir, last)) { warn(dpath(argv[i], 0), "already there"); continue; }
        mkdir_in(dir, last, time(0));
    }
    finish();
}

/* ---------------------------------------------------------------- doscp, to the drive */
static int readfull(int fd, unsigned char *b, unsigned n)
{
    unsigned k = 0;
    int r;
    while (k < n) {
        if ((r = read(fd, b + k, n - k)) < 0) return -1;
        if (r == 0) break;
        k += r;
    }
    return k;
}

/* the P32W file or directory src as name in drive directory d */
static void put(const char *src, unsigned d, const char *name, const char *shown)
{
    struct stat st;
    struct dent old;
    int fd, n;
    unsigned first = 0, last = 0, size = 0, c;
    if (stat(src, &st) < 0) { warn(src, strerror(errno)); return; }
    if (S_ISDIR(st.st_mode)) {
        DIR *dp;
        struct dirent *x;
        if (!recurse) { warn(src, "a directory (doscp -r copies it)"); return; }
        if (lookup(d, name, &old)) {
            if (!(old.e[11] & A_DIR)) { warn(shown, "a file is there"); return; }
            c = dclus(old.e);
        } else if (!(c = mkdir_in(d, name, st.st_mtime))) return;
        if (verbose) printf("%s -> %s/\n", src, shown);
        if (!(dp = opendir(src))) { warn(src, strerror(errno)); return; }
        while ((x = readdir(dp))) {
            char *s, *t;
            if (!strcmp(x->d_name, ".") || !strcmp(x->d_name, "..")) continue;
            s = join(src, x->d_name);
            t = join(shown, x->d_name);
            put(s, c, x->d_name, t);
            free(s);
            free(t);
        }
        closedir(dp);
        return;
    }
    if (!S_ISREG(st.st_mode)) { warn(src, "not a file or a directory"); return; }
    if ((fd = open(src, O_RDONLY)) < 0) { warn(src, strerror(errno)); return; }
    while ((n = readfull(fd, cbuf, csize)) > 0) {
        if (!(c = alloc(last))) {
            warn(shown, "the drive is full");
            freechain(first);
            close(fd);
            return;
        }
        if (!first) first = c;
        last = c;
        if ((unsigned)n < csize) memset(cbuf + n, 0, csize - n);
        wrsec(clsec(c), (n + SEC - 1) / SEC, cbuf);
        size += n;
        if ((unsigned)n < csize) break;
    }
    if (n < 0) { warn(src, strerror(errno)); freechain(first); close(fd); return; }
    close(fd);
    if (lookup(d, name, &old)) {        /* replace a file of that name (not a directory) */
        if (old.e[11] & A_DIR) { warn(shown, "a directory is there"); freechain(first); return; }
        freechain(eclus(old.e));
        unlink_ent(d, &old);
    }
    if (!create(d, name, A_ARC, first, size, st.st_mtime)) { freechain(first); return; }
    if (verbose) printf("%s -> %s\n", src, shown);
}

/* ---------------------------------------------------------------- doscp, from the drive */
/* dst with its last name replaced by de's short name (P32W's names stop at
   28 characters; FAT's long ones go to 255): a new string */
static char *shortdst(const char *dst, const struct dent *de)
{
    char *p = malloc(strlen(dst) + 14), *q;
    if (!p) die(dst, "out of memory");
    strcpy(p, dst);
    q = strrchr(p, '/');
    strcpy(q ? q + 1 : p, de->sname);
    fprintf(stderr, "%s: %s: the name is too long for P32W: copied as %s\n", prog, dst, de->sname);
    return p;
}

static void get(struct dent *de, const char *dst, const char *shown)
{
    struct utimbuf ut;
    unsigned c, left, k, n;
    int fd;
    if (de->e[11] & A_DIR) {
        struct dent x;
        unsigned pos, d = dclus(de->e);
        struct stat st;
        if (!recurse) { warn(shown, "a directory (doscp -r copies it)"); return; }
        if (stat(dst, &st) < 0) {
            if (mkdir(dst, 0777) < 0) {
                if (errno == ENAMETOOLONG && strcmp(de->name, de->sname)) {
                    char *t = shortdst(dst, de);
                    get(de, t, shown);
                    free(t);
                } else warn(dst, strerror(errno));
                return;
            }
        } else if (!S_ISDIR(st.st_mode)) { warn(dst, "not a directory"); return; }
        if (verbose) printf("%s/ -> %s\n", shown, dst);
        for (pos = 0; nextent(d, &pos, &x); ) {
            char *s, *t;
            if (!strcmp(x.name, ".") || !strcmp(x.name, "..")) continue;
            s = join(dst, x.name);
            t = join(shown, x.name);
            get(&x, s, t);
            free(s);
            free(t);
        }
        ut.actime = ut.modtime = unixtime(de->e);
        if (de->idx != UNKNOWN) utime(dst, &ut);
        return;
    }
    if ((fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0) {
        if (errno == ENAMETOOLONG && strcmp(de->name, de->sname)) {
            char *t = shortdst(dst, de);
            get(de, t, shown);
            free(t);
        } else warn(dst, strerror(errno));
        return;
    }
    c = eclus(de->e);
    for (left = g32(de->e + 28); left; left -= n) {
        if (!valid(c)) { warn(shown, "damaged on the drive (its clusters end early)"); break; }
        n = left < csize ? left : csize;
        rdsec(clsec(c), (n + SEC - 1) / SEC, cbuf);
        for (k = 0; k < n; ) {
            int r = write(fd, cbuf + k, n - k);
            if (r <= 0) { warn(dst, r < 0 ? strerror(errno) : "write failed"); close(fd); return; }
            k += r;
        }
        c = fat(c);
    }
    close(fd);
    ut.actime = ut.modtime = unixtime(de->e);
    utime(dst, &ut);
    if (verbose) printf("%s -> %s\n", shown, dst);
}

static void doscp(int argc, char **argv)
{
    const char *target = argv[argc - 1];
    struct dent de;
    unsigned dir;
    char last[256];
    int i, todrive = !strncmp(target, "::", 2);
    if (argc < 2) { fprintf(stderr, "usage: doscp [-i image] [-rv] source ... target (drive paths start with ::)\n"); exit(2); }
    for (i = 0; i < argc - 1; i++)
        if ((strncmp(argv[i], "::", 2) == 0) == todrive)
            die(argv[i], todrive ? "on the drive, as is the target: doscp copies between P32W and the drive" :
                                   "neither it nor the target is on the drive (drive paths start with ::)");
    opendev(todrive ? O_RDWR : O_RDONLY);
    openvol();
    if (todrive) {
        int there = namei(target, &de, &dir, last);
        if (there && (de.e[11] & A_DIR)) {
            unsigned d = de.idx == UNKNOWN ? rootdir : dclus(de.e);
            for (i = 0; i < argc - 1; i++) put(argv[i], d, base(argv[i]), dpath(target, base(argv[i])));
        } else if (argc > 2) die(dpath(target, 0), "not a directory");
        else put(argv[0], dir, there ? de.name : last, dpath(target, 0));
        finish();
    } else {
        struct stat st;
        int isdir = stat(target, &st) == 0 && S_ISDIR(st.st_mode);
        if (argc > 2 && !isdir) die(target, "not a directory");
        for (i = 0; i < argc - 1; i++) {
            if (!namei(argv[i], &de, &dir, 0)) { warn(dpath(argv[i], 0), "not there"); continue; }
            if (isdir) {
                char *t = join(target, de.idx == UNKNOWN ? "" : de.name);
                get(&de, t, dpath(argv[i], 0));
                free(t);
            } else get(&de, target, dpath(argv[i], 0));
        }
    }
}

/* ---------------------------------------------------------------- dosformat */
static void chs(unsigned char *p, unsigned lba)     /* a CHS address in 255 heads, 63 sectors */
{
    unsigned c = lba / (255 * 63), h = lba / 63 % 255, s = lba % 63 + 1;
    if (c > 1023) { c = 1023; h = 254; s = 63; }
    p[0] = h;
    p[1] = s | (c >> 2 & 0xC0);
    p[2] = c;
}

static void dosformat(int fat, const char *label)
{
    unsigned char b[SEC], *p;
    unsigned psize, k, sp = 0, t1, t2, id = time(0) * 2654435761u;
    char lab[12];
    opendev(O_RDWR);
    part = 2048;                        /* the partition starts at 1 MiB, as flash likes */
    if (dsize < part + 4096) die(drive, "too small for a FAT drive (3 MB at least)");
    psize = dsize - part;
    if (!fat) fat = psize >= 1048576 ? 32 : 16;     /* FAT32 from 512 MB (Microsoft's choice) */
    ftype = fat;
    nfats = 2;
    if (fat == 32) {
        rsvd = 32; rootents = 0;
        spc = psize <= 532480 ? 1 : psize <= 16777216 ? 8 : psize <= 33554432 ? 16 : psize <= 67108864 ? 32 : 64;
    } else {
        rsvd = 1; rootents = 512;
    }
    rootsecs = rootents * 32 / SEC;
    for (sp = fat == 32 ? spc : 1; sp <= 128; sp *= 2) {    /* (FAT16: the smallest cluster that fits) */
        t1 = psize - rsvd - rootsecs;
        t2 = 256 * sp + nfats;
        if (fat == 32) t2 /= 2;
        fatsz = (t1 + t2 - 1) / t2;
        nclus = (psize - rsvd - nfats * fatsz - rootsecs) / sp;
        if (fat == 32 || nclus < 65525) break;
    }
    spc = sp;
    if (fat == 16 && (spc > 64 || nclus < 4085))
        die(drive, spc > 64 ? "too big for FAT16 (2 GB at most; use -F 32)" : "too small for FAT16");
    if (fat == 32 && nclus < 65525) die(drive, "too small for FAT32 (33 MB at least; use -F 16)");
    memset(lab, ' ', 11);
    lab[11] = 0;
    memcpy(lab, "NO NAME", 7);
    if (label) {
        for (k = 0; k < 11 && label[k]; k++) {
            int c = (unsigned char)label[k];
            if (c >= 'a' && c <= 'z') c -= 32;
            if (c != ' ' && !shortok(c)) die(label, "not a volume label (letters, digits, - and _)");
            lab[k] = c;
        }
        for (; k < 11; k++) lab[k] = ' ';
    }

    zero(0, part);                      /* the MBR, and anything left before the partition */
    memset(b, 0, SEC);
    p32(b + 440, id);
    p = b + 446;
    chs(p + 1, part);
    p[4] = fat == 32 ? 0x0C : 0x0E;     /* FAT32 or FAT16, by LBA */
    chs(p + 5, dsize - 1);
    p32(p + 8, part);
    p32(p + 12, psize);
    b[510] = 0x55; b[511] = 0xAA;
    wrsec(0, 1, b);

    zero(part, rsvd);                   /* the boot sector */
    memset(b, 0, SEC);
    b[0] = 0xEB; b[1] = fat == 32 ? 0x58 : 0x3C; b[2] = 0x90;
    memcpy(b + 3, "P32UNIX ", 8);
    p16(b + 11, SEC);
    b[13] = spc;
    p16(b + 14, rsvd);
    b[16] = nfats;
    p16(b + 17, rootents);
    if (fat == 16 && psize < 65536) p16(b + 19, psize); else p32(b + 32, psize);
    b[21] = 0xF8;                       /* media: a fixed disk */
    p16(b + 24, 63); p16(b + 26, 255);  /* sectors per track, heads */
    p32(b + 28, part);                  /* hidden sectors: those before the partition */
    if (fat == 32) {
        p32(b + 36, fatsz);
        p32(b + 44, 2);                 /* the root directory: cluster 2 */
        p16(b + 48, 1);                 /* FSInfo */
        p16(b + 50, 6);                 /* the backup boot sector */
        p = b + 64;
    } else {
        p16(b + 22, fatsz);
        p = b + 36;
    }
    p[0] = 0x80;                        /* drive number */
    p[2] = 0x29;                        /* (the extended boot signature: ID, label and type follow) */
    p32(p + 3, id);
    memcpy(p + 7, lab, 11);
    memcpy(p + 18, fat == 32 ? "FAT32   " : "FAT16   ", 8);
    p[26] = 0xCD; p[27] = 0x18;         /* the boot code: int 18h, "not bootable, try the next disk" */
    b[510] = 0x55; b[511] = 0xAA;
    wrsec(part, 1, b);
    if (fat == 32) {
        wrsec(part + 6, 1, b);
        memset(b, 0, SEC);
        p32(b, 0x41615252);
        p32(b + 484, 0x61417272);
        p32(b + 488, nclus - 1);        /* free: all but the root's cluster */
        p32(b + 492, 3);
        p32(b + 508, 0xAA550000);
        wrsec(part + 1, 1, b);
        wrsec(part + 7, 1, b);
    }

    fatstart = part + rsvd;
    rootstart = fatstart + nfats * fatsz;
    datastart = rootstart + rootsecs;
    zero(fatstart, nfats * fatsz + rootsecs);
    memset(b, 0, SEC);
    if (fat == 32) {
        p32(b, 0x0FFFFFF8); p32(b + 4, 0x0FFFFFFF); p32(b + 8, 0x0FFFFFFF);    /* media, clean, the root's end */
        zero(datastart, spc);
    } else {
        p16(b, 0xFFF8); p16(b + 2, 0xFFFF);
    }
    for (k = 0; k < nfats; k++) wrsec(fatstart + k * fatsz, 1, b);
    if (label) {                        /* the label, as the first entry of the top directory */
        memset(b, 0, SEC);
        memcpy(b, lab, 11);
        b[11] = A_VOL;
        dostime(time(0), &t1, &t2);
        p16(b + 22, t2); p16(b + 24, t1);
        wrsec(fat == 32 ? datastart : rootstart, 1, b);
    }
    printf("%s: FAT%d, %u KiB in %u clusters of %u bytes\n", drive, fat, nclus / 2 * spc + nclus % 2 * spc / 2, nclus, spc * SEC);
}

/* ---------------------------------------------------------------- main */
static void usage(void)
{
    if (!strcmp(prog, "dosdir")) fprintf(stderr, "usage: dosdir [-i image] [path ...]\n");
    else if (!strcmp(prog, "doscp")) fprintf(stderr, "usage: doscp [-i image] [-rv] source ... target (drive paths start with ::)\n");
    else if (!strcmp(prog, "dosrm")) fprintf(stderr, "usage: dosrm [-i image] [-rv] path ...\n");
    else if (!strcmp(prog, "dosmkdir")) fprintf(stderr, "usage: dosmkdir [-i image] path ...\n");
    else if (!strcmp(prog, "dosformat")) fprintf(stderr, "usage: dosformat [-i image] [-F 16|32] [-v label]\n");
    else fprintf(stderr, "usage: run as dosdir, doscp, dosrm, dosmkdir or dosformat\n");
    exit(2);
}

int main(int argc, char **argv)
{
    const char *label = 0;
    int i, fat = 0;
    prog = strrchr(argv[0], '/');
    prog = prog ? prog + 1 : argv[0];
    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char *o;
        if (!strcmp(argv[i], "--")) { i++; break; }
        for (o = argv[i] + 1; *o; o++)
            switch (*o) {
            case 'i': if (o[1] || ++i >= argc) usage(); drive = argv[i]; goto next;
            case 'F': if (o[1] || ++i >= argc) usage(); fat = atoi(argv[i]);
                      if (fat != 16 && fat != 32) usage();
                      goto next;
            case 'v':
                if (!strcmp(prog, "dosformat")) { if (o[1] || ++i >= argc) usage(); label = argv[i]; goto next; }
                verbose = 1;
                break;
            case 'r': recurse = 1; break;
            default: usage();
            }
    next: ;
    }
    argc -= i;
    argv += i;
    if (!strcmp(prog, "dosdir")) dosdir(argc, argv);
    else if (!strcmp(prog, "doscp")) { if (argc < 2) usage(); doscp(argc, argv); }
    else if (!strcmp(prog, "dosrm")) { if (argc < 1) usage(); dosrm(argc, argv); }
    else if (!strcmp(prog, "dosmkdir")) { if (argc < 1) usage(); dosmkdir(argc, argv); }
    else if (!strcmp(prog, "dosformat")) { if (argc) usage(); dosformat(fat, label); }
    else usage();
    return status;
}
