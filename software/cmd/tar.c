/* tar -- tape archives, POSIX ustar format (D25): exchangeable with the
 * Mac's tar (bsdtar) and GNU tar.
 *
 *   tar c[vz]f archive file ...     create (directories recursively)
 *   tar t[vz]f archive [name ...]   list
 *   tar x[vz]f archive [name ...]   extract (all, or the names and what is below them)
 *
 * "-" (or no f) is stdin / stdout; the leading "-" on the keys is optional.
 * c stores names less a leading / and leaves out the archive itself.
 * z runs the archive through gzip / gunzip. Kept: modes, owners (by name,
 * root only, when extracting), modification times, directories, hard links,
 * devices and FIFOs. Read too: the pax extended headers macOS writes (their
 * path and linkpath; the rest is skipped) and GNU long names; symbolic links
 * are reported and skipped (P32W Unix has none). A Mac archive may hold
 * "._name" AppleDouble files: make it with COPYFILE_DISABLE=1 to leave them out.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <pwd.h>
#include <grp.h>
#include <utime.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define BLK 512
#define RECORD (20 * BLK)           /* the archive is written in 10 KiB records */
#define NAMEMAX 256

struct hdr {                        /* POSIX ustar header: one 512-byte block */
    char name[100], mode[8], uid[8], gid[8], size[12], mtime[12], chksum[8], type;
    char linkname[100], magic[6], version[2], uname[32], gname[32], devmajor[8], devminor[8];
    char prefix[155], pad[12];
};

static int afd, verbose, status, ziped;
static int self_dev = -1;           /* the archive itself (c): not archived */
static unsigned self_ino;
static int stripped;                /* (the "removing leading /" message given) */
static char rec[RECORD];
static int recpos, reclen;          /* (reading: bytes in rec; writing: bytes filled) */
static char **names;
static int nnames;
static char *found;                 /* (t, x: which names were met) */

/* ---------------------------------------------------------------- the archive, a block at a time */
static void die(const char *what, const char *why)
{
    fflush(stdout);                 /* (the listing so far first) */
    fprintf(stderr, "tar: %s: %s\n", what, why);
    exit(2);
}

static int rdblk(char *b)           /* 1, or 0 at the end */
{
    if (recpos >= reclen) {
        int n = 0, r;
        while (n < BLK) {           /* (a pipe may deliver less than a record) */
            r = read(afd, rec + n, RECORD - n);
            if (r < 0) die("read", strerror(errno));
            if (r == 0) break;
            n += r;
            if (n % BLK == 0) break;
        }
        if (n == 0) return 0;
        if (n % BLK) die("archive", "truncated");
        reclen = n;
        recpos = 0;
    }
    memcpy(b, rec + recpos, BLK);
    recpos += BLK;
    return 1;
}

static void wrblk(const char *b)
{
    memcpy(rec + recpos, b, BLK);
    recpos += BLK;
    if (recpos == RECORD) {
        if (write(afd, rec, RECORD) != RECORD) die("write", strerror(errno));
        recpos = 0;
    }
}

/* ---------------------------------------------------------------- header fields */
static void octal(char *f, int width, unsigned v)  /* width-1 digits and a NUL */
{
    int i;
    f[width - 1] = 0;
    for (i = width - 2; i >= 0; i--) { f[i] = '0' + (v & 7); v >>= 3; }
}

static unsigned unoctal(const char *f, int width)
{
    unsigned v = 0;
    int i = 0;
    while (i < width && (f[i] == ' ' || f[i] == 0)) i++;
    for (; i < width && f[i] >= '0' && f[i] <= '7'; i++) v = v * 8 + (f[i] - '0');
    return v;
}

static unsigned checksum(struct hdr *h)
{
    unsigned char *p = (unsigned char *)h;
    unsigned s = 0;
    int i;
    for (i = 0; i < BLK; i++)
        s += (i >= 148 && i < 156) ? ' ' : p[i];
    return s;
}

static void field(char *dst, const char *src, int width)    /* (not NUL-terminated if full) */
{
    int n = strlen(src);
    memcpy(dst, src, n < width ? n : width);
}

static void strfield(char *dst, const char *src, int width)  /* a header string as a C string */
{
    memcpy(dst, src, width);
    dst[width] = 0;
}

/* ---------------------------------------------------------------- create */
struct link { unsigned ino; int dev; char *name; struct link *next; };
static struct link *links;

static void put_file(const char *path);

static const char *relative(const char *path)    /* the name less any leading / */
{
    if (path[0] != '/') return path;
    if (!stripped) { fprintf(stderr, "tar: removing leading / from member names\n"); stripped = 1; }
    while (path[0] == '/') path++;
    return path;
}

static int put_header(const char *path, struct stat *st, int type, const char *linkname)
{
    struct hdr h;
    struct passwd *pw;
    struct group *gr;
    int n;
    path = relative(path);
    if (linkname) linkname = relative(linkname);
    if (!path[0]) return 0;         /* (the directory / itself: only what is below it) */
    n = strlen(path);
    memset(&h, 0, sizeof h);
    if (n <= 100)
        field(h.name, path, 100);
    else {                          /* split at a '/' into prefix (<= 155) and name (<= 100) */
        int k = n - 101;
        while (k < n && path[k] != '/') k++;
        if (k >= n || k > 155 || k == 0) {
            fprintf(stderr, "tar: %s: name too long\n", path);
            status = 1;
            return -1;
        }
        memcpy(h.prefix, path, k);
        field(h.name, path + k + 1, 100);
    }
    octal(h.mode, 8, st->st_mode & 07777);
    octal(h.uid, 8, st->st_uid);
    octal(h.gid, 8, st->st_gid);
    octal(h.size, 12, type == '0' ? st->st_size : 0);
    octal(h.mtime, 12, st->st_mtime);
    h.type = type;
    if (linkname) field(h.linkname, linkname, 100);
    memcpy(h.magic, "ustar", 6);
    memcpy(h.version, "00", 2);
    if ((pw = getpwuid(st->st_uid)) != 0) field(h.uname, pw->pw_name, 32);
    if ((gr = getgrgid(st->st_gid)) != 0) field(h.gname, gr->gr_name, 32);
    if (type == '3' || type == '4') {
        octal(h.devmajor, 8, (st->st_rdev >> 8) & 0xFF);
        octal(h.devminor, 8, st->st_rdev & 0xFF);
    }
    octal(h.chksum, 7, checksum(&h));
    h.chksum[7] = ' ';
    wrblk((char *)&h);
    if (verbose) fprintf(stderr, "a %s%s%s\n", path, linkname ? " link to " : "", linkname ? linkname : "");
    return 0;
}

static void put_dir(const char *path, struct stat *st)
{
    char name[NAMEMAX + 2];
    DIR *d;
    struct dirent *e;
    int n = strlen(path);
    if (n + 1 > NAMEMAX) { fprintf(stderr, "tar: %s: name too long\n", path); status = 1; return; }
    strcpy(name, path);
    if (n && name[n - 1] != '/') strcat(name, "/");
    if (put_header(name, st, '5', 0) < 0) return;
    if (!(d = opendir(path))) { fprintf(stderr, "tar: %s: %s\n", path, strerror(errno)); status = 1; return; }
    {   /* the names first, then the directory closed: a deep tree needs no open file per level */
        char *list = 0, *p;
        int len = 0, cap = 0;
        while ((e = readdir(d)) != 0) {
            int k = strlen(e->d_name) + 1;
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            if (len + k > cap) {
                char *nl = realloc(list, cap = cap * 2 + 512);
                if (!nl) die(path, "out of memory");
                list = nl;
            }
            strcpy(list + len, e->d_name);
            len += k;
        }
        closedir(d);
        for (p = list; p && p < list + len; p += strlen(p) + 1) {
            char sub[NAMEMAX + 32];
            sprintf(sub, "%s%s", name, p);
            put_file(sub);
        }
        free(list);
    }
}

static void put_file(const char *path)
{
    struct stat st;
    int fd, n, left;
    char b[BLK];
    if (stat(path, &st) < 0) { fprintf(stderr, "tar: %s: %s\n", path, strerror(errno)); status = 1; return; }
    if (st.st_dev == self_dev && st.st_ino == self_ino) {
        fprintf(stderr, "tar: %s: the archive itself, not archived\n", path);
        return;
    }
    if (S_ISDIR(st.st_mode)) { put_dir(path, &st); return; }
    if (st.st_nlink > 1) {          /* a second name for a file already in the archive */
        struct link *l;
        for (l = links; l; l = l->next)
            if (l->ino == st.st_ino && l->dev == st.st_dev) { put_header(path, &st, '1', l->name); return; }
        l = malloc(sizeof *l);
        if (l && (l->name = malloc(strlen(path) + 1)) != 0) {
            l->ino = st.st_ino; l->dev = st.st_dev;
            strcpy(l->name, path);
            l->next = links; links = l;
        }
    }
    if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)) { put_header(path, &st, S_ISCHR(st.st_mode) ? '3' : '4', 0); return; }
    if ((st.st_mode & S_IFMT) == S_IFIFO) { put_header(path, &st, '6', 0); return; }
    if ((fd = open(path, O_RDONLY)) < 0) { fprintf(stderr, "tar: %s: %s\n", path, strerror(errno)); status = 1; return; }
    if (put_header(path, &st, '0', 0) < 0) { close(fd); return; }
    for (left = st.st_size; left > 0; left -= BLK) {
        n = read(fd, b, BLK);
        if (n < 0) n = 0;
        if (n < BLK) memset(b + n, 0, BLK - n);  /* (a file that shrank: padded with zeros) */
        wrblk(b);
    }
    close(fd);
}

static void finish(void)
{
    char z[BLK];
    memset(z, 0, BLK);
    wrblk(z);
    wrblk(z);
    while (recpos) wrblk(z);        /* pad the last record */
}

/* ---------------------------------------------------------------- list and extract */
static int wanted(const char *name)
{
    int i, n, r = 0;
    if (!nnames) return 1;
    for (i = 0; i < nnames; i++) {
        n = strlen(names[i]);
        while (n > 1 && names[i][n - 1] == '/') n--;
        if (!strncmp(name, names[i], n) && (name[n] == 0 || name[n] == '/')) {
            if (found) found[i] = 1;
            r = 1;
        }
    }
    return r;
}

static void skip(unsigned size)
{
    char b[BLK];
    for (; size > 0; size -= size < BLK ? size : BLK)
        if (!rdblk(b)) die("archive", "truncated");
}

/* a pax extended header's records: "len key=value\n"; path and linkpath kept */
static void pax(unsigned size, char *path, char *linkpath)
{
    char *buf = malloc(size + 1), *p;
    unsigned k;
    if (!buf) die("pax header", "out of memory");
    for (k = 0; k < size; k += BLK) {
        char b[BLK];
        if (!rdblk(b)) die("archive", "truncated");
        memcpy(buf + k, b, size - k < BLK ? size - k : BLK);
    }
    buf[size] = 0;
    for (p = buf; p < buf + size; ) {
        int len = atoi(p);
        char *kv = strchr(p, ' '), *eq;
        if (len <= 0 || !kv || p + len > buf + size) break;
        kv++;
        p[len - 1] = 0;             /* (the newline) */
        if ((eq = strchr(kv, '=')) != 0) {
            *eq = 0;
            if (!strcmp(kv, "path") && strlen(eq + 1) < NAMEMAX) strcpy(path, eq + 1);
            if (!strcmp(kv, "linkpath") && strlen(eq + 1) < NAMEMAX) strcpy(linkpath, eq + 1);
        }
        p += len;
    }
    free(buf);
}

static void mkparents(char *path)   /* make the directories a path needs */
{
    char *s;
    for (s = path + 1; *s; s++)
        if (*s == '/') {
            *s = 0;
            if (access(path, 0) < 0) mkdir(path, 0777);
            *s = '/';
        }
}

static void set_attrs(const char *path, struct hdr *h, unsigned mode, unsigned mtime)
{
    struct utimbuf ut;
    if (geteuid() == 0) {           /* owners: by name if this system knows it, else the number */
        char un[33], gn[33];
        struct passwd *pw;
        struct group *gr;
        int uid = unoctal(h->uid, 8), gid = unoctal(h->gid, 8);
        strfield(un, h->uname, 32);
        strfield(gn, h->gname, 32);
        if (un[0] && (pw = getpwnam(un)) != 0) uid = pw->pw_uid;
        if (gn[0] && (gr = getgrnam(gn)) != 0) gid = gr->gr_gid;
        chown(path, uid, gid);
    }
    chmod(path, mode);
    ut.actime = ut.modtime = mtime;
    utime(path, &ut);
}

/* directories get their modes and times last (writing into one changes its time;
   a read-only one could not be filled) */
struct dirattr { struct hdr h; unsigned mode, mtime; struct dirattr *next; char path[1]; };
static struct dirattr *dirattrs;

static void later(const char *path, struct hdr *h, unsigned mode, unsigned mtime)
{
    struct dirattr *d = malloc(sizeof *d + strlen(path));
    if (!d) { set_attrs(path, h, mode, mtime); return; }
    d->h = *h; d->mode = mode; d->mtime = mtime;
    strcpy(d->path, path);
    d->next = dirattrs; dirattrs = d;   /* (deepest first when applied) */
}

static void dirs_done(void)
{
    struct dirattr *d;
    for (d = dirattrs; d; d = d->next) set_attrs(d->path, &d->h, d->mode, d->mtime);
}

static void longname(unsigned size, char *dst)  /* GNU 'L' / 'K': the name in the data */
{
    char b[BLK];
    unsigned k;
    for (k = 0; k < size; k += BLK) {
        if (!rdblk(b)) die("archive", "truncated");
        if (k < NAMEMAX) memcpy(dst + k, b, NAMEMAX - k < BLK ? NAMEMAX - k : BLK);
    }
    dst[size < NAMEMAX ? size : NAMEMAX - 1] = 0;
}

/* t with v: "-rw-r--r-- ken/staff      123 2026-10-14 09:30 name" (local time) */
static void long_line(struct hdr *h, int type, unsigned mode, unsigned size, unsigned mtime, const char *path)
{
    static const char rwx[] = "rwxrwxrwx";
    char m[11], un[33], gn[33];
    struct passwd *pw;
    struct group *gr;
    time_t t = mtime;
    struct tm *tm;
    int i;
    m[0] = type == '5' ? 'd' : type == '3' ? 'c' : type == '4' ? 'b' : type == '6' ? 'p' : type == '2' ? 'l' :
           type == '1' ? 'h' : '-';
    for (i = 0; i < 9; i++) m[i + 1] = mode & (0400 >> i) ? rwx[i] : '-';
    if (mode & 04000) m[3] = m[3] == 'x' ? 's' : 'S';
    if (mode & 02000) m[6] = m[6] == 'x' ? 's' : 'S';
    if (mode & 01000) m[9] = m[9] == 'x' ? 't' : 'T';
    m[10] = 0;
    strfield(un, h->uname, 32);
    strfield(gn, h->gname, 32);
    if (!un[0]) {
        if ((pw = getpwuid(unoctal(h->uid, 8))) != 0) strcpy(un, pw->pw_name);
        else sprintf(un, "%u", unoctal(h->uid, 8));
    }
    if (!gn[0]) {
        if ((gr = getgrgid(unoctal(h->gid, 8))) != 0) strcpy(gn, gr->gr_name);
        else sprintf(gn, "%u", unoctal(h->gid, 8));
    }
    tm = localtime(&t);
    printf("%s %s/%s %8u %04d-%02d-%02d %02d:%02d %s", m, un, gn, size, tm->tm_year + 1900,
           tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, path);
}

static void list_or_extract(int extract)
{
    struct hdr h;
    char path[NAMEMAX + 160], linkpath[NAMEMAX], xpath[NAMEMAX], xlink[NAMEMAX];
    int zeros = 0;
    xpath[0] = xlink[0] = 0;
    while (rdblk((char *)&h)) {
        unsigned size, mode, mtime, sum;
        int type, keep;
        if (((char *)&h)[0] == 0) {     /* two zero blocks end the archive */
            if (++zeros == 2) break;
            continue;
        }
        zeros = 0;
        sum = unoctal(h.chksum, 8);
        if (sum != checksum(&h)) die("archive", "header checksum error (not a tar archive?)");
        size = unoctal(h.size, 12);
        mode = unoctal(h.mode, 8) & 07777;
        mtime = unoctal(h.mtime, 12);
        type = h.type ? h.type : '0';
        if (type == 'x') { pax(size, xpath, xlink); continue; }
        if (type == 'g') { skip(size); continue; }
        if (type == 'L') { longname(size, xpath); continue; }
        if (type == 'K') { longname(size, xlink); continue; }
        if (xpath[0]) strcpy(path, xpath);
        else {
            char nm[101], pf[156];
            strfield(nm, h.name, 100);
            strfield(pf, h.prefix, 155);
            if (!memcmp(h.magic, "ustar", 5) && pf[0]) sprintf(path, "%s/%s", pf, nm);
            else strcpy(path, nm);
        }
        if (xlink[0]) strcpy(linkpath, xlink);
        else strfield(linkpath, h.linkname, 100);
        xpath[0] = xlink[0] = 0;
        keep = wanted(path);
        if (!extract || !keep) {
            if (keep && !extract) {
                if (verbose)
                    long_line(&h, type, mode, size, mtime, path);
                else
                    printf("%s", path);
                if (type == '1') printf(" link to %s", linkpath);
                if (type == '2') printf(" -> %s", linkpath);
                putchar('\n');
            }
            if (type == '0' || type == '7') skip(size);
            continue;
        }
        /* extract */
        if (path[0] == '/' || strstr(path, "../") || !strcmp(path, "..")) {
            fprintf(stderr, "tar: %s: not extracted (an absolute path or ..)\n", path);
            if (type == '0' || type == '7') skip(size);
            status = 1;
            continue;
        }
        mkparents(path);
        if (verbose) fprintf(stderr, "x %s\n", path);
        switch (type) {
        case '5': {
            int n = strlen(path);
            while (n > 1 && path[n - 1] == '/') path[--n] = 0;
            if (mkdir(path, 0700) < 0 && errno != EEXIST) {
                fprintf(stderr, "tar: %s: %s\n", path, strerror(errno)); status = 1; break;
            }
            later(path, &h, mode, mtime);
            break;
        }
        case '1':
            unlink(path);
            if (link(linkpath, path) < 0) { fprintf(stderr, "tar: %s: %s\n", path, strerror(errno)); status = 1; }
            break;
        case '2':
            fprintf(stderr, "tar: %s: a symbolic link (to %s), skipped\n", path, linkpath);
            status = 1;
            break;
        case '3': case '4': case '6': {
            int dev = (unoctal(h.devmajor, 8) << 8) | unoctal(h.devminor, 8);
            unlink(path);
            if (mknod(path, (type == '3' ? S_IFCHR : type == '4' ? S_IFBLK : S_IFIFO) | mode, dev) < 0) {
                fprintf(stderr, "tar: %s: %s\n", path, strerror(errno)); status = 1; break;
            }
            set_attrs(path, &h, mode, mtime);
            break;
        }
        case '0': case '7': {
            int fd;
            char b[BLK];
            unsigned left;
            unlink(path);
            if ((fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0) {
                fprintf(stderr, "tar: %s: %s\n", path, strerror(errno));
                status = 1;
                skip(size);
                break;
            }
            for (left = size; left > 0; ) {
                unsigned n = left < BLK ? left : BLK;
                if (!rdblk(b)) die("archive", "truncated");
                if (write(fd, b, n) != (int)n) { fprintf(stderr, "tar: %s: %s\n", path, strerror(errno)); status = 1; }
                left -= n;
            }
            close(fd);
            set_attrs(path, &h, mode, mtime);
            break;
        }
        default:
            fprintf(stderr, "tar: %s: unknown entry type '%c', skipped\n", path, type);
            status = 1;
            skip(size);
        }
    }
}

/* ---------------------------------------------------------------- z: through gzip */
static int filter_pid;

static void filter(int writing)
{
    int p[2];
    if (pipe(p) < 0) die("pipe", strerror(errno));
    if ((filter_pid = fork()) < 0) die("fork", strerror(errno));
    if (filter_pid == 0) {
        if (writing) { dup2(p[0], 0); dup2(afd, 1); }
        else { dup2(afd, 0); dup2(p[1], 1); }
        close(p[0]); close(p[1]);
        if (writing) execl("/bin/gzip", "gzip", "-c", (char *)0);
        else execl("/bin/gzip", "gzip", "-dc", (char *)0);
        perror("tar: /bin/gzip");
        _exit(2);
    }
    if (afd > 1) close(afd);
    if (writing) { afd = p[1]; close(p[0]); }
    else { afd = p[0]; close(p[1]); }
    ziped = 1;
}

static void filter_done(void)
{
    int st;
    if (!ziped) return;
    close(afd);
    while (wait(&st) != filter_pid) ;
    if (st) status = status ? status : 2;
}

int main(int argc, char **argv)
{
    char *keys, *file = 0, mode = 0;
    int i = 2, k, z = 0;
    if (argc < 2) {
        fprintf(stderr, "usage: tar c|t|x[vzf] [archive] [file ...]\n");
        return 2;
    }
    keys = argv[1];
    if (*keys == '-') keys++;
    for (k = 0; keys[k]; k++)
        switch (keys[k]) {
        case 'c': case 't': case 'x': mode = keys[k]; break;
        case 'v': verbose = 1; break;
        case 'z': z = 1; break;
        case 'f':
            if (i >= argc) { fprintf(stderr, "tar: f needs an archive name\n"); return 2; }
            file = argv[i++];
            break;
        default:
            fprintf(stderr, "tar: unknown key '%c'\n", keys[k]);
            return 2;
        }
    if (!mode) { fprintf(stderr, "tar: one of c, t, x is needed\n"); return 2; }
    names = argv + i;
    nnames = argc - i;
    if (mode == 'c') {
        if (!nnames) { fprintf(stderr, "tar: nothing to archive\n"); return 2; }
        if (!file || !strcmp(file, "-")) afd = 1;
        else if ((afd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0) die(file, strerror(errno));
        {
            struct stat st;
            if (fstat(afd, &st) == 0 && S_ISREG(st.st_mode)) { self_dev = st.st_dev; self_ino = st.st_ino; }
        }
        if (z) filter(1);
        for (k = 0; k < nnames; k++) put_file(names[k]);
        finish();
    } else {
        if (!file || !strcmp(file, "-")) afd = 0;
        else if ((afd = open(file, O_RDONLY)) < 0) die(file, strerror(errno));
        if (z) filter(0);
        found = calloc(nnames + 1, 1);
        list_or_extract(mode == 'x');
        dirs_done();
        fflush(stdout);
        for (k = 0; found && k < nnames; k++)
            if (!found[k]) { fprintf(stderr, "tar: %s: not found in the archive\n", names[k]); status = 1; }
    }
    filter_done();
    return status;
}
